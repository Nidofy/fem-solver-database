#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <sdb/results/checkpoint_link.hpp>
#include <chrono>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
using namespace sdb;
using namespace sdb::lifecycle;
template <class F> double measure(F f) {
    auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}
std::uint64_t peak() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS p{};
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &p, sizeof(p)))
        throw std::runtime_error("memory counter unavailable");
    return p.PeakWorkingSetSize;
#else
    rusage p{};
    getrusage(RUSAGE_SELF, &p);
    return std::uint64_t(p.ru_maxrss) * 1024;
#endif
}
template <class T>
void run(const std::filesystem::path &input, const std::filesystem::path &directory, bool asynchronous) {
    std::filesystem::create_directories(directory);
    auto file = directory / "run.sdbres", restart = directory / "run.sdbrst";
    if (std::filesystem::exists(file))
        throw std::runtime_error("benchmark requires fresh directory");
    ReadResult parsed;
    double parse_ms = measure([&] { parsed = ReaderRegistry{}.read(input); });
    std::optional<CompiledModel<T>> m;
    double compile_ms = measure([&] { m.emplace(ModelCompiler{}.compile<T>(std::move(parsed.model))); });
    std::optional<CpuRun<T>> cpu;
    double plan_ms = measure([&] { cpu.emplace(*m); });
    double initialize_ms = measure([&] { cpu->initialize(); });
    std::optional<NativeResultStorage> storage;
    double storage_setup_ms = measure([&] {
        storage.emplace(file, ResultModel::from(*m, cpu->configuration()), false, false,
                        native::default_limit, std::function<void()>{}, true);
    });
    std::optional<AsyncResultWriter> async;
    if (asynchronous)
        async.emplace(*storage, 1024 * 1024);
    double first_step_ms = 0, steady_steps_ms = 0, snapshot_ms = 0, encode_ms = 0, write_or_admit_ms = 0;
    std::uint64_t frames = 0, encoded_bytes = 0, increments = 0;
    auto publish = [&] {
        ResultFrame frame;
        snapshot_ms += measure([&] { frame = cpu->snapshot(); });
        if (frame.fields.empty())
            return;
        ++frames;
        write_or_admit_ms += measure([&] {
            if (async)
                async->write(std::move(frame));
            else
                storage->append(frame);
        });
    };
    double run_with_output_ms = measure([&] {
        publish();
        while (!cpu->finished()) {
            auto ms = measure([&] { cpu->advance(); });
            if (!increments)
                first_step_ms = ms;
            else
                steady_steps_ms += ms;
            ++increments;
            publish();
        }
    });
    double flush_ms = measure([&] {
        if (async)
            async->close();
        else
            storage->flush();
    });
    auto encode_sample = cpu->snapshot();
    encode_ms = measure([&] {
        for (unsigned i = 0; i < 20; ++i)
            encoded_bytes += native::pack(encode_sample).size();
    });
    double checkpoint_ms = measure([&] { CheckpointLink::publish(file, restart, *cpu, *m); });
    std::optional<ResultReader> reader;
    double reopen_ms = measure([&] { reader.emplace(file); });
    auto open_read_bytes = reader->read_stats().bytes;
    double random_ms = measure([&] {
        for (unsigned i = 0; i < 20; ++i)
            (void)reader->frame((i * 7919) % frames);
    });
    std::vector<std::pair<double, double>> history;
    double history_ms = measure([&] { history = ResultQuery::history(*reader, "U", 1); });
    CpuRun<T> resumed(*m);
    double restart_ms = measure([&] { resumed.restore(restart); });
    if (native::pack(resumed.snapshot()) != native::pack(cpu->snapshot()))
        throw std::runtime_error("benchmark restart mismatch");
    auto stats = async ? async->stats() : AsyncResultWriter::Stats{};
    std::cout << std::setprecision(9) << "{\"fixture\":\"" << input.filename().string()
              << "\",\"precision\":" << sizeof(T)
              << ",\"execution_policy\":\"serial\",\"async\":" << (asynchronous ? "true" : "false")
              << ",\"nodes\":" << m->mesh.nodes() << ",\"elements\":" << m->mesh.elements()
              << ",\"increments\":" << increments << ",\"frames\":" << frames << ",\"parse_ms\":" << parse_ms
              << ",\"compile_ms\":" << compile_ms << ",\"plan_ms\":" << plan_ms
              << ",\"initialize_ms\":" << initialize_ms << ",\"storage_setup_ms\":" << storage_setup_ms
              << ",\"first_step_ms\":" << first_step_ms
              << ",\"steady_step_mean_ms\":" << steady_steps_ms / (increments - 1)
              << ",\"snapshot_total_ms\":" << snapshot_ms << ",\"standalone_v1_encode_20_ms\":" << encode_ms
              << ",\"write_or_admit_ms\":" << write_or_admit_ms << ",\"end_to_end_output_ms\":"
              << (parse_ms + compile_ms + plan_ms + initialize_ms + storage_setup_ms + run_with_output_ms +
                  flush_ms)
              << ",\"run_with_output_ms\":" << run_with_output_ms << ",\"flush_ms\":" << flush_ms
              << ",\"checkpoint_ms\":" << checkpoint_ms << ",\"warm_cache_reopen_ms\":" << reopen_ms
              << ",\"random_20_frames_ms\":" << random_ms << ",\"node_history_ms\":" << history_ms
              << ",\"restart_load_ms\":" << restart_ms << ",\"open_read_bytes\":" << open_read_bytes
              << ",\"query_read_bytes\":" << reader->read_stats().bytes
              << ",\"decoded_value_bytes\":" << reader->read_stats().decoded_value_bytes
              << ",\"file_bytes\":" << std::filesystem::file_size(file)
              << ",\"v1_encoded_bytes\":" << encoded_bytes
              << ",\"queue_peak_charged_bytes\":" << stats.peak_bytes << ",\"process_peak_bytes\":" << peak()
              << "}\n";
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("usage: lifecycle_stages fixture-directory fresh-output-directory");
        for (auto file : {"forced_tet.inp", "forced_hex.inp", "mixed_hex_tet.inp"})
            for (bool async : {false, true}) {
                auto dir =
                    std::filesystem::path(argv[2]) / (std::string(file) + (async ? "-async" : "-sync"));
                run<float>(std::filesystem::path(argv[1]) / file, dir / "float", async);
                run<double>(std::filesystem::path(argv[1]) / file, dir / "double", async);
            }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
