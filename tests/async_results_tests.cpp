#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <future>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
void check(bool v, const char *why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected exception");
}
struct SlowStorage : IResultStorage {
    std::promise<void> entered, release;
    std::shared_future<void> gate = release.get_future().share();
    std::vector<std::uint64_t> ids;
    bool fail = false, fail_flush = false;
    void append(const ResultFrame &f) override {
        if (ids.empty()) {
            entered.set_value();
            gate.wait();
        }
        if (fail)
            throw std::runtime_error("injected background write failure");
        ids.push_back(f.info.id);
    }
    void flush() override {
        if (fail_flush)
            throw std::runtime_error("injected flush failure");
    }
    std::optional<FrameInfo> last() const override {
        if (ids.empty())
            return {};
        FrameInfo f;
        f.id = ids.back();
        return f;
    }
};
ResultFrame sample(std::uint64_t id) {
    ResultFrame f;
    f.info.id = id;
    ResultField v;
    v.descriptor.name = "U";
    v.descriptor.components = {"X", "Y", "Z"};
    v.ids = {1};
    v.values = {double(id), 0, 0};
    f.fields.push_back(std::move(v));
    return f;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture and scratch arguments");
        std::filesystem::path dir = argv[2];
        std::filesystem::create_directories(dir);
        auto charge = AsyncResultWriter::required_bytes(sample(1));
        {
            SlowStorage storage;
            AsyncResultWriter writer(storage, charge);
            writer.write(sample(1));
            storage.entered.get_future().wait();
            auto waiting = std::async(std::launch::async, [&] { writer.write(sample(2)); });
            check(waiting.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
                  "in-flight frame must keep budget reserved");
            auto stats = writer.stats();
            check(stats.queued == 1 && stats.committed == 0 && stats.resident_bytes <= charge,
                  "queued is not committed");
            storage.release.set_value();
            waiting.get();
            writer.close();
            check(storage.ids == std::vector<std::uint64_t>({1, 2}), "ordered lossless backpressure");
            stats = writer.stats();
            check(stats.committed == 2 && stats.peak_bytes <= charge && stats.resident_bytes == 0,
                  "bounded queue drains");
            writer.close();
            rejects([&] { writer.write(sample(3)); });
        }
        for (bool flush_failure : {false, true}) {
            SlowStorage storage;
            storage.fail = !flush_failure;
            storage.fail_flush = flush_failure;
            AsyncResultWriter writer(storage, charge);
            writer.write(sample(1));
            storage.entered.get_future().wait();
            storage.release.set_value();
            rejects([&] { writer.flush(); });
            rejects([&] { writer.close(); });
            check(writer.stats().committed == 0, "failure never reports committed frame");
        }
        {
            SlowStorage storage;
            AsyncResultWriter writer(storage, charge);
            writer.write(sample(1));
            storage.entered.get_future().wait();
            auto waiting = std::async(std::launch::async, [&] { rejects([&] { writer.write(sample(2)); }); });
            writer.cancel();
            waiting.get();
            storage.release.set_value();
            rejects([&] { writer.close(); });
        }
        {
            SlowStorage storage;
            AsyncResultWriter writer(storage, charge - 1);
            rejects([&] { writer.write(sample(1)); });
            writer.close();
        }
        auto model = ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        auto sync_path = dir / "sync.sdbres", async_path = dir / "async.sdbres";
        std::filesystem::remove(sync_path);
        std::filesystem::remove(async_path);
        CpuRun<double> run(model);
        {
            NativeResultStorage sync(sync_path, ResultModel::from(model, run.configuration()));
            NativeResultStorage async(async_path, ResultModel::from(model, run.configuration()));
            rejects([&] {
                NativeResultStorage conflicting(async_path, ResultModel::from(model, run.configuration()),
                                                true);
            });
            AsyncResultWriter writer(async, 262144);
            do {
                auto frame = run.snapshot();
                if (!frame.fields.empty())
                    sync.append(frame);
                writer.write(std::move(frame));
            } while (run.advance());
            writer.close();
            check(writer.stats().committed == 21, "all due frames committed");
        }
        std::ifstream a(sync_path, std::ios::binary), b(async_path, std::ios::binary);
        check(std::string(std::istreambuf_iterator<char>(a), {}) ==
                  std::string(std::istreambuf_iterator<char>(b), {}),
              "sync/async byte equality");
        check(ResultReader(async_path).index.size() == 21, "independent reader");
        std::cout << "async ownership/backpressure/cancel/failure/native equality PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
