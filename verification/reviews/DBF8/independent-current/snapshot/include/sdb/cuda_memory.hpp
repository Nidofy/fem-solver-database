#pragma once
#ifndef SDB_HAS_CUDA
#error "cuda_memory.hpp requires SDB_CUDA and CUDA Toolkit"
#endif
#include "field.hpp"
#include <cuda_runtime.h>
#include <cstring>

namespace sdb::cuda {
inline void check(cudaError_t error, const char *operation) {
    if (error != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}
// Destructors cannot throw. Surface teardown errors through this counter and explicit close/wait.
inline std::atomic<std::size_t> cleanup_errors{0};
inline void cleanup(cudaError_t error) noexcept {
    if (error != cudaSuccess)
        ++cleanup_errors;
}
class Context {
    int device_;
    cudaStream_t stream_{};
    cudaMemPool_t pool_{};

  public:
    MemoryStats memory;
    explicit Context(int device = 0) : device_(device) {
        check(cudaSetDevice(device_), "cudaSetDevice");
        int pools = 0;
        check(cudaDeviceGetAttribute(&pools, cudaDevAttrMemoryPoolsSupported, device_),
              "memory pool capability");
        if (!pools)
            throw std::runtime_error("device does not support stream-ordered memory pools");
        check(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking), "stream create");
        try {
            cudaMemPoolProps props{};
            props.allocType = cudaMemAllocationTypePinned;
            props.handleTypes = cudaMemHandleTypeNone;
            props.location.type = cudaMemLocationTypeDevice;
            props.location.id = device_;
            check(cudaMemPoolCreate(&pool_, &props), "pool create");
            std::uint64_t threshold = std::numeric_limits<std::uint64_t>::max();
            check(cudaMemPoolSetAttribute(pool_, cudaMemPoolAttrReleaseThreshold, &threshold),
                  "pool retain capacity");
        } catch (...) {
            if (pool_)
                cleanup(cudaMemPoolDestroy(pool_));
            cleanup(cudaStreamDestroy(stream_));
            throw;
        }
    }
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    ~Context() {
        cleanup(cudaSetDevice(device_));
        cleanup(cudaStreamSynchronize(stream_));
        cleanup(cudaMemPoolDestroy(pool_));
        cleanup(cudaStreamDestroy(stream_));
    }
    void activate() const { check(cudaSetDevice(device_), "activate device"); }
    cudaStream_t stream() const noexcept { return stream_; }
    cudaMemPool_t pool() const noexcept { return pool_; }
    void wait() const {
        activate();
        check(cudaStreamSynchronize(stream_), "stream wait");
    }
    std::uint64_t reserved_peak() const {
        std::uint64_t n = 0;
        activate();
        check(cudaMemPoolGetAttribute(pool_, cudaMemPoolAttrReservedMemHigh, &n), "pool peak");
        return n;
    }
};
class Event {
    std::shared_ptr<Context> context_;
    cudaEvent_t event_{};
    bool recorded_ = false;

  public:
    explicit Event(std::shared_ptr<Context> context, bool timing = false) : context_(std::move(context)) {
        context_->activate();
        check(cudaEventCreateWithFlags(&event_, timing ? cudaEventDefault : cudaEventDisableTiming),
              "event create");
    }
    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;
    ~Event() { cleanup(cudaEventDestroy(event_)); }
    void record() {
        context_->activate();
        check(cudaEventRecord(event_, context_->stream()), "event record");
        recorded_ = true;
    }
    void wait() const {
        if (!recorded_)
            throw std::logic_error("event not recorded");
        context_->activate();
        check(cudaEventSynchronize(event_), "event wait");
    }
    bool ready() const {
        if (!recorded_)
            return false;
        context_->activate();
        auto status = cudaEventQuery(event_);
        if (status == cudaErrorNotReady)
            return false;
        check(status, "event query");
        return true;
    }
    void wait_on(const std::shared_ptr<Context> &other) const {
        if (!recorded_)
            throw std::logic_error("event not recorded");
        other->activate();
        check(cudaStreamWaitEvent(other->stream(), event_, 0), "stream event dependency");
    }
    float elapsed_since(const Event &start) const {
        if (!recorded_ || !start.recorded_)
            throw std::logic_error("timing event not recorded");
        float ms = 0;
        check(cudaEventElapsedTime(&ms, start.event_, event_), "event elapsed");
        return ms;
    }
};
template <class T> class DeviceBuffer {
    std::shared_ptr<Context> context_;
    T *pointer_ = nullptr;
    std::size_t count_ = 0;

  public:
    DeviceBuffer(std::shared_ptr<Context> context, std::size_t count)
        : context_(std::move(context)), count_(count) {
        const auto bytes = product(count, sizeof(T));
        context_->activate();
        if (bytes) {
            check(cudaMallocFromPoolAsync(reinterpret_cast<void **>(&pointer_), bytes, context_->pool(),
                                          context_->stream()),
                  "pool allocate");
            context_->memory.acquired(bytes);
        }
    }
    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;
    ~DeviceBuffer() {
        if (pointer_) {
            try {
                context_->activate();
                cleanup(cudaFreeAsync(pointer_, context_->stream()));
            } catch (...) {
                ++cleanup_errors;
            }
            context_->memory.released(count_ * sizeof(T));
        }
    }
    T *data() noexcept { return pointer_; }
    const T *data() const noexcept { return pointer_; }
    std::size_t size() const noexcept { return count_; }
};
inline MemoryStats pinned_memory;
template <class T> class PinnedBuffer {
    T *pointer_ = nullptr;
    std::size_t count_;

  public:
    explicit PinnedBuffer(std::size_t count) : count_(count) {
        const auto bytes = product(count, sizeof(T));
        if (bytes) {
            check(cudaHostAlloc(reinterpret_cast<void **>(&pointer_), bytes, cudaHostAllocPortable),
                  "pinned allocate");
            pinned_memory.acquired(bytes);
            std::fill_n(pointer_, count, T{});
        }
    }
    PinnedBuffer(const PinnedBuffer &) = delete;
    PinnedBuffer &operator=(const PinnedBuffer &) = delete;
    ~PinnedBuffer() {
        if (pointer_) {
            cleanup(cudaFreeHost(pointer_));
            pinned_memory.released(count_ * sizeof(T));
        }
    }
    T *data() noexcept { return pointer_; }
    const T *data() const noexcept { return pointer_; }
};
enum class Residency { HostLatest, Synced, DeviceLatest, Uploading, Downloading };
// Owned staging guarantees async transfers never borrow caller memory.
// All device users must enqueue on context()->stream(). Foreign stream users must
// establish event dependencies back to the owner stream before destruction/reuse.
template <class T, std::size_t C = 1, class Layout = SoA> class Mirror {
    std::shared_ptr<Context> context_;
    std::size_t count_, storage_;
    DeviceBuffer<T> device_;
    PinnedBuffer<T> host_;
    Event completion_;
    Residency residency_ = Residency::HostLatest;
    std::uint64_t version_ = 0;

  public:
    Mirror(std::shared_ptr<Context> context, std::size_t count)
        : context_(std::move(context)), count_(count), storage_(Layout::size(count, C)),
          device_(context_, storage_), host_(storage_), completion_(context_) {}
    ~Mirror() {
        try {
            context_->wait();
        } catch (...) {
            ++cleanup_errors;
        }
    }
    Residency residency() const noexcept { return residency_; }
    std::uint64_t version() const noexcept { return version_; }
    const auto &context() const noexcept { return context_; }
    View<const T, C, Layout> host_read() const {
        if (residency_ != Residency::HostLatest && residency_ != Residency::Synced)
            throw std::logic_error("host data stale/pending");
        return {host_.data(), count_};
    }
    View<T, C, Layout> host_write() {
        host_read();
        residency_ = Residency::HostLatest;
        ++version_;
        return {host_.data(), count_};
    }
    View<const T, C, Layout> device_read() const {
        if (residency_ != Residency::Synced && residency_ != Residency::DeviceLatest)
            throw std::logic_error("device data stale/pending");
        return {device_.data(), count_};
    }
    View<T, C, Layout> device_write() {
        device_read();
        residency_ = Residency::DeviceLatest;
        ++version_;
        return {device_.data(), count_};
    }
    void upload() {
        if (residency_ != Residency::HostLatest && residency_ != Residency::Synced)
            throw std::logic_error("invalid upload");
        context_->activate();
        if (storage_)
            check(cudaMemcpyAsync(device_.data(), host_.data(), storage_ * sizeof(T), cudaMemcpyHostToDevice,
                                  context_->stream()),
                  "H2D");
        residency_ = Residency::Uploading;
        completion_.record();
    }
    void download() {
        if (residency_ != Residency::DeviceLatest && residency_ != Residency::Synced)
            throw std::logic_error("invalid download");
        context_->activate();
        if (storage_)
            check(cudaMemcpyAsync(host_.data(), device_.data(), storage_ * sizeof(T), cudaMemcpyDeviceToHost,
                                  context_->stream()),
                  "D2H");
        residency_ = Residency::Downloading;
        completion_.record();
    }
    void finish() {
        if (residency_ != Residency::Uploading && residency_ != Residency::Downloading)
            throw std::logic_error("no transfer pending");
        completion_.wait();
        residency_ = Residency::Synced;
    }
    void assign(View<const T, C, Layout> source) {
        if (source.count != count_)
            throw std::invalid_argument("mirror shape mismatch");
        auto dest = host_write();
        for (std::size_t c = 0; c < C; ++c)
            for (std::size_t i = 0; i < count_; ++i)
                dest(i, c) = source(i, c);
    }
    void copy_to(Field<T, C, Layout> &target) const {
        if (target.size() != count_)
            throw std::invalid_argument("mirror target shape");
        auto source = host_read();
        auto dest = target.write();
        for (std::size_t c = 0; c < C; ++c)
            for (std::size_t i = 0; i < count_; ++i)
                dest(i, c) = source(i, c);
    }
};
} // namespace sdb::cuda
