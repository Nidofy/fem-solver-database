#pragma once
#include "field.hpp"
#include "io/crc64.hpp"
#include "io/native.hpp"

namespace sdb {
struct SlotHandle {
    std::uint32_t slot, generation;
    bool operator==(const SlotHandle &) const = default;
};

namespace slot_detail {
namespace wire = lifecycle::native;
inline constexpr auto end = UINT32_MAX;

// SplitMix64 finalizer and linear probing. Tombstones are compacted when the
// occupied probe chains exceed 75% of capacity, including on churn-only loads.
class KeyIndex {
    struct Bucket { std::uint64_t key=0; std::uint32_t slot=0; enum { Empty, Live, Deleted } state=Empty; };
    std::vector<Bucket> entries_ = std::vector<Bucket>(16);
    std::size_t live_=0, occupied_=0;
    std::size_t start(std::uint64_t x) const {
        x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
        return (x^(x>>31)) & (entries_.size()-1);
    }
    std::size_t next(std::size_t i) const { return (i+1)&(entries_.size()-1); }
    std::size_t probe(std::uint64_t key) const {
        auto i=start(key);
        while(entries_[i].state!=Bucket::Empty && (entries_[i].state!=Bucket::Live || entries_[i].key!=key)) i=next(i);
        return i;
    }
  public:
    std::size_t size() const { return live_; }
    std::uint32_t find(std::uint64_t key) const {
        const auto &b=entries_[probe(key)];
        return b.state==Bucket::Live ? b.slot : end;
    }
    void prepare() {
        if(occupied_ < entries_.size()-entries_.size()/4) return;
        KeyIndex fresh;
        fresh.entries_.resize(live_>=entries_.size()/2 ? product(entries_.size(),2) : entries_.size());
        for(const auto &b:entries_) if(b.state==Bucket::Live) fresh.insert(b.key,b.slot);
        *this=std::move(fresh);
    }
    // Caller has already checked uniqueness and reserved capacity.
    void insert(std::uint64_t key,std::uint32_t slot) {
        auto i=start(key);
        while(entries_[i].state==Bucket::Live) i=next(i);
        if(entries_[i].state==Bucket::Empty) ++occupied_;
        entries_[i]={key,slot,Bucket::Live}; ++live_;
    }
    void erase(std::uint64_t key) { entries_[probe(key)].state=Bucket::Deleted; --live_; }
};

inline std::pair<std::uint64_t,std::uint64_t> parent_identity(const std::filesystem::path &path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("cannot open slot sidecar parent");
    std::array<std::uint8_t,32768> buffer;
    std::uint64_t size=0, crc=0;
    while(in.read(reinterpret_cast<char *>(buffer.data()),buffer.size()) || in.gcount()) {
        auto n=static_cast<std::size_t>(in.gcount());
        crc=io::crc64(std::span(buffer).first(n),crc); size+=n;
    }
    if(!in.eof() || in.bad()) throw std::runtime_error("slot sidecar parent read failed");
    return {size,crc};
}
} // namespace slot_detail

// Handles belong to one pool and its current transaction. A rolled-back trial
// restores its entry handle namespace; discard all handles acquired in it.
// Domain keys travel between ranks. Slot numbers and generations remain local.
template<class T,std::size_t C=1,class Layout=SoA,std::size_t FixedPageCount=256>
class PagedSlotField {
    static_assert(C && C<=UINT32_MAX && FixedPageCount && FixedPageCount<=UINT32_MAX);
    static constexpr auto end=slot_detail::end;
    static constexpr std::uint64_t checkpoint_magic=UINT64_C(0x31544f4c53424453);
    static constexpr std::uint64_t packet_magic=UINT64_C(0x3147494d53424453);
    using Writer=checkpoint_detail::Writer;
    using Reader=checkpoint_detail::Reader;
    using Bytes=checkpoint_detail::Bytes;
    struct Meta {
        std::uint64_t key=0;
        std::uint32_t generation=1, next=end;
        bool live=false,valid=false;
    };
    struct alignas(64) Page {
        alignas(64) std::array<Meta,FixedPageCount> meta{};
        AlignedVector<T> values=AlignedVector<T>(Layout::size(FixedPageCount,C));
    };
    struct Store {
        std::shared_ptr<slot_detail::KeyIndex> index=std::make_shared<slot_detail::KeyIndex>();
        std::vector<std::shared_ptr<Page>> pages;
        std::uint32_t count=0,free=end;
    };
    std::uint64_t schema_;
    std::uint32_t version_;
    Store state_;
    std::optional<Store> saved_;
    const Page &page(std::uint32_t i) const { return *state_.pages[i/FixedPageCount]; }
    Page &edit(std::uint32_t i) {
        auto &p=state_.pages[i/FixedPageCount];
        if(!p.unique()) p=std::make_shared<Page>(*p);
        return *p;
    }
    void edit_index() {
        if(!state_.index.unique()) state_.index=std::make_shared<slot_detail::KeyIndex>(*state_.index);
    }
    const Meta &meta(std::uint32_t slot) const { return page(slot).meta[slot%FixedPageCount]; }
    const Meta &checked(SlotHandle h) const {
        if(h.slot>=state_.count || !meta(h.slot).live || meta(h.slot).generation!=h.generation)
            throw std::out_of_range("stale slot handle");
        return meta(h.slot);
    }
    static std::size_t offset(std::uint32_t slot,std::size_t component) {
        return Layout::offset(slot%FixedPageCount,component,FixedPageCount,C);
    }
    void schema(Writer &w) const {
        w.integer(schema_); w.integer(version_); w.integer<std::uint32_t>(static_cast<unsigned>(scalar_type<T>()));
        w.integer<std::uint32_t>(C);
    }
    void check_schema(Reader &r) const {
        if(r.integer<std::uint64_t>()!=schema_ || r.integer<std::uint32_t>()!=version_ ||
           r.integer<std::uint32_t>()!=static_cast<unsigned>(scalar_type<T>()) || r.integer<std::uint32_t>()!=C)
            throw std::invalid_argument("slot schema mismatch");
    }
    void payload(Writer &w,std::uint32_t i) const {
        w.integer<std::uint8_t>(meta(i).valid);
        for(std::size_t c=0;c<C;++c) w.scalar(page(i).values[offset(i,c)]);
    }
    static bool flag(Reader &r) {
        auto v=r.integer<std::uint8_t>();
        if(v>1) throw std::runtime_error("invalid slot flag");
        return v==1;
    }
    static void read_payload(Reader &r,Page &p,std::uint32_t i) {
        p.meta[i%FixedPageCount].valid=flag(r);
        for(std::size_t c=0;c<C;++c) {
            auto v=r.scalar<T>();
            if constexpr(std::is_floating_point_v<T>) if(!std::isfinite(v)) throw std::runtime_error("nonfinite slot value");
            p.values[offset(i,c)]=v;
        }
    }
    void append_page(std::uint32_t slot) {
        if(slot%FixedPageCount==0) state_.pages.push_back(std::make_shared<Page>());
    }
    Bytes record() const {
        if(in_trial()) throw std::logic_error("cannot checkpoint an active slot trial");
        Writer w; schema(w); w.integer(state_.count); w.integer(state_.free);
        for(std::uint32_t i=0;i<state_.count;++i) {
            const auto &m=meta(i); w.integer(m.generation); w.integer<std::uint8_t>(m.live);
            if(m.live) { w.integer(m.key); payload(w,i); }
            else w.integer(m.next);
        }
        return std::move(w.bytes);
    }
    void load_record(const Bytes &bytes) {
        Reader r{bytes}; check_schema(r);
        auto count=r.integer<std::uint32_t>(), head=r.integer<std::uint32_t>();
        if(count>(bytes.size()-r.pos)/9) throw std::runtime_error("truncated slot metadata");
        PagedSlotField staged(schema_,version_);
        auto &s=staged.state_; s.free=head;
        std::size_t reusable=0;
        for(std::uint32_t i=0;i<count;++i) {
            staged.append_page(i); ++s.count;
            auto &p=staged.edit(i); auto &m=p.meta[i%FixedPageCount];
            m.generation=r.integer<std::uint32_t>(); m.live=flag(r);
            if(m.live) {
                m.key=r.integer<std::uint64_t>();
                if(!m.generation || s.index->find(m.key)!=end) throw std::runtime_error("invalid or duplicate live slot");
                read_payload(r,p,i); s.index->prepare(); s.index->insert(m.key,i);
            } else {
                m.next=r.integer<std::uint32_t>();
                if(m.generation) ++reusable;
                else if(m.next!=end) throw std::runtime_error("invalid exhausted slot link");
            }
        }
        if(r.pos!=bytes.size()) throw std::runtime_error("slot record trailing bytes");
        for(auto i=head;i!=end;i=staged.meta(i).next) {
            if(i>=count || staged.meta(i).live || !staged.meta(i).generation || !reusable)
                throw std::runtime_error("invalid slot free list");
            --reusable;
        }
        if(reusable) throw std::runtime_error("unreachable free slot");
        state_=std::move(s);
    }
    using Parent=std::pair<std::uint64_t,std::uint64_t>;
    void save_file(const std::filesystem::path &path,std::optional<Parent> parent) const {
        namespace n=lifecycle::native;
        auto data=record();
        n::atomic_stream(path,[&](auto &out) {
            n::write(out,n::pack(checkpoint_magic));
            if(parent) n::record(out,1,n::pack(*parent));
            n::record(out,2,data);
        });
    }
    void restore_file(const std::filesystem::path &path,std::optional<Parent> parent,std::uint64_t budget) {
        namespace n=lifecycle::native;
        if(in_trial()) throw std::logic_error("restore during slot trial");
        std::ifstream in(path,std::ios::binary); std::array<std::uint8_t,8> magic;
        n::read(in,magic);
        if(Reader{magic}.integer<std::uint64_t>()!=checkpoint_magic) throw std::runtime_error("slot checkpoint magic");
        if(parent && n::unpack<Parent>(n::record(in,1,16))!=*parent) throw std::runtime_error("slot sidecar parent mismatch");
        auto data=n::record(in,2,budget); n::end(in); load_record(data);
    }
  public:
    PagedSlotField(std::uint64_t schema_id,std::uint32_t version):schema_(schema_id),version_(version) {
        if(!schema_id || !version) throw std::invalid_argument("invalid slot schema identity");
    }
    std::size_t size() const { return state_.index->size(); }
    bool in_trial() const { return saved_.has_value(); }
    void begin_trial() {
        if(in_trial()) throw std::logic_error("nested slot trial");
        saved_=state_;
    }
    void commit() { if(!in_trial()) throw std::logic_error("no slot trial"); saved_.reset(); }
    void rollback() {
        if(!in_trial()) throw std::logic_error("no slot trial");
        state_=std::move(*saved_); saved_.reset();
    }
    std::optional<SlotHandle> find(std::uint64_t key) const {
        auto i=state_.index->find(key);
        if(i==end) return {};
        return SlotHandle{i,meta(i).generation};
    }
    SlotHandle allocate(std::uint64_t domain_key) {
        if(find(domain_key)) throw std::invalid_argument("duplicate live slot key");
        auto i=state_.free;
        if(i==end && state_.count==end) throw std::length_error("slot namespace exhausted");
        edit_index(); state_.index->prepare();
        if(i==end) { i=state_.count; append_page(i); }
        auto &p=edit(i); auto &m=p.meta[i%FixedPageCount];
        if(i==state_.count) ++state_.count;
        else state_.free=m.next;
        m.next=end; m.live=true; m.valid=false; m.key=domain_key;
        for(std::size_t c=0;c<C;++c) p.values[offset(i,c)]=T{};
        state_.index->insert(domain_key,i);
        return {i,m.generation};
    }
    void erase(SlotHandle h) {
        checked(h); edit_index(); auto &m=edit(h.slot).meta[h.slot%FixedPageCount];
        state_.index->erase(m.key); m.live=false; m.valid=false;
        if(++m.generation) { m.next=state_.free; state_.free=h.slot; }
    }
    std::uint64_t domain_key(SlotHandle h) const { return checked(h).key; }
    bool valid(SlotHandle h) const { return checked(h).valid; }
    void set_valid(SlotHandle h,bool value) { checked(h); edit(h.slot).meta[h.slot%FixedPageCount].valid=value; }
    T get(SlotHandle h,std::size_t c=0) const {
        checked(h); if(c>=C) throw std::out_of_range("slot component");
        return page(h.slot).values[offset(h.slot,c)];
    }
    void set(SlotHandle h,std::size_t c,T v) {
        checked(h); if(c>=C) throw std::out_of_range("slot component");
        if constexpr(std::is_floating_point_v<T>) if(!std::isfinite(v)) throw std::invalid_argument("nonfinite slot value");
        auto &p=edit(h.slot); p.values[offset(h.slot,c)]=v; p.meta[h.slot%FixedPageCount].valid=true;
    }
    void save(const std::filesystem::path &p) const { save_file(p,{}); }
    void restore(const std::filesystem::path &p,std::uint64_t budget=lifecycle::native::default_limit) {
        restore_file(p,{},budget);
    }
    void save_sidecar(const std::filesystem::path &p,const std::filesystem::path &parent) const {
        if(std::filesystem::weakly_canonical(p)==std::filesystem::weakly_canonical(parent))
            throw std::invalid_argument("slot sidecar would overwrite parent");
        save_file(p,slot_detail::parent_identity(parent));
    }
    void restore_sidecar(const std::filesystem::path &p,const std::filesystem::path &parent,
                         std::uint64_t budget=lifecycle::native::default_limit) {
        restore_file(p,slot_detail::parent_identity(parent),budget);
    }
    template<class Predicate> std::vector<std::byte> export_migration_packets(Predicate &&select,bool retire=false) {
        if(retire && !in_trial()) throw std::logic_error("retiring exports require a slot trial");
        std::vector<std::pair<std::uint64_t,SlotHandle>> selected;
        for(std::uint32_t i=0;i<state_.count;++i)
            if(meta(i).live && select(meta(i).key)) selected.emplace_back(meta(i).key,SlotHandle{i,meta(i).generation});
        std::ranges::sort(selected,{},&decltype(selected)::value_type::first);
        Writer w; w.integer(packet_magic); schema(w); w.integer<std::uint64_t>(selected.size());
        for(auto [key,h]:selected) {
            auto start=w.bytes.size();
            w.integer(key); schema(w); w.integer<std::uint64_t>(1+C*sizeof(T)); payload(w,h.slot);
            w.integer(checkpoint_detail::crc(std::span(w.bytes).subspan(start)));
        }
        w.integer(checkpoint_detail::crc(w.bytes));
        auto bytes=std::as_bytes(std::span(w.bytes));
        std::vector<std::byte> out(bytes.begin(),bytes.end());
        if(retire && !selected.empty()) {
            edit_index(); for(auto [key,h]:selected) (void)edit(h.slot);
            for(auto [key,h]:selected) erase(h);
        }
        return out;
    }
    void import_migration_packets(std::span<const std::byte> packet) {
        auto bytes=std::span(reinterpret_cast<const std::uint8_t *>(packet.data()),packet.size());
        if(bytes.size()<4 || Reader{bytes.last(4)}.integer<std::uint32_t>()!=checkpoint_detail::crc(bytes.first(bytes.size()-4)))
            throw std::runtime_error("migration envelope CRC");
        Reader r{bytes.first(bytes.size()-4)};
        if(r.integer<std::uint64_t>()!=packet_magic) throw std::runtime_error("slot migration magic");
        check_schema(r); auto count=r.integer<std::uint64_t>();
        constexpr std::size_t row_bytes=8+20+8+1+C*sizeof(T)+4;
        if(count>(r.bytes.size()-r.pos)/row_bytes || count*row_bytes!=r.bytes.size()-r.pos)
            throw std::runtime_error("migration extent");
        Store before=state_;
        try {
            for(std::uint64_t j=0;j<count;++j) {
                auto start=r.pos; auto key=r.integer<std::uint64_t>(); check_schema(r);
                if(r.integer<std::uint64_t>()!=1+C*sizeof(T)) throw std::runtime_error("slot payload length");
                auto h=allocate(key); read_payload(r,edit(h.slot),h.slot);
                auto crc=checkpoint_detail::crc(r.bytes.subspan(start,r.pos-start));
                if(r.integer<std::uint32_t>()!=crc) throw std::runtime_error("slot packet CRC");
            }
        } catch(...) { state_=std::move(before); throw; }
    }
};
} // namespace sdb
