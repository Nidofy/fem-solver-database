#pragma once
#include "io/foundation_store.hpp"
#include <mutex>

namespace sdb {
inline std::size_t state_layout_bytes(const StateLayout &layout) {
    return sum(product(layout.addresses.size(),sizeof(lifecycle::ResultAddress)),
               product(sum(layout.indices.size(),layout.offsets.size()),sizeof(std::uint64_t)));
}
inline std::size_t state_payload_bytes(const RegisteredState &state) {
    std::size_t n=0;
    for(auto e:state.entries()) n=sum(n,sum(e->payload_bytes(e->count()),e->addressing()?state_layout_bytes(*e->addressing()):0));
    return n;
}
struct StateMemoryStats {
    std::size_t resident_payload=0,trial_payload=0,high_water_payload=0,copied_payload=0;
};
// Opt-in owner. Copies the supplied registry, so old Field references cannot
// mutate owned storage. Views are valid only within their lease; legacy APIs
// outside this owner retain their original joined-phase contract.
class StateSession {
    template<class V> class LeaseView {
        V view_;
        std::weak_ptr<int> lease_;
      public:
        LeaseView(V v,const std::shared_ptr<int> &lease):view_(std::move(v)),lease_(lease){}
        void validate() const {if(lease_.expired())throw std::logic_error("expired state lease");view_.validate();}
        auto raw() const {validate();return view_.raw();}
        auto size() const {validate();return view_.size();}
        decltype(auto) at(std::size_t row,std::size_t component=0) const {validate();return view_.at(row,component);}
    };
    struct Core {
        std::mutex mutex;
        std::size_t readers=0;
        bool writer=false;
        RegisteredState state;
        std::size_t budget;
        StateMemoryStats stats;
        Core(const RegisteredState &s,std::size_t limit):budget(limit) {
            auto bytes=state_payload_bytes(s);
            if(bytes>limit) throw std::length_error("state payload budget");
            state=s; stats={bytes,0,bytes,bytes};
        }
        void admit(std::size_t extra) {
            auto peak=sum(sum(stats.resident_payload,stats.trial_payload),extra);
            if(peak>budget) throw std::length_error("state staging payload budget");
            stats.high_water_payload=std::max(stats.high_water_payload,peak);
        }
    };
    std::shared_ptr<Core> core_;
    template<bool Write> class Lease {
        friend class StateSession;
        std::shared_ptr<Core> core_;
        std::shared_ptr<int> token_=std::make_shared<int>(0);
        explicit Lease(std::shared_ptr<Core> c):core_(std::move(c)) {
            std::lock_guard lock(core_->mutex);
            if(core_->writer || (Write && core_->readers)) throw std::logic_error("state access busy");
            if constexpr(Write) core_->writer=true; else ++core_->readers;
        }
      public:
        Lease(const Lease&)=delete;
        Lease &operator=(const Lease&)=delete;
        Lease(Lease&&)=default;
        Lease &operator=(Lease&&)=delete;
        ~Lease() {
            token_.reset();
            if(core_) { std::lock_guard lock(core_->mutex); if constexpr(Write) core_->writer=false; else --core_->readers; }
        }
        template<class F> auto field(const std::string &name) const {
            if(!core_) throw std::logic_error("moved state lease");
            if constexpr(Write) return LeaseView(core_->state.template get<F>(name).checked_write(),token_);
            else return LeaseView(std::as_const(core_->state).template get<F>(name).checked_read(),token_);
        }
    };
  public:
    explicit StateSession(const RegisteredState &state,std::size_t payload_budget=std::numeric_limits<std::size_t>::max())
        :core_(std::make_shared<Core>(state,payload_budget)) { state.validate(); }
    StateSession(const StateSession&)=delete;
    StateSession &operator=(const StateSession&)=delete;
    auto read() const { return Lease<false>(core_); }
    auto write() { return Lease<true>(core_); }
    StateMemoryStats memory() const { auto lease=read(); return core_->stats; }
    void begin_trial() {
        auto lease=write(); auto &c=*core_;
        c.admit(c.stats.resident_payload); c.state.begin_trial();
        c.stats.trial_payload=c.stats.resident_payload;
        c.stats.copied_payload=sum(c.stats.copied_payload,c.stats.resident_payload);
    }
    void commit() { auto lease=write(); core_->state.commit(); core_->stats.trial_payload=0; }
    void rollback() {
        auto lease=write(); core_->state.rollback(); core_->stats.resident_payload=core_->stats.trial_payload; core_->stats.trial_payload=0;
    }
    void remap(const std::string &name,StateLayout layout) {
        auto lease=write(); auto &c=*core_; const auto &e=c.state.find(name);
        layout.validate(e.descriptor());
        auto before=sum(e.payload_bytes(e.count()),e.addressing()?state_layout_bytes(*e.addressing()):0);
        auto after=sum(e.payload_bytes(layout.addresses.size()),state_layout_bytes(layout));
        c.admit(after); c.state.remap(name,std::move(layout));
        c.stats.resident_payload=sum(c.stats.resident_payload-before,after);
        c.stats.copied_payload=sum(c.stats.copied_payload,after);
    }
    void save(const std::filesystem::path &path,const DofSpace &space,std::uint64_t file_budget=lifecycle::native::default_limit) const {
        auto lease=read(); FoundationStore::save(path,space,core_->state,file_budget);
    }
    void restore(const std::filesystem::path &path,const DofSpace &space,std::uint64_t file_budget=lifecycle::native::default_limit) {
        auto lease=write(); auto &c=*core_;
        c.admit(c.stats.resident_payload); FoundationStore::restore(path,space,c.state,file_budget);
        c.stats.copied_payload=sum(c.stats.copied_payload,c.stats.resident_payload);
    }
};
} // namespace sdb
