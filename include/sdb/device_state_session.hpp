#pragma once
#include "state_session.hpp"
#include <cstring>
#include <initializer_list>
#include <string_view>
#ifdef SDB_HAS_CUDA
#include "cuda_memory.hpp"
#endif

namespace sdb {
enum class MirrorResidency : std::uint8_t { HostLatest, DeviceLatest, Synced, Uploading, Downloading };
enum class DeviceCopy : std::uint8_t { Upload, Download, DeviceToDevice };
struct DeviceTransferStats {
    std::size_t h2d_bytes = 0, d2h_bytes = 0, d2d_bytes = 0;
};

// One ordered execution stream per backend. Submitted copies and kernels must
// be joined by wait(). Allocations return an owning handle even for zero bytes.
// A throwing submission must not keep borrowed pointers.
class DeviceStateBackend {
  public:
    struct Buffer {
        virtual ~Buffer() = default;
        virtual std::byte *data() noexcept = 0;
    };
    virtual ~DeviceStateBackend() = default;
    std::atomic<std::size_t> cleanup_errors{0};
    virtual std::unique_ptr<Buffer> allocate_device(std::size_t bytes) = 0;
    virtual std::unique_ptr<Buffer> allocate_host(std::size_t bytes) = 0;
    virtual void copy(DeviceCopy direction, Buffer &dst, Buffer &src, std::size_t bytes) = 0;
    virtual void wait() = 0;
    virtual bool host_accessible_device() const noexcept { return false; }
};

class HostDeviceSimulator final : public DeviceStateBackend {
    struct Storage final : Buffer {
        AlignedVector<std::byte> bytes;
        explicit Storage(std::size_t n) : bytes(n) {}
        std::byte *data() noexcept override { return bytes.data(); }
    };
  public:
    std::unique_ptr<Buffer> allocate_device(std::size_t n) override { return std::make_unique<Storage>(n); }
    std::unique_ptr<Buffer> allocate_host(std::size_t n) override { return std::make_unique<Storage>(n); }
    void copy(DeviceCopy,Buffer &dst,Buffer &src,std::size_t n) override {
        if (n) std::memcpy(dst.data(),src.data(),n);
    }
    void wait() override {}
    bool host_accessible_device() const noexcept override { return true; }
};

#ifdef SDB_HAS_CUDA
// DBF9 CUDA bridge: not locally compiled / not locally verified.
// The native dbf9_device_cuda CTest launches on the same context stream as these copies.
class CudaStateBackend final : public DeviceStateBackend {
    std::shared_ptr<cuda::Context> context_;
    template<class Storage> struct Allocation final : Buffer {
        Storage storage;
        template<class... Args> explicit Allocation(Args&&... args) : storage(std::forward<Args>(args)...) {}
        std::byte *data() noexcept override { return storage.data(); }
    };
  public:
    explicit CudaStateBackend(std::shared_ptr<cuda::Context> context) : context_(std::move(context)) {
        if (!context_) throw std::invalid_argument("null CUDA state context");
    }
    std::unique_ptr<Buffer> allocate_device(std::size_t n) override {
        return std::make_unique<Allocation<cuda::DeviceBuffer<std::byte>>>(context_,n);
    }
    std::unique_ptr<Buffer> allocate_host(std::size_t n) override {
        context_->activate();
        return std::make_unique<Allocation<cuda::PinnedBuffer<std::byte>>>(n);
    }
    void copy(DeviceCopy direction,Buffer &dst,Buffer &src,std::size_t n) override {
        context_->activate();
        auto kind = direction == DeviceCopy::Upload ? cudaMemcpyHostToDevice :
                    direction == DeviceCopy::Download ? cudaMemcpyDeviceToHost : cudaMemcpyDeviceToDevice;
        if (n) cuda::check(cudaMemcpyAsync(dst.data(),src.data(),n,kind,context_->stream()),"state copy");
    }
    void wait() override { context_->wait(); }
};
#endif

// Resolve a checked view at each joined phase boundary. raw() gives a kernel
// borrow valid only for that phase. CUDA work uses the backend context stream.
template<class F,bool Writable> class DeviceLeaseView {
    using T = std::conditional_t<Writable,typename F::value_type,const typename F::value_type>;
    using Mask = std::conditional_t<Writable,std::uint8_t,const std::uint8_t>;
    CheckedView<T,F::components,typename F::layout_type> values_;
    Mask *mask_;
    bool has_mask_, host_accessible_;
  public:
    DeviceLeaseView(View<T,F::components,typename F::layout_type> v,const std::shared_ptr<ViewLifetime> &life,
                    Mask *mask,bool has_mask,bool host_accessible)
        : values_(v,life),mask_(mask),has_mask_(has_mask),host_accessible_(host_accessible) {}
    auto raw() const { return values_.raw(); }
    Mask *raw_validity() const { values_.validate(); return mask_; }
    T &at(std::size_t row,std::size_t component=0) const {
        if (!host_accessible_) throw std::logic_error("device pointer requires a device kernel");
        return values_.at(row,component);
    }
    Mask &validity_at(std::size_t row,std::size_t component=0) const {
        const auto rows=values_.size();
        if (!has_mask_ || !host_accessible_) throw std::logic_error("validity mask inaccessible");
        if (row>=rows || component>=F::components) throw std::out_of_range("device field mask index");
        return mask_[row*F::components+component];
    }
};

// Owns its registry so a caller's pre-existing host borrows cannot bypass
// residency. All session operations are single-owner, joined phase boundaries.
class DeviceStateSession {
    using Buffer = DeviceStateBackend::Buffer;
    struct Entry {
        RegisteredFieldBase *field;
        std::size_t value_bytes, mask_bytes;
        MirrorResidency authority = MirrorResidency::HostLatest;
        std::unique_ptr<Buffer> device, alternate, staging;
        bool trial_written = false;
        std::shared_ptr<ViewLifetime> lifetime = std::make_shared<ViewLifetime>();
        Entry(RegisteredFieldBase &owner,std::size_t values,std::size_t mask)
            : field(&owner),value_bytes(values),mask_bytes(mask) {}
        std::size_t bytes() const { return sum(value_bytes,mask_bytes); }
        MirrorResidency residency() const { return trial_written ? MirrorResidency::DeviceLatest : authority; }
        bool pending() const { return authority==MirrorResidency::Uploading || authority==MirrorResidency::Downloading; }
        void invalidate() noexcept { ++lifetime->generation; }
    };
    std::shared_ptr<DeviceStateBackend> backend_;
    RegisteredState host_;
    std::vector<Entry> entries_;
    DeviceTransferStats stats_;
    bool trial_ = false;

    Entry &entry(std::string_view name) {
        for (auto &e:entries_) if (e.field->descriptor().name==name) return e;
        throw std::out_of_range("unknown device state field");
    }
    static void copy_bytes(std::byte *dst,const std::byte *src,std::size_t n) {
        if (n) std::memcpy(dst,src,n);
    }
    static void pack_host(Entry &e,Buffer &staging) {
        copy_bytes(staging.data(),e.field->value_storage().data(),e.value_bytes);
        if (e.mask_bytes) copy_bytes(staging.data()+e.value_bytes,
            reinterpret_cast<const std::byte*>(e.field->validity_->read().data),e.mask_bytes);
    }
    static void unpack_host(Entry &e,Buffer &staging) {
        auto values=e.field->writable_value_storage();
        auto *mask=e.mask_bytes ? reinterpret_cast<std::byte*>(e.field->validity_->write().data) : nullptr;
        copy_bytes(values.data(),staging.data(),e.value_bytes);
        if (mask) copy_bytes(mask,staging.data()+e.value_bytes,e.mask_bytes);
    }
    void start(Entry &e,bool upload) {
        const auto source=upload ? MirrorResidency::HostLatest : MirrorResidency::DeviceLatest;
        if (trial_ || e.authority!=source) throw std::logic_error("invalid field transfer");
        auto staging=backend_->allocate_host(e.bytes());
        if (upload) {
            pack_host(e,*staging);
            if (!e.device) e.device=backend_->allocate_device(e.bytes());
            backend_->copy(DeviceCopy::Upload,*e.device,*staging,e.bytes());
        } else backend_->copy(DeviceCopy::Download,*staging,*e.device,e.bytes());
        e.staging=std::move(staging);
        e.authority=upload ? MirrorResidency::Uploading : MirrorResidency::Downloading;
        e.invalidate();
    }
    void finish(Entry &e) {
        if (!e.pending()) throw std::logic_error("no pending field transfer");
        backend_->wait();
        auto &bytes=e.authority==MirrorResidency::Uploading ? stats_.h2d_bytes : stats_.d2h_bytes;
        if (e.authority==MirrorResidency::Downloading) unpack_host(e,*e.staging);
        bytes=state_counter_add(bytes,e.bytes());
        e.authority=MirrorResidency::Synced; e.staging.reset(); e.invalidate();
    }
    void sync(std::span<const std::string_view> names,bool upload) {
        if (trial_) throw std::logic_error("sync during device trial");
        // Resolve all names and reject busy fields before starting any transfer.
        std::vector<Entry*> selection; selection.reserve(names.size());
        for (auto name:names) {
            auto &e=entry(name);
            if (e.pending()) throw std::logic_error("field transfer pending");
            selection.push_back(&e);
        }
        const auto source=upload ? MirrorResidency::HostLatest : MirrorResidency::DeviceLatest;
        for (auto *e:selection) if (e->authority==source) { start(*e,upload); finish(*e); }
    }
    template<class F,bool Write> DeviceLeaseView<F,Write> view(std::string_view name,bool device) {
        using T=std::conditional_t<Write,typename F::value_type,const typename F::value_type>;
        using Mask=std::conditional_t<Write,std::uint8_t,const std::uint8_t>;
        auto &e=entry(name);
        if (e.field->type()!=typeid(F)) throw std::invalid_argument("device field type/layout mismatch");
        const auto authority=e.residency();
        if (authority!=MirrorResidency::Synced && authority!=(device ? MirrorResidency::DeviceLatest : MirrorResidency::HostLatest))
            throw std::logic_error("field data stale or transfer pending");
        T *values;
        Mask *mask=nullptr;
        if (device) {
            if constexpr (Write) {
                if (trial_ && !e.trial_written) {
                    if (!e.alternate) e.alternate=backend_->allocate_device(e.bytes());
                    backend_->copy(DeviceCopy::DeviceToDevice,*e.alternate,*e.device,e.bytes());
                    e.trial_written=true;
                    stats_.d2d_bytes=state_counter_add(stats_.d2d_bytes,e.bytes());
                } else if (!trial_) e.authority=MirrorResidency::DeviceLatest;
            }
            auto *p=(e.trial_written ? e.alternate : e.device)->data();
            values=reinterpret_cast<T*>(p);
            if (e.mask_bytes) mask=reinterpret_cast<Mask*>(p+e.value_bytes);
        } else {
            if constexpr (Write) {
                if (trial_) throw std::logic_error("host write during device trial");
                values=reinterpret_cast<T*>(e.field->writable_value_storage().data());
                if (e.mask_bytes) mask=e.field->validity_->write().data;
                e.authority=MirrorResidency::HostLatest;
            } else {
                values=reinterpret_cast<T*>(e.field->value_storage().data());
                if (e.mask_bytes) mask=e.field->validity_->read().data;
            }
        }
        if constexpr (Write) e.invalidate();
        return {{values,e.field->count()},e.lifetime,mask,e.field->validity_.has_value(),
                !device || backend_->host_accessible_device()};
    }
    void end_trial(bool commit) {
        if (!trial_) throw std::logic_error("no device trial");
        backend_->wait(); // failed join retains the complete rollback generation
        for (auto &e:entries_) {
            if (commit && e.trial_written) { e.device.swap(e.alternate); e.authority=MirrorResidency::DeviceLatest; }
            e.trial_written=false; e.invalidate();
        }
        trial_=false;
    }
    struct AdoptSnapshot {};
    DeviceStateSession(AdoptSnapshot,RegisteredState state,std::shared_ptr<DeviceStateBackend> backend)
        : backend_(std::move(backend)),host_(std::move(state)) {
        if (!backend_ || host_.in_trial()) throw std::invalid_argument("device session needs a committed registry and backend");
        host_.validate();
        entries_.reserve(host_.entries().size());
        for (auto field:host_.entries()) {
            auto &owned=host_.find(field->descriptor().name);
            entries_.emplace_back(owned,owned.value_storage().size(),owned.validity_ ? owned.validity_->bytes() : 0);
        }
    }
  public:
    explicit DeviceStateSession(const RegisteredState &state,std::shared_ptr<DeviceStateBackend> backend)
        : DeviceStateSession(AdoptSnapshot{},RegisteredState(state),std::move(backend)) {}
    explicit DeviceStateSession(const StateSession &session,std::shared_ptr<DeviceStateBackend> backend)
        : DeviceStateSession(AdoptSnapshot{},session.snapshot(),std::move(backend)) {}
    DeviceStateSession(const DeviceStateSession&)=delete;
    DeviceStateSession &operator=(const DeviceStateSession&)=delete;
    ~DeviceStateSession() { try { backend_->wait(); } catch (...) { ++backend_->cleanup_errors; } }

    MirrorResidency residency(std::string_view name) { return entry(name).residency(); }
    DeviceTransferStats transfers() const noexcept { return stats_; }
    bool in_trial() const noexcept { return trial_; }
    template<class F> auto host_read(std::string_view name) { return view<F,false>(name,false); }
    template<class F> auto host_write(std::string_view name) { return view<F,true>(name,false); }
    template<class F> auto device_read(std::string_view name) { return view<F,false>(name,true); }
    template<class F> auto device_write(std::string_view name) { return view<F,true>(name,true); }
    void begin_upload(std::string_view name) { start(entry(name),true); }
    void begin_download(std::string_view name) { start(entry(name),false); }
    void finish_transfer(std::string_view name) { finish(entry(name)); }
    void sync_to_device(std::span<const std::string_view> names) { sync(names,true); }
    void sync_to_host(std::span<const std::string_view> names) { sync(names,false); }
    void sync_to_device(std::initializer_list<std::string_view> names) { sync_to_device(std::span(names)); }
    void sync_to_host(std::initializer_list<std::string_view> names) { sync_to_host(std::span(names)); }
    void begin_trial_device() {
        if (trial_) throw std::logic_error("device trial already active");
        for (const auto &e:entries_) if (e.pending()) throw std::logic_error("field transfer pending");
        for (auto &e:entries_) e.invalidate();
        trial_=true;
    }
    void commit_device() { end_trial(true); }
    void rollback_device() { end_trial(false); }
    RegisteredState snapshot() const {
        if (trial_) throw std::logic_error("cannot snapshot device trial");
        for (const auto &e:entries_) if (e.authority!=MirrorResidency::HostLatest && e.authority!=MirrorResidency::Synced)
            throw std::logic_error("snapshot requires current host fields");
        host_.validate();
        return host_;
    }
};
} // namespace sdb
