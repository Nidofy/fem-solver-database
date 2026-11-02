#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace sdb {
enum class CpuPolicy { Serial, OpenMP };
template <class F> void parallel_for(std::size_t count, CpuPolicy policy, F &&fn) {
#ifndef _OPENMP
    if (policy == CpuPolicy::OpenMP)
        throw std::runtime_error("OpenMP backend not compiled");
#else
    if (policy == CpuPolicy::OpenMP) {
#pragma omp parallel for schedule(static)
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(count); ++i)
            fn(static_cast<std::size_t>(i));
        return;
    }
#endif
    for (std::size_t i = 0; i < count; ++i)
        fn(i);
}
} // namespace sdb
