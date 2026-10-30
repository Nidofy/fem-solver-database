#pragma once
#include "contact.hpp"
#include "cuda_memory.hpp"

namespace sdb::cuda {
struct CandidateView {
    ContactKey *entries;
    unsigned long long *count;
    unsigned long long *dropped;
    unsigned long long capacity;
#ifdef __CUDACC__
    __device__ bool append(ContactKey key) const {
        auto slot = atomicAdd(count, 0ull);
        for (;;) {
            if (slot >= capacity) {
                atomicAdd(dropped, 1ull);
                return false;
            }
            auto previous = atomicCAS(count, slot, slot + 1);
            if (previous == slot) {
                entries[slot] = key;
                return true;
            }
            slot = previous;
        }
    }
#endif
};
// Append kernels and download use the owner stream. Reset and download are phase operations.
class Candidates {
    std::shared_ptr<Context> context_;
    DeviceBuffer<ContactKey> data_;
    DeviceBuffer<unsigned long long> counters_;
    PinnedBuffer<unsigned long long> host_counters_{2};
    PinnedBuffer<ContactKey> host_data_;

  public:
    Candidates(std::shared_ptr<Context> context, std::size_t capacity)
        : context_(std::move(context)), data_(context_, capacity), counters_(context_, 2),
          host_data_(capacity) {
        reset();
    }
    ~Candidates() {
        try {
            context_->wait();
        } catch (...) {
            ++cleanup_errors;
        }
    }
    void reset() {
        context_->activate();
        check(cudaMemsetAsync(counters_.data(), 0, 2 * sizeof(unsigned long long), context_->stream()),
              "candidate reset");
    }
    CandidateView view() { return {data_.data(), counters_.data(), counters_.data() + 1, data_.size()}; }
    std::vector<ContactKey> download(std::size_t &dropped) {
        context_->activate();
        check(cudaMemcpyAsync(host_counters_.data(), counters_.data(), 2 * sizeof(unsigned long long),
                              cudaMemcpyDeviceToHost, context_->stream()),
              "candidate counts download");
        context_->wait();
        auto n = host_counters_.data()[0];
        dropped = host_counters_.data()[1];
        if (n > data_.size())
            throw std::runtime_error("corrupt candidate count");
        if (n)
            check(cudaMemcpyAsync(host_data_.data(), data_.data(), n * sizeof(ContactKey),
                                  cudaMemcpyDeviceToHost, context_->stream()),
                  "candidate download");
        context_->wait();
        if (!n)
            return {};
        return {host_data_.data(), host_data_.data() + n};
    }
};
template <class T> struct ManifoldView {
    ContactHistory<T> *histories;
    std::size_t count;
    // Each history is owned by one thread/manifold. Identity and generation are immutable.
    SDB_HD ContactHistory<T> &operator[](std::size_t i) const { return histories[i]; }
};
template <class T> class Manifolds {
    std::shared_ptr<Context> context_;
    std::vector<ContactHistory<T>> original_;
    DeviceBuffer<ContactHistory<T>> device_;
    PinnedBuffer<ContactHistory<T>> host_;

  public:
    Manifolds(std::shared_ptr<Context> context, const ContactDB<T> &source)
        : context_(std::move(context)), original_(source.snapshot()), device_(context_, original_.size()),
          host_(original_.size()) {
        std::copy(original_.begin(), original_.end(), host_.data());
        if (!original_.empty())
            check(cudaMemcpyAsync(device_.data(), host_.data(), original_.size() * sizeof(ContactHistory<T>),
                                  cudaMemcpyHostToDevice, context_->stream()),
                  "manifold upload");
    }
    ~Manifolds() {
        try {
            context_->wait();
        } catch (...) {
            ++cleanup_errors;
        }
    }
    ManifoldView<T> view() { return {device_.data(), original_.size()}; }
    void download(ContactDB<T> &target) {
        if (target.in_trial())
            throw std::logic_error("GPU manifold restore during trial");
        if (!original_.empty())
            check(cudaMemcpyAsync(host_.data(), device_.data(), original_.size() * sizeof(ContactHistory<T>),
                                  cudaMemcpyDeviceToHost, context_->stream()),
                  "manifold download");
        context_->wait();
        auto next = original_;
        for (std::size_t i = 0; i < next.size(); ++i) {
            const auto &h = host_.data()[i];
            if (h.key != original_[i].key || h.generation != original_[i].generation ||
                h.active != original_[i].active)
                throw std::runtime_error("GPU modified manifold identity");
            next[i] = h;
        }
        target.restore(std::move(next));
    }
};
} // namespace sdb::cuda
