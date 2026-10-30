#pragma once
#include "dynamics.hpp"

namespace sdb {
// Explicitly uploaded single-GPU state is authoritative until download().
// CPU StateDB is a snapshot, never automatically synchronized by accessors.
template <class T> class CudaExplicit {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    CudaExplicit(const MeshDB<T> &mesh, StateDB<T> &initial, int device = 0);
    ~CudaExplicit();
    CudaExplicit(const CudaExplicit &) = delete;
    CudaExplicit &operator=(const CudaExplicit &) = delete;
    void initialize(T dt);
    void step();
    void wait();
    void download(StateDB<T> &target);
    float elapsed_kernel_ms();
    std::size_t device_peak_bytes() const;
};
extern template class CudaExplicit<float>;
extern template class CudaExplicit<double>;
} // namespace sdb
