#include <sdb/lifecycle.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
using namespace sdb;
using namespace sdb::lifecycle;
std::uint64_t peak() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS p{};
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &p, sizeof(p)))
        throw std::runtime_error("peak memory unavailable");
    return p.PeakWorkingSetSize;
#else
    rusage p{};
    if (getrusage(RUSAGE_SELF, &p))
        throw std::runtime_error("peak memory unavailable");
    return std::uint64_t(p.ru_maxrss) * 1024;
#endif
}
template <class F> double timed(F f) {
    auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
int main(int argc, char **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error(
                "usage: sdb_lifecycle_benchmark node-count memory-budget-MiB output-directory");
        auto requested = std::stoull(argv[1]), budget = std::stoull(argv[2]) * 1024 * 1024;
        std::filesystem::path dir = argv[3];
        std::filesystem::create_directories(dir);
        auto ne = requested / 4;
        if (ne == 0 || ne >= invalid_index / 4)
            throw std::invalid_argument("node count");
        auto nn = 4 * ne;
        // Conservative preflight for IR, compiler maps, mesh, state/plan, header and one U frame.
        if (nn > budget / 3000)
            throw std::length_error("preflight memory budget: require 3000 bytes/node");
        ModelIR ir;
        ir.parts.push_back({"", {}, {}, {"generated", 1, 1}});
        auto &p = ir.parts[0];
        p.nodes.reserve(nn);
        p.elements.reserve(ne);
        ir.materials.push_back({"M", 1, 100, 0, {"generated", 1, 1}});
        IRSet set{"", "ALL", "", false, {}, {"generated", 1, 1}};
        set.members.reserve(ne);
        for (Id e = 0; e < ne; ++e) {
            Id base = 4 * e + 1;
            double x = double(e) * 2;
            p.nodes.push_back({base, {x, 0, 0}, {"generated", 1, 1}});
            p.nodes.push_back({base + 1, {x + 1, 0, 0}, {"generated", 1, 1}});
            p.nodes.push_back({base + 2, {x, 1, 0}, {"generated", 1, 1}});
            p.nodes.push_back({base + 3, {x, 0, 1}, {"generated", 1, 1}});
            p.elements.push_back(
                {e + 1, descriptor("C3D4", {}), {base, base + 1, base + 2, base + 3}, {"generated", 1, 1}});
            set.members.push_back(std::to_string(e + 1));
        }
        ir.sets.push_back(std::move(set));
        ir.sections.push_back({"", "ALL", "M", "SOLID SECTION", {}, {"generated", 1, 1}});
        IRStep step;
        step.name = "BENCH";
        step.period = .0021;
        step.increment = .0001;
        step.direct = true;
        // 21 increments; due at 0, 10, 20, 21. Measures both a non-due and a
        // due (pure-U) publication per run.
        step.outputs.push_back({false, 10, 0, "", {"U"}, {"generated", 1, 1}});
        ir.steps.push_back(step);
        auto start = std::chrono::steady_clock::now();
        auto model = ModelCompiler{}.compile<double>(std::move(ir));
        double compile_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        // CpuRun exposes no policy parameter: it constructs CpuExplicit with the
        // default CpuPolicy::Serial, so every timing below is serial execution
        // regardless of OMP_NUM_THREADS. Reported explicitly (B01-A-001/O-002):
        // no parallel-efficiency conclusion may be drawn from this benchmark.
        // Setup phases kept distinct (B01-A-004): compile_ms above covers IR
        // compilation only; run_setup_ms covers StateDB+TetPlan+plan build;
        // initialize_ms covers the first force pass + half-step velocity setup.
        double run_setup_ms = timed([&] {
            CpuRun<double> probe(model); // same construction work; discarded
            (void)probe;
        });
        CpuRun<double> run(model);
        double initialize_ms = timed([&] { run.initialize(); });
        auto enforce = [&] {
            if (peak() > budget)
                throw std::length_error("measured peak exceeded memory budget");
        };
        enforce();
        // True kernel-only region: one serial forces() (element loop +
        // assembly) on a twin state prepared outside the timed region.
        double kernel_only_ms = 0;
        {
            StateDB<double> twin(model.mesh);
            CpuExplicit<double> kernel(model.mesh, twin);
            kernel_only_ms = timed([&] { kernel.forces(twin); });
        }
        enforce();
        double kernel_ms = timed([&] { run.advance(); }); // increment 1: non-due
        ResultFrame frame;
        // Publication cost on a non-output increment (R1.01 lazy path).
        double nondue_snapshot_ms = timed([&] { (void)run.snapshot(); });
        // Steady-state stepping: increments 2..9, away from first-touch effects.
        double steady_kernel_ms = timed([&] {
            for (int i = 0; i != 8; ++i)
                run.advance();
        });
        run.advance(); // increment 10: due, pure-U frame
        double snapshot_ms = timed([&] { frame = run.snapshot(); });
        std::uint64_t plan_increments = 0;
        for (auto &p : run.plans())
            plan_increments += p.increments;
        const auto executed_increments = run.state().data().step; // advances performed
        constexpr std::uint64_t frames_written = 1;               // single due-U frame below
        enforce();
        auto result = dir / "million.sdbres", restart = dir / "million.sdbrst";
        if (std::filesystem::exists(result))
            throw std::runtime_error("benchmark output exists; choose a fresh directory");
        const auto io_limit = budget / 4;
        auto rm = ResultModel::from(model, run.configuration());
        double write_ms = timed([&] {
            NativeResultStorage storage(result, std::move(rm), false, false, io_limit);
            storage.append(frame);
        });
        enforce();
        double restart_write_ms = timed([&] { run.save(restart); });
        enforce();
        double read_ms = timed([&] {
            ResultReader reader(result, false, io_limit);
            auto u = reader.field(0, "U");
            if (u.ids.size() != nn || u.values.size() != 3 * nn)
                throw std::runtime_error("benchmark result read mismatch");
        });
        enforce();
        double restore_ms = timed([&] { run.restore(restart); });
        enforce();
        const char *omp_env = std::getenv("OMP_NUM_THREADS");
        std::cout << std::setprecision(9) << "{\"execution_policy\":\"serial\""
                  << ",\"execution_policy_note\":\"CpuRun default; lifecycle API has no OpenMP selection\""
#ifdef _OPENMP
                  << ",\"omp_compiled\":1"
                  << ",\"omp_max_threads\":" << omp_get_max_threads()
#else
                  << ",\"omp_compiled\":0"
                  << ",\"omp_max_threads\":0"
#endif
                  << ",\"omp_env\":" << (omp_env ? "\"" + std::string(omp_env) + "\"" : "\"\"")
                  << ",\"nodes\":" << nn << ",\"elements\":" << ne
                  << ",\"plan_increments\":" << plan_increments
                  << ",\"executed_increments\":" << executed_increments
                  << ",\"frames_written\":" << frames_written << ",\"run_setup_ms\":" << run_setup_ms
                  << ",\"initialize_ms\":" << initialize_ms << ",\"kernel_only_ms\":" << kernel_only_ms
                  << ",\"budget_bytes\":" << budget << ",\"peak_process_bytes\":" << peak()
                  << ",\"compile_ms\":" << compile_ms << ",\"kernel_ms\":" << kernel_ms
                  << ",\"nondue_snapshot_ms\":" << nondue_snapshot_ms
                  << ",\"steady_kernel_ms\":" << steady_kernel_ms
                  << ",\"steady_kernel_per_increment_ms\":" << steady_kernel_ms / 8
                  << ",\"force_evaluations\":" << run.force_evaluations()
                  << ",\"snapshot_ms\":" << snapshot_ms << ",\"native_write_ms\":" << write_ms
                  << ",\"reader_scan_query_ms\":" << read_ms << ",\"restart_write_ms\":" << restart_write_ms
                  << ",\"restart_restore_ms\":" << restore_ms
                  << ",\"result_bytes\":" << std::filesystem::file_size(result)
                  << ",\"restart_bytes\":" << std::filesystem::file_size(restart)
                  << ",\"hdf5\":\"unavailable\",\"gpu_transfer\":\"unverified\"}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
