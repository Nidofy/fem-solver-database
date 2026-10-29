#include <sdb/results/async_writer.hpp>
#include <future>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
using durability::Level;
static unsigned checks = 0;
static void check(bool value, const char *why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
template<class F> static void rejects(F fn) {
    ++checks;
    try { fn(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected rejection");
}
static ResultFrame frame(std::uint64_t id, const ResultModel &model = {}, bool typed = false) {
    ResultFrame out;
    out.info.id = id; out.info.increment = id; out.info.time = out.info.local_time = double(id);
    out.info.mesh_revision = model.mesh;
    ResultField field;
    field.descriptor.name = "ENERGY"; field.descriptor.location = Location::Global;
    field.descriptor.components = {"VALUE"}; field.ids = {0}; field.values = {double(id)};
    if (typed) {
        FieldDescriptor d; d.name = "ENERGY"; d.association = Association::Global;
        d.location = FieldLocation::Entity; d.scalar = ScalarType::Float64; d.shape = {1}; d.components = {"VALUE"};
        ResultAddress a; a.entity = 0;
        field = make_typed_field<double>(d, {a}, {double(id)});
        bind_result_frame(out, model);
    }
    out.fields.push_back(std::move(field));
    return out;
}
struct Fault {
    durability::FaultPoint target;
    unsigned calls = 0;
    static void invoke(durability::FaultPoint point, const std::filesystem::path &, void *context) {
        auto &self = *static_cast<Fault *>(context);
        if (point == self.target) { ++self.calls; throw std::runtime_error("injected durability failure"); }
    }
};
struct MemoryStorage : IResultStorage {
    std::optional<FrameInfo> info;
    void append(const ResultFrame &f) override { info = f.info; }
    void flush() override {}
    std::optional<FrameInfo> last() const override { return info; }
};
struct BarrierStorage : MemoryStorage {
    std::promise<void> entered, release;
    std::shared_future<void> gate = release.get_future().share();
    std::thread::id barrier_thread;
    bool fail = false;
    ResultBarrier barrier(Level level) override {
        if (level == Level::FileSync) {
            barrier_thread = std::this_thread::get_id();
            entered.set_value();
            gate.wait();
            if (fail) throw std::runtime_error("injected worker sync failure");
        }
        return {level, {}, info ? info->id : 0, info};
    }
};
int main(int argc, char **argv) {
    try {
        check(argc == 2, "scratch argument");
        const std::filesystem::path dir = argv[1];
        std::filesystem::create_directories(dir);
        for (auto format : {ResultFormat::Native1, ResultFormat::Indexed2, ResultFormat::TypedIndexed3}) {
            const auto path = dir / (std::to_string(static_cast<unsigned>(format)) + ".sdbres");
            std::filesystem::remove(path);
            const ResultModel model{};
            const bool typed = format == ResultFormat::TypedIndexed3;
            std::uint64_t first_size = 0;
            {
                NativeResultStorage storage(path, model, false, false, native::default_limit, {}, false, format);
                check(storage.path() == std::filesystem::absolute(path).lexically_normal(), "absolute output path");
                check(storage.model().mesh == model.mesh, "storage model accessor");
                auto initial = storage.barrier(Level::FileSync);
                check(!initial.last && initial.bytes > 0, "empty committed prefix barrier");
                storage.append(frame(1, model, typed));
                auto stream = storage.barrier(Level::StreamFlush);
                check(stream.level == Level::StreamFlush && stream.last->id == 1, "stream watermark");
                auto file = storage.barrier(Level::FileSync);
                check(file.level == Level::FileSync && file.bytes == stream.bytes && file.path == storage.path(), "file watermark");
                check(ResultReader(path).index.size() == 1, "visible committed prefix");
                first_size = file.bytes;
                if constexpr (durability::supports_parent_sync) {
                    check(storage.barrier(Level::PublicationSync).level == Level::PublicationSync, "publication watermark");
                } else {
                    rejects([&] { storage.barrier(Level::PublicationSync); });
                }
                rejects([&] { storage.barrier(static_cast<Level>(99)); });
                storage.append(frame(2, model, typed));
                check(storage.barrier(Level::FileSync).bytes > first_size, "unsupported requests did not poison writer");
            }
            {
                NativeResultStorage storage(path, model, true);
                auto resumed = storage.barrier(Level::FileSync);
                check(resumed.last->id == 2 && resumed.bytes > first_size, "append barrier covers inherited prefix");
                Fault fault{durability::FaultPoint::BeforeFileSync};
                { durability::ScopedFaultHook hook({&Fault::invoke, &fault});
                  rejects([&] { storage.barrier(Level::FileSync); }); }
                check(fault.calls == 1, "file sync fault reached");
                rejects([&] { storage.append(frame(3, model, typed)); });
                rejects([&] { storage.flush(); });
                rejects([&] { storage.barrier(Level::StreamFlush); });
            }
            check(ResultReader(path).index.size() == 2, "failed sync preserves readable prefix without acknowledgement");
            {
                NativeResultStorage storage(path, model, true);
                Fault fault{durability::FaultPoint::AfterFileSync};
                { durability::ScopedFaultHook hook({&Fault::invoke, &fault});
                  rejects([&] { storage.barrier(Level::FileSync); }); }
                check(fault.calls == 1, "post-sync fault reached");
                rejects([&] { storage.barrier(Level::FileSync); });
            }
            {
                NativeResultStorage storage(path, model, true);
                AsyncResultWriter writer(storage, AsyncResultWriter::required_bytes(frame(3, model, typed)));
                writer.write(frame(3, model, typed));
                auto receipt = writer.barrier(Level::FileSync);
                check(receipt.level == Level::FileSync && receipt.last->id == 3 &&
                      receipt.bytes == std::filesystem::file_size(path), "native worker file sync acknowledgement");
                writer.close();
            }
            check(ResultReader(path).index.size() == 3, "native async reader sees acknowledged prefix");
            if (format != ResultFormat::Native1) {
                const auto broken = dir / (std::to_string(static_cast<unsigned>(format)) + "-recover.sdbres");
                std::filesystem::remove(broken);
                std::uint64_t good_size = 0;
                {
                    NativeResultStorage storage(broken, model, false, false, native::default_limit, {}, false, format);
                    storage.append(frame(1, model, typed));
                    good_size = storage.barrier(Level::FileSync).bytes;
                    Fault fault{durability::FaultPoint::IndexCommit};
                    { durability::ScopedFaultHook hook({&Fault::invoke, &fault});
                      rejects([&] { storage.append(frame(2, model, typed)); }); }
                    check(fault.calls == 1, "index commit fault reached");
                    rejects([&] { storage.barrier(Level::FileSync); });
                }
                auto incomplete = ResultReader(broken, true);
                check(incomplete.incomplete_tail && incomplete.valid_end == good_size,
                      "uncommitted directory is a recoverable tail");
                {
                    NativeResultStorage storage(broken, model, true, true);
                    auto receipt = storage.barrier(Level::FileSync);
                    check(receipt.bytes == good_size && receipt.last->id == 1,
                          "file sync barrier covers recovery truncation");
                }
                check(ResultReader(broken).index.size() == 1, "repaired prefix reopens without recovery");
            }
        }
        const auto charge = AsyncResultWriter::required_bytes(frame(1));
        {
            MemoryStorage storage;
            check(storage.barrier(Level::StreamFlush).level == Level::StreamFlush, "legacy custom backend stream barrier");
            rejects([&] { storage.barrier(Level::FileSync); });
            AsyncResultWriter writer(storage, charge);
            writer.write(frame(1));
            rejects([&] { writer.barrier(Level::FileSync); });
            check(writer.stats().failed, "unsupported worker durability propagates failure");
            rejects([&] { writer.close(); });
        }
        for (bool fail : {false, true}) {
            BarrierStorage storage;
            storage.fail = fail;
            AsyncResultWriter writer(storage, charge * 2);
            writer.write(frame(1)); writer.write(frame(2));
            auto pending = std::async(std::launch::async, [&] { return writer.barrier(Level::FileSync); });
            storage.entered.get_future().wait();
            check(pending.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout,
                  "no acknowledgement before worker sync completes");
            check(writer.stats().committed == 2, "barrier drains all admitted frames before sync");
            storage.release.set_value();
            if (fail) {
                rejects([&] { pending.get(); });
                check(writer.stats().failed, "sync failure poisons async writer");
                rejects([&] { writer.write(frame(3)); });
                rejects([&] { writer.close(); });
            } else {
                auto receipt = pending.get();
                check(receipt.level == Level::FileSync && receipt.last->id == 2 && receipt.bytes == 2,
                      "worker returns exact successful prefix receipt");
                check(storage.barrier_thread != std::this_thread::get_id(), "storage barrier executes on worker");
                writer.close(); writer.close();
                rejects([&] { writer.barrier(Level::FileSync); });
                check(writer.barrier(Level::StreamFlush).last->id == 2, "closed stream acknowledgement is stable");
            }
        }
        std::cout << "DBF4 result durability checks=" << checks << " PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

