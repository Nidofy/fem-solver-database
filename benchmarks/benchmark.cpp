#include "sdb/contact.hpp"
#include "sdb/dynamics.hpp"
#include <iomanip>
#include <iostream>
#include "raw_tet.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef SDB_HAS_CUDA
#include "sdb/cuda_backend.hpp"
#include "sdb/cuda_memory.hpp"
#endif
using Clock = std::chrono::steady_clock;
std::size_t process_peak_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return 0;
    return info.PeakWorkingSetSize;
#else
    rusage info{};
    if (getrusage(RUSAGE_SELF, &info))
        return 0;
#ifdef __APPLE__
    return info.ru_maxrss;
#else
    return std::size_t(info.ru_maxrss) * 1024;
#endif
#endif
}
template <class F> double timed(F &&fn) {
    auto t = Clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
template <class T> void benchmark(std::size_t n, std::size_t repetitions, sdb::CpuPolicy policy) {
    using namespace sdb;
    auto begin = Clock::now();
    Field<T, 3> x(n), v(n);
    x.fill(T(.2));
    v.fill(T(.01));
    auto xv = x.write();
    auto vv = v.read();
    AlignedVector<T> raw_x(product(n, 3), T(.2)), raw_v(product(n, 3), T(.01));
    auto *rx = raw_x.data();
    auto *rv = raw_v.data();
    auto allocation_start = host_memory.allocations.load();
    auto view_loop = [&] {
        parallel_for(n, policy, [=](std::size_t i) {
            for (unsigned c = 0; c < 3; ++c)
                xv(i, c) += T(.001) * vv(i, c);
        });
    };
    auto raw_loop = [&] {
        parallel_for(n, policy, [=](std::size_t i) {
            for (unsigned c = 0; c < 3; ++c)
                rx[c * n + i] += T(.001) * rv[c * n + i];
        });
    };
    view_loop();
    raw_loop(); // Warm both allocations and execution paths.
    double view_ms = 0, raw_ms = 0;
    for (std::size_t r = 0; r < repetitions; ++r) {
        if (r % 2) {
            raw_ms += timed(raw_loop);
            view_ms += timed(view_loop);
        } else {
            view_ms += timed(view_loop);
            raw_ms += timed(raw_loop);
        }
    }
    double checksum = 0;
    for (std::size_t i = 0; i < 3 * n; ++i) {
        if (xv.data[i] != rx[i])
            throw std::runtime_error("baseline disagreement");
        checksum += rx[i];
    }
    const auto loop_allocations = host_memory.allocations.load() - allocation_start;
    Field<T, 8> history(n);
    auto hv = history.write();
    double ip_ms = timed([&] {
        for (std::size_t r = 0; r < repetitions; ++r)
            parallel_for(n, policy, [=](std::size_t i) {
                for (unsigned c = 0; c < 8; ++c)
                    hv(i, c) += T(.0001) * (c + 1);
            });
    });
    std::array<T, 4> materials{1, 2, 3, 4};
    double group_ms = timed([&] {
        for (std::size_t r = 0; r < repetitions; ++r)
            for (unsigned g = 0; g < 4; ++g) {
                auto first = n * g / 4, last = n * (g + 1) / 4;
                parallel_for(last - first, policy,
                             [=](std::size_t i) { hv(first + i, 0) += materials[g] * T(.0001); });
            }
    });
    CandidateBuffer<ContactKey> candidates(n);
    double contact_ms = timed([&] {
        parallel_for(n, policy,
                     [&](std::size_t i) { candidates.append({{Id(i), 0, 1}, {Id(i + 1), 0, -1}, 0, 0}); });
    });
    candidates.reset();
    double contact_batch_ms = timed([&] {
        parallel_for((n + 255) / 256, policy, [&](std::size_t block) {
            ContactKey batch[256];
            const auto count = std::min<std::size_t>(256, n - block * 256);
            for (std::size_t j = 0; j < count; ++j) {
                auto i = block * 256 + j;
                batch[j] = {{Id(i), 0, 1}, {Id(i + 1), 0, -1}, 0, 0};
            }
            candidates.append_batch({batch, count});
        });
    });
    if (candidates.entries().size() != n || candidates.dropped())
        throw std::runtime_error("candidate batch mismatch");
    // Independent unit tetrahedra share one origin: extreme assembly fan-in plus O(N) mesh.
    const auto ne = std::max<std::size_t>(1, (n - 1) / 3);
    ModelBuilder<T> builder;
    builder.reserve(3 * ne + 1, ne);
    builder.add_material({1, 1, {T(1), T(100), T(.2)}, 4});
    builder.add_part(1);
    builder.add_node(0, {0, 0, 0});
    for (std::size_t e = 0; e < ne; ++e) {
        Id b = 3 * e + 1;
        builder.add_node(b, {1, 0, 0});
        builder.add_node(b + 1, {0, 1, 0});
        builder.add_node(b + 2, {0, 0, 1});
        builder.add_element(e, Topology::Tet4, {0, b, b + 1, b + 2}, 1, 1);
    }
    auto mesh = builder.finalize();
    StateDB<T> state(mesh);
    CpuExplicit<T> solver(mesh, state, policy);
    auto displacement = state.data().displacement.write();
    for (std::size_t i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            displacement(i, c) = T(.0001) * mesh.reference()(i, c);
    solver.initialize(state, T(.0001));
    AlignedVector<T> raw_local(product(ne, 12)), raw_stress(product(ne, 6)), raw_strain(product(ne, 6)),
        raw_force(product(mesh.nodes(), 3));
    allocation_start = host_memory.allocations.load();
    const auto steps = std::min<std::size_t>(repetitions, 10);
    double gather_scatter = timed([&] {
        for (std::size_t r = 0; r < steps; ++r)
            solver.forces(state);
    });
    const auto &plan = solver.plan();
    auto run_raw = [&] {
        raw_tet_forces(ne, mesh.nodes(), plan.nodes.read().data, plan.gradients.read().data,
                       plan.volume.read().data, plan.lambda.read().data, plan.mu.read().data,
                       state.data().displacement.read().data, raw_local.data(), raw_stress.data(),
                       raw_strain.data(), plan.adjacency_offsets.data(), plan.adjacency.data(),
                       raw_force.data(), policy, state.data().element_active.read().data);
    };
    double raw_gather_scatter = timed([&] {
        for (std::size_t r = 0; r < steps; ++r)
            run_raw();
    });
    for (std::size_t i = 0; i < raw_force.size(); ++i)
        if (raw_force[i] != state.data().internal_force.read().data[i])
            throw std::runtime_error("raw TET baseline disagreement");
    double explicit_ms = timed([&] {
        for (std::size_t r = 0; r < steps; ++r)
            solver.step(state);
    });
    const auto step_allocations = host_memory.allocations.load() - allocation_start;
    double e2e = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    std::cout << std::setprecision(9) << "{\"precision\":" << sizeof(T) * 8 << ",\"nodes\":" << n
              << ",\"elements\":" << ne << ",\"repetitions\":" << repetitions << ",\"steps\":" << steps
              << ",\"threads\":";
#ifdef _OPENMP
    std::cout << (policy == CpuPolicy::OpenMP ? omp_get_max_threads() : 1);
#else
    std::cout << 1;
#endif
    std::cout << ",\"node_view_ms\":" << view_ms << ",\"node_raw_ms\":" << raw_ms << ",\"ip_ms\":" << ip_ms
              << ",\"material_group_ms\":" << group_ms << ",\"candidate_ms\":" << contact_ms
              << ",\"candidate_batch_ms\":" << contact_batch_ms << ",\"gather_scatter_ms\":" << gather_scatter
              << ",\"explicit_steps_ms\":" << explicit_ms
              << ",\"raw_gather_scatter_ms\":" << raw_gather_scatter << ",\"end_to_end_ms\":" << e2e
              << ",\"host_aligned_peak_bytes\":" << host_memory.peak.load()
              << ",\"process_peak_bytes\":" << process_peak_bytes()
              << ",\"node_hot_allocations\":" << loop_allocations
              << ",\"explicit_hot_allocations\":" << step_allocations
              << ",\"checksum\":" << checksum + hv(0, 0) << "}" << '\n';
#ifdef SDB_HAS_CUDA
    auto ctx = std::make_shared<cuda::Context>();
    cuda::Mirror<T, 3> mirror(ctx, n);
    mirror.assign(x.read());
    cuda::Event transfer_start(ctx, true), transfer_end(ctx, true);
    transfer_start.record();
    mirror.upload();
    mirror.finish();
    transfer_end.record();
    transfer_end.wait();
    float h2d = transfer_end.elapsed_since(transfer_start);
    transfer_start.record();
    mirror.download();
    mirror.finish();
    transfer_end.record();
    transfer_end.wait();
    float d2h = transfer_end.elapsed_since(transfer_start);
    StateDB<T> gs(mesh);
    auto gpu_begin = Clock::now();
    CudaExplicit<T> gpu(mesh, gs);
    gpu.initialize(T(.0001));
    for (std::size_t r = 0; r < steps; ++r)
        gpu.step();
    gpu.download(gs);
    std::cout << "{\"cuda_kernel_ms\":" << gpu.elapsed_kernel_ms() << ",\"cuda_e2e_ms\":"
              << std::chrono::duration<double, std::milli>(Clock::now() - gpu_begin).count()
              << ",\"h2d_ms\":" << h2d << ",\"d2h_ms\":" << d2h
              << ",\"device_peak_bytes\":" << gpu.device_peak_bytes() << "}\n";
#endif
}
int main(int argc, char **argv) {
    try {
        auto n = argc > 1 ? std::stoull(argv[1]) : 100000;
        auto repeats = argc > 2 ? std::stoull(argv[2]) : 20;
        bool omp = argc > 3 && std::string(argv[3]) == "omp";
        if (n < 4 || n >= sdb::invalid_index || repeats == 0)
            throw std::invalid_argument("usage: sdb_benchmark nodes>=4 repetitions [omp]");
        auto policy = omp ? sdb::CpuPolicy::OpenMP : sdb::CpuPolicy::Serial;
        benchmark<float>(n, repeats, policy);
        benchmark<double>(n, repeats, policy);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
