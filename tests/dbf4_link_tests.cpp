#include <sdb/results/checkpoint_link.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
namespace fs = std::filesystem;
static unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
struct Injection { durability::FaultPoint point; fs::path path; };
void inject(durability::FaultPoint point, const fs::path &path, void *context) {
    const auto &i = *static_cast<Injection *>(context);
    if (point == i.point && path == i.path) throw std::runtime_error("compound publication test fault");
}
int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::invalid_argument("fixture scratch required");
        const auto root = fs::absolute(argv[2]);
        fs::create_directories(root);
        auto model = ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        for (unsigned scenario = 0; scenario != 7; ++scenario) {
            auto result = root / ("case-" + std::to_string(scenario) + ".sdbres");
            auto restart = root / ("case-" + std::to_string(scenario) + ".sdbrst");
            auto link = CheckpointLink::path(restart);
            fs::remove(result); fs::remove(restart); fs::remove(link);
            CpuRun<double> run(model); run.initialize();
            NativeResultStorage storage(result, ResultModel::from(model, run.configuration()));
            storage.append(run.snapshot());
            const auto initial = CheckpointLink::publish_durable(storage, restart, run, model);
            check(initial.result.level == durability::Level::FileSync && initial.restart_published && initial.link_published,
                  "initial publication acknowledgement");
            CheckpointLink::verify(result, restart, run, model);
            check(initial.result.bytes == fs::file_size(result), "wrong linked watermark");
            run.advance(); // restart changes while the output schedule need not emit a new frame
            Injection fault{durability::FaultPoint::BeforeFileSync, result};
            auto expected = LinkPublicationPhase::ResultBarrier;
            bool changed_restart = false, changed_link = false;
            if (scenario == 1) { fault = {durability::FaultPoint::BeforeReplace, restart}; expected = LinkPublicationPhase::RestartPublication; }
            if (scenario == 2) { fault = {durability::FaultPoint::AfterReplace, restart}; expected = LinkPublicationPhase::RestartPublication; changed_restart = true; }
            if (scenario == 3) { fault = {durability::FaultPoint::BeforeLinkPublish, link}; expected = LinkPublicationPhase::LinkPublication; changed_restart = true; }
            if (scenario == 4) { fault = {durability::FaultPoint::BeforeReplace, link}; expected = LinkPublicationPhase::LinkPublication; changed_restart = true; }
            if (scenario == 5) { fault = {durability::FaultPoint::AfterReplace, link}; expected = LinkPublicationPhase::LinkPublication; changed_restart = changed_link = true; }
            if (scenario == 6) { fault = {durability::FaultPoint::AfterLinkPublish, link}; expected = LinkPublicationPhase::LinkPublication; changed_restart = changed_link = true; }
            bool rejected = false;
            try {
                durability::ScopedFaultHook hook({inject, &fault});
                CheckpointLink::publish_durable(storage, restart, run, model);
            } catch (const LinkPublicationError &e) {
                rejected = true;
                check(e.phase() == expected, "wrong publication error phase");
                check(e.restart_may_have_changed() == changed_restart, "wrong restart change flag");
                check(e.link_may_have_changed() == changed_link, "wrong link change flag");
                check(bool(e.cause()), "missing publication error cause");
            }
            check(rejected, "publication fault not observed");
            CpuRun<double> restored(model); restored.restore(restart);
            check(restored.state().data().step == (changed_restart ? 1 : 0), "restart visibility contradicts error");
            bool linked = true;
            try { CheckpointLink::verify(result, restart, restored, model); } catch (...) { linked = false; }
            check(linked == (!changed_restart || changed_link), "partial publication authorized mismatch");
        }
        // Model identity, path aliases and unsupported guarantees fail before publishing.
        auto output = root / "preflight.sdbres"; fs::remove(output);
        CpuRun<double> run(model); run.initialize();
        NativeResultStorage storage(output, ResultModel::from(model, run.configuration()));
        storage.append(run.snapshot());
        auto bad_model = ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        bad_model.definition.units += "-different";
        bool mismatch = false;
        try { CheckpointLink::publish_durable(storage, root / "wrong.sdbrst", run, bad_model); }
        catch (const std::invalid_argument &) { mismatch = true; }
        check(mismatch && !fs::exists(root / "wrong.sdbrst"), "model mismatch published a checkpoint");
        bool alias = false;
        try { CheckpointLink::publish_durable(storage, output, run, model); }
        catch (const std::invalid_argument &) { alias = true; }
        check(alias, "alias accepted");
        if (!durability::supports_parent_sync) {
            bool unsupported = false;
            try { CheckpointLink::publish_durable(storage, root / "unsupported.sdbrst", run, model, durability::Level::PublicationSync); }
            catch (const std::exception &) { unsupported = true; }
            check(unsupported && !fs::exists(root / "unsupported.sdbrst"), "unsupported level mutated checkpoint");
        }
        std::cout << "DBF4 link publication: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
