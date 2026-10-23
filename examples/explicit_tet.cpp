#include "sdb/checkpoint.hpp"
#include "sdb/dynamics.hpp"
#include <iomanip>
#include <iostream>
#ifdef SDB_HAS_CUDA
#include "sdb/cuda_backend.hpp"
#endif
template <class T> void run() {
    using namespace sdb;
    auto mesh = oscillator_model<T>();
    StateDB<T> state(mesh);
    CpuExplicit<T> cpu(mesh, state);
    const auto n = mesh.node_index(40);
    state.data().displacement.write()(n) = T(.01);
    cpu.initialize(state, T(.0001));
    for (int i = 0; i < 500; ++i)
        cpu.step(state);
    const auto path =
        std::filesystem::path(sizeof(T) == 4 ? "example_float.checkpoint" : "example_double.checkpoint");
    save_checkpoint(path, mesh, state);
    StateDB<T> resumed(mesh);
    CpuExplicit<T> restart(mesh, resumed);
    load_checkpoint(path, mesh, resumed);
    for (int i = 0; i < 500; ++i) {
        cpu.step(state);
        restart.step(resumed);
    }
    T exact = T(.01) * std::cos(T(20) * T(state.data().time));
    T error = std::abs(state.data().displacement.read()(n) - exact);
    if (error > T(2e-7) || state.data().displacement.read()(n) != resumed.data().displacement.read()(n))
        throw std::runtime_error("oscillator/reference or restart mismatch");
    std::cout << std::setprecision(12) << "precision=" << sizeof(T) * 8 << " steps=" << state.data().step
              << " time=" << state.data().time << " displacement=" << state.data().displacement.read()(n)
              << " analytic=" << exact << " abs_error=" << error << " restart_error=0";
#ifdef SDB_HAS_CUDA
    StateDB<T> gpu_state(mesh);
    gpu_state.data().displacement.write()(n) = T(.01);
    CudaExplicit<T> gpu(mesh, gpu_state);
    gpu.initialize(T(.0001));
    for (int i = 0; i < 500; ++i)
        gpu.step();
    gpu.download(gpu_state);
    save_checkpoint(path, mesh, gpu_state);
    StateDB<T> gpu_restart_state(mesh);
    load_checkpoint(path, mesh, gpu_restart_state);
    CudaExplicit<T> gpu_restart(mesh, gpu_restart_state);
    for (int i = 0; i < 500; ++i) {
        gpu.step();
        gpu_restart.step();
    }
    gpu.download(gpu_state);
    gpu_restart.download(gpu_restart_state);
    T gpu_error = 0;
    for (Index i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            gpu_error = std::max(gpu_error, std::abs(gpu_state.data().displacement.read()(i, c) -
                                                     state.data().displacement.read()(i, c)));
    if (gpu_error > (sizeof(T) == 4 ? T(2e-7) : T(1e-12)))
        throw std::runtime_error("CPU/CUDA mismatch");
    if (gpu_state.data().displacement.read()(n) != gpu_restart_state.data().displacement.read()(n))
        throw std::runtime_error("CUDA restart mismatch");
    std::cout << " cuda_error=" << gpu_error << " cuda_kernel_ms=" << gpu.elapsed_kernel_ms();
#else
    std::cout << " cuda=not_built";
#endif
    std::cout << '\n';
    std::filesystem::remove(path);
}
int main() {
    try {
        run<float>();
        run<double>();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
