#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace sdb {
inline std::size_t product(std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b)
        throw std::length_error("capacity overflow");
    return a * b;
}
inline std::size_t sum(std::size_t a, std::size_t b) {
    if (a > std::numeric_limits<std::size_t>::max() - b)
        throw std::length_error("capacity overflow");
    return a + b;
}
struct MemoryStats {
    std::atomic<std::size_t> live{0}, peak{0}, allocations{0};
    void acquired(std::size_t n) noexcept {
        const auto v = live.fetch_add(n) + n;
        auto p = peak.load();
        while (p < v && !peak.compare_exchange_weak(p, v)) {
        }
        ++allocations;
    }
    void released(std::size_t n) noexcept { live.fetch_sub(n); }
};
inline MemoryStats host_memory;
template <class T, std::size_t Alignment = 64> struct AlignedAllocator {
    using value_type = T;
    using is_always_equal = std::true_type;
    template <class U> struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
    AlignedAllocator() = default;
    template <class U> AlignedAllocator(const AlignedAllocator<U, Alignment> &) noexcept {}
    T *allocate(std::size_t n) {
        constexpr auto a = std::max(Alignment, alignof(T));
        static_assert((a & (a - 1)) == 0);
        auto bytes = product(n, sizeof(T));
        auto *p = static_cast<T *>(::operator new(bytes, std::align_val_t(a)));
        host_memory.acquired(bytes);
        return p;
    }
    void deallocate(T *p, std::size_t n) noexcept {
        ::operator delete(p, std::align_val_t(std::max(Alignment, alignof(T))));
        host_memory.released(n * sizeof(T));
    }
    template <class U> bool operator==(const AlignedAllocator<U, Alignment> &) const noexcept { return true; }
};
template <class T> using AlignedVector = std::vector<T, AlignedAllocator<T>>;

// Single-owner monotonic pool. reset() is a phase boundary invalidating all allocations.
class Arena {
    AlignedVector<std::byte> storage_;
    std::size_t used_ = 0, peak_ = 0, allocations_ = 0;

  public:
    explicit Arena(std::size_t capacity) : storage_(capacity) {}
    void *allocate(std::size_t bytes, std::size_t alignment = 64) {
        if (!alignment || (alignment & (alignment - 1)) || alignment > 64)
            throw std::invalid_argument("arena alignment must be power of two <=64");
        const auto start = sum(used_, alignment - 1) & ~(alignment - 1);
        const auto end = sum(start, bytes);
        if (end > storage_.size())
            throw std::bad_alloc();
        used_ = end;
        peak_ = std::max(peak_, used_);
        ++allocations_;
        if (storage_.empty())
            return nullptr;
        return storage_.data() + start;
    }
    template <class T> T *allocate(std::size_t count) {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T *>(allocate(product(count, sizeof(T)), alignof(T)));
    }
    void reset() noexcept { used_ = 0; }
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::size_t used() const noexcept { return used_; }
    std::size_t peak() const noexcept { return peak_; }
    std::size_t allocations() const noexcept { return allocations_; }
};
inline Arena &thread_scratch(std::size_t minimum_capacity = 1024 * 1024) {
    thread_local std::unique_ptr<Arena> arena;
    if (!arena)
        arena = std::make_unique<Arena>(minimum_capacity);
    if (arena->capacity() < minimum_capacity)
        throw std::length_error("configure thread scratch before use");
    return *arena;
}
} // namespace sdb
