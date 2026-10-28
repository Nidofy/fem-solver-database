#include <sdb/lifecycle.hpp>
#include <sdb/results/export.hpp>
#include <iostream>
#include <random>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const std::string &text = "") {
    ++checks;
    try {
        f();
    } catch (const std::exception &e) {
        if (!text.empty() && std::string(e.what()).find(text) == std::string::npos)
            throw std::runtime_error(std::string("wrong diagnostic: ") + e.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + text);
}
std::string read_text(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write_text(const std::filesystem::path &p, const std::string &s) {
    std::ofstream out(p, std::ios::binary);
    out << s;
}
std::string replaced(std::string s, const std::string &a, const std::string &b) {
    auto p = s.find(a);
    if (p == s.npos)
        throw std::runtime_error("test replacement missing");
    s.replace(p, a.size(), b);
    return s;
}
struct FailingStorage : IResultStorage {
    void append(const ResultFrame &) override { throw std::runtime_error("injected disk failure"); }
    void flush() override {}
    std::optional<FrameInfo> last() const override { return {}; }
};
template <class T> native::Bytes state_bytes(const StateDB<T> &state) {
    native::Writer w;
    const auto &d = state.data();
    d.fields([&](const auto &f) {
        using F = std::decay_t<decltype(f)>;
        auto view = f.read();
        for (std::size_t n = 0; n < f.size(); ++n)
            for (unsigned c = 0; c < F::components; ++c)
                w.scalar(view(n, c));
    });
    native::encode(w, d.step);
    native::encode(w, d.time);
    native::encode(w, d.dt);
    return std::move(w.bytes);
}
template <class T> void output_trajectories(const std::filesystem::path &fixture) {
    auto model = ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    auto off = ModelCompiler{}.compile<T>(model.definition);
    auto every = ModelCompiler{}.compile<T>(model.definition);
    for (auto &step : off.outputs)
        for (auto &r : step) {
            r.frequency = 0;
            r.intervals = 0;
        }
    for (auto &step : every.outputs)
        for (auto &r : step) {
            r.frequency = 1;
            r.intervals = 0;
        }
    CpuRun<T> a(off), b(model), c(every);
    a.initialize();
    b.initialize();
    c.initialize();
    ResultFrame retained;
    native::Bytes retained_bytes;
    do {
        auto before = state_bytes(c.state());
        auto count = a.output_force_evaluations();
        check(a.snapshot().fields.empty(), "disabled output has no fields");
        check(a.output_force_evaluations() == count, "disabled output has no derived evaluations");
        auto previous = b.output_force_evaluations();
        auto rare = b.snapshot();
        check(b.output_force_evaluations() - previous == (rare.fields.empty() ? 0u : 1u),
              "non-due output has no derived evaluations");
        auto f = c.snapshot();
        check(state_bytes(c.state()) == before, "all state fields unchanged by snapshot");
        check(native::pack(c.snapshot()) == native::pack(f), "repeat snapshot identical");
        check(state_bytes(a.state()) == state_bytes(b.state()) &&
                  state_bytes(b.state()) == state_bytes(c.state()),
              "all output trajectories bit identical");
        if (c.increment() == 1) {
            retained = std::move(f);
            retained_bytes = native::pack(retained);
        }
        if (c.increment() > 1)
            check(native::pack(retained) == retained_bytes, "snapshot owns data after later steps");
        auto more = a.advance();
        check(b.advance() == more && c.advance() == more, "matching output trajectory cursors");
        if (!more)
            break;
    } while (true);
}

template <class T> void numerical(const std::filesystem::path &fixture, const std::filesystem::path &temp) {
    auto m = ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    check(m.report.executable_cpu && !m.report.executable_cuda, "capability");
    check(m.mesh.nodes() == 5 && m.mesh.elements() == 2, "counts");
    auto free = m.sources.lookup(true, {"", "", 40}).index;
    CpuRun<T> run(m);
    run.initialize();
    check(std::abs(double(run.state().data().mass.read()(free)) - 1. / 24) < 1e-8, "lumped mass");
    auto path = temp / (sizeof(T) == 4 ? "float.sdbres" : "double.sdbres");
    auto split = temp / (sizeof(T) == 4 ? "split-f.sdbres" : "split-d.sdbres");
    auto rst = temp / (sizeof(T) == 4 ? "f.sdbrst" : "d.sdbrst");
    auto store = temp / (sizeof(T) == 4 ? "f.sdbmodel" : "d.sdbmodel");
    ModelStore::save(store, m);
    auto copy = ModelStore::load<T>(store);
    check(copy.mesh.fingerprint() == m.mesh.fingerprint(), "model reload");
    check(copy.sections.size() == 2 && copy.sources.reverse(true, free).key.label == 40,
          "model semantics/source reload");
    auto result_model = ResultModel::from(m, run.configuration());
    {
        NativeResultStorage storage(path, result_model);
        ResultWriter writer(storage);
        writer.write(run.snapshot());
        while (run.advance()) {
            writer.write(run.snapshot());
            if (run.state().data().step == 700)
                run.save(rst);
        }
        writer.flush();
    }
    double time = run.state().data().time, expected = .06 * (1 - std::cos(20 * time)),
           actual = run.state().data().displacement.read()(free, 0);
    check(std::abs(actual - expected) < (sizeof(T) == 4 ? 3e-6 : 1e-7), "forced response reference");
    ResultReader reader(path);
    check(reader.index.size() == 21, "21 result frames");
    auto u = reader.field(reader.index.size() - 1, "U");
    auto stress = reader.field(reader.index.size() - 1, "S");
    check(std::abs(stress.values[0] - 100 * actual) < (sizeof(T) == 4 ? 1e-5 : 1e-11), "coherent U/S");
    auto history = ResultQuery::history(reader, "U", Id(free) + 1, 0, "FREE", true);
    check(history.size() == 21, "history selection");
    check(reader.field(4, "U", "FREE").ids.size() == 1, "region filter");
    rejects([&] { reader.field(4, "U", "MISSING"); }, "unknown");
    rejects([&] { reader.by_time(2); }, "outside");
    check(reader.by_time(reader.index[7].time) == 7, "time index");
    for (auto i : {17u, 2u, 20u, 0u, 8u}) {
        auto frame = reader.frame(i);
        check(frame.info.id == i * 100, "random reader frame");
        auto velocity = reader.field(i, "V");
        check(std::abs(velocity.values[free * 3] - 1.2 * std::sin(20 * frame.info.time)) < 2e-5,
              "full-time velocity");
        auto energy = reader.field(i, "ALLSE");
        auto disp = reader.field(i, "U").values[free * 3];
        check(std::abs(energy.values[0] - .5 * (100. / 6) * disp * disp) < 1e-7, "strain energy");
    }
    // The checkpoint was taken after coherent output, without changing integration history.
    CpuRun<T> resumed(copy);
    resumed.restore(rst);
    check(resumed.state().data().step == 700, "restart cursor");
    {
        NativeResultStorage storage(split, result_model);
        for (std::size_t i = 0; i <= 7; ++i)
            storage.append(reader.frame(i));
    }
    {
        NativeResultStorage storage(split, result_model, true);
        ResultWriter writer(storage);
        while (resumed.advance())
            writer.write(resumed.snapshot());
    }
    check(native::pack(reader.frame(20)) == native::pack(ResultReader(split).frame(20)),
          "restart append exact final frame");
    check(read_text(path) == read_text(split), "restart append byte-identical results");
    auto broken = temp / (sizeof(T) == 4 ? "broken-f.sdbres" : "broken-d.sdbres");
    auto bytes = read_text(path);
    bytes.pop_back();
    write_text(broken, bytes);
    rejects([&] { ResultReader r(broken); }, "incomplete");
    {
        ResultReader recovery(broken, true);
        check(recovery.index.size() == 20, "incomplete tail prefix");
    }
    {
        NativeResultStorage writer(broken, result_model, true, true);
        writer.append(reader.frame(20));
    }
    check(read_text(path) == read_text(broken), "tail repair");
    bytes = read_text(path);
    bytes.back() ^= 1;
    write_text(broken, bytes);
    rejects([&] { ResultReader r(broken, true); }, "CRC");
    {
        NativeResultStorage writer(split, result_model, true);
        rejects([&] { writer.append(reader.frame(20)); }, "nonmonotonic");
    }
    bytes = read_text(rst);
    bytes.back() ^= 1;
    auto badrst = temp / (sizeof(T) == 4 ? "bad-f.sdbrst" : "bad-d.sdbrst");
    write_text(badrst, bytes);
    auto old = resumed.state().data().step;
    rejects([&] { resumed.restore(badrst); });
    check(resumed.state().data().step == old, "transactional restart failure");
    rejects(
        [&] {
            auto frame = run.snapshot(1);
            (void)frame;
        },
        "budget");
    FailingStorage faildisk;
    ResultWriter failwriter(faildisk);
    rejects([&] { failwriter.write(reader.frame(0)); }, "injected");
    check(run.state().data().step == 2000, "writer failure leaves committed state");
    ResultExporter::vtk(reader, temp / (sizeof(T) == 4 ? "vtk-f" : "vtk-d"));
    ResultExporter::xdmf(reader, temp / (sizeof(T) == 4 ? "f.xdmf" : "d.xdmf"));
    std::cout << "precision=" << sizeof(T) << " response_error=" << std::abs(actual - expected)
              << " frames=" << reader.index.size() << " exact_restart=1\n";
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("fixtures and scratch arguments required");
        std::filesystem::path fixtures = argv[1], temp = argv[2];
        std::filesystem::create_directories(
            temp); // CTest uses an isolated scratch folder; test outputs are individually replaced below.
        for (auto &entry : std::filesystem::directory_iterator(temp))
            if (entry.is_regular_file())
                std::filesystem::remove(entry.path());
        auto source = read_text(fixtures / "forced_tet.inp");
        std::erase(source, '\r'); // Normalize the mutation fixture; CRLF parsing is tested below.
        AbaqusInpReader parser;
        auto parse = [&](std::string text) { return parser.buffer(text, temp / "buffer.inp"); };
        auto parsed = parse(source);
        check(parsed.report.parsed, "parsed");
        auto bom = std::string("\xef\xbb\xbf") + source;
        for (std::size_t p = 0; (p = bom.find('\n', p)) != bom.npos; p += 2)
            bom.replace(p, 1, "\r\n");
        check(parse(bom).model.parts[0].nodes.size() == 5, "BOM CRLF");
        auto continuation =
            replaced(source, "*Element, type=C3D4, elset=SOLID", "*Element,\n type=C3D4, elset=SOLID");
        check(parse(continuation).model.parts[0].elements[0].nodes.size() == 4, "keyword/data continuation");
        for (auto [a, b] :
             std::vector<std::pair<std::string, std::string>>{{"*Elastic", "*Plastic"}, {"100/6", "100/6"}}) {
            if (a == "*Elastic")
                rejects([&] { parse(replaced(source, a, b)); }, "buffer.inp:");
        }
        rejects([&] { parse(replaced(source, "U,V,A,RF", "PEEQ")); }, "unsupported output");
        rejects([&] { parse(replaced(source, "FREE,2,3", "FREE,2,3,0.1")); }, "zero boundary");
        rejects([&] { parse(replaced(source, "*Elastic", "*Elastic,dependencies=1")); },
                "unsupported parameter");
        rejects([&] { parse(replaced(source, "10,0,0,0", "10,NaN,0,0")); }, "nonfinite");
        rejects(
            [&] {
                ModelCompiler{}.compile<double>(
                    parse(replaced(source, "FIXED_LATER\n", "UNKNOWN_SET\n")).model);
            },
            "unresolved");
        rejects(
            [&] {
                ModelCompiler{}.compile<double>(parse(replaced(source, "FIXED_LATER\n", "FIXED\n")).model);
            },
            "cyclic");
        rejects(
            [&] {
                ModelCompiler{}.compile<double>(
                    parse(replaced(source, "material=STEEL", "material=MISSING")).model);
            },
            "material");
        write_text(temp / "nodes.dat", "10,0,0,0\n20,0,1,0\n30,0,0,1\n40,1,0,0\n50,-1,0,0\n");
        auto external = replaced(source, "*Node\n10,0,0,0\n20,0,1,0\n30,0,0,1\n40,1,0,0\n50,-1,0,0",
                                 "*Node,input=nodes.dat");
        check(parse(external).model.parts[0].nodes[2].source.file.find("nodes.dat") != std::string::npos,
              "external NODE source");
        write_text(temp / "part.inc", external);
        {
            auto overflow = parse(source).model;
            overflow.steps[0].loads[0].actions[0].value = 1e40;
            rejects([&] { ModelCompiler{}.compile<float>(overflow); }, "precision");
            IRAmplitude extreme{"EXTREME", false, {{0, -1e308}, {1, 1e308}}, {"test", 1, 1}};
            check(LoadEvaluator::amplitude(extreme, .5, .5) == 0, "finite extreme amplitude interpolation");
            auto scaled = parse(source).model;
            scaled.amplitudes.push_back({"HUGE", false, {{0, 1e20}, {1, 1e20}}, {"test", 1, 1}});
            scaled.steps[0].loads[0].actions[0].value = 1e20;
            scaled.steps[0].loads[0].actions[0].amplitude = "HUGE";
            rejects([&] { ModelCompiler{}.compile<float>(scaled); }, "precision");
        }
        auto included = parse("*INCLUDE,INPUT=part.inc\n");
        check(included.model.parts[0].elements.size() == 2, "include");
        write_text(temp / "cycle.inc", "*INCLUDE,INPUT=cycle.inc\n");
        rejects([&] { parse("*INCLUDE,INPUT=cycle.inc\n"); }, "cycle");
        rejects([&] { parse("*INCLUDE,INPUT=../escape.inp\n"); }, "escapes");
        ReaderOptions low;
        low.max_bytes = 10;
        rejects([&] { parser.buffer(source, temp / "buffer.inp", low); }, "budget");
        low = {};
        low.max_depth = 1;
        rejects([&] { parser.buffer("*INCLUDE,INPUT=part.inc\n", temp / "buffer.inp", low); }, "depth");
        ReaderOptions meshonly;
        meshonly.mode = ImportMode::MeshOnly;
        auto opaque = parser.buffer(source + "*CONTACT\n", temp / "buffer.inp", meshonly);
        check(opaque.model.opaque.size() == 1, "opaque mesh-only block");
        auto imported = ModelCompiler{}.compile<double>(std::move(opaque.model));
        check(!imported.report.executable_cpu, "opaque fails capability");
        auto instances =
            ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixtures / "instances.inp").model);
        check(instances.mesh.nodes() == 8 && instances.mesh.elements() == 2, "instance counts");
        auto left = instances.sources.lookup(true, {"P", "LEFT", 20}).index,
             right = instances.sources.lookup(true, {"P", "RIGHT", 20}).index;
        check(left != right, "instance identity separation");
        check(std::abs(instances.mesh.reference()(right, 0)) < 1e-12 &&
                  std::abs(instances.mesh.reference()(right, 1) - 2) < 1e-12,
              "translation before rotation");
        check(instances.region(true, "BOTH").size() == 8 &&
                  instances.region(false, "RIGHT_SOLID").size() == 1,
              "scoped sets");
        check(instances.sections.size() == 2, "instanced properties");
        check(instances.analysis.steps[1].loads.empty(), "OP NEW load reset");
        check(LoadEvaluator::amplitude(instances.analysis.amplitudes.at("RAMP"), 0, .005) == .5,
              "tabular amplitude");
        CpuRun<double> instance_run(instances);
        while (instance_run.advance()) {
        }
        check(instance_run.finished(), "multi-step execution");
        {
            NativeResultStorage storage(temp / "instances.sdbres",
                                        ResultModel::from(instances, instance_run.configuration()));
            storage.append(instance_run.snapshot());
        }
        ResultReader instance_reader(temp / "instances.sdbres");
        check(instance_reader.field(0, "U", "@INSTANCE:RIGHT").ids.size() == 4, "instance result filtering");
        check(instance_reader.field(0, "U", "@PART:P").ids.size() == 8, "part result filtering");
        rejects([&] { instance_reader.field(0, "U", "@INSTANCE:MISSING"); }, "unknown");
        auto tet = descriptor("C3D4", {});
        check(abaqus_face(tet, "S1") == std::vector<unsigned>({0, 1, 2}), "tet face S1");
        check(abaqus_face(tet, "S4") == std::vector<unsigned>({2, 3, 0}), "tet face S4");
        auto hex = descriptor("C3D8", {}), reduced = descriptor("C3D8R", {});
        check(hex.points == 8 && reduced.points == 1 && hex.integration != reduced.integration,
              "hex formulations");
        check(abaqus_face(hex, "S6") == std::vector<unsigned>({3, 7, 4, 0}), "hex face S6");
        check(abaqus_face(descriptor("S4R", {}), "SNEG") == std::vector<unsigned>({3, 2, 1, 0}),
              "shell negative side");
        rejects([&] { abaqus_face(tet, "S5"); }, "invalid surface");
        auto stored =
            ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixtures / "stored_types.inp").model);
        check(stored.report.database_stored && !stored.report.executable_cpu, "stored types capability");
        check(stored.descriptors[0].points == 8 && stored.descriptors[1].points == 1 &&
                  stored.descriptors[2].formulation == Formulation::Shell &&
                  stored.descriptors[3].formulation == Formulation::Beam,
              "stored type fidelity");
        check(stored.sections[2].properties[0] == .1 && stored.sections[3].properties[1] == .2,
              "shell/beam section properties");
        rejects([&] { CpuRun<double> reject(stored); }, "CPU");
        ModelStore::save(temp / "stored.sdbmodel", stored);
        auto storedcopy = ModelStore::load<double>(temp / "stored.sdbmodel");
        check(storedcopy.descriptors[1].token == "C3D8R", "unsupported type reload fidelity");
        for (unsigned i = 1; i <= 4; ++i)
            check(abaqus_face(tet, "S" + std::to_string(i)).size() == 3, "all tetra faces");
        for (unsigned i = 1; i <= 6; ++i)
            check(abaqus_face(hex, "S" + std::to_string(i)).size() == 4, "all hexa faces");
        rejects([&] { parse(replaced(source, "name=FORCE,nlgeom=NO", "name=FORCE")); }, "NLGEOM=NO");
        rejects([&] { parse(replaced(source, "*Element, type=C3D4", "*Element, type=C3D10")); },
                "unsupported element");
        output_trajectories<float>(fixtures / "forced_tet.inp");
        output_trajectories<double>(fixtures / "forced_tet.inp");
        for (auto fixture : {"forced_hex.inp", "mixed_hex_tet.inp"}) {
            output_trajectories<float>(fixtures / fixture);
            output_trajectories<double>(fixtures / fixture);
        }
        numerical<float>(fixtures / "forced_tet.inp", temp);
        numerical<double>(fixtures / "forced_tet.inp", temp);
        rejects([&] { ModelStore::load<float>(temp / "d.sdbmodel"); }, "precision");
        auto corrupt_model = read_text(temp / "d.sdbmodel");
        corrupt_model.back() ^= 1;
        write_text(temp / "corrupt.sdbmodel", corrupt_model);
        rejects([&] { ModelStore::load<double>(temp / "corrupt.sdbmodel"); }, "CRC");
        auto model = ModelCompiler{}.compile<double>(parse(source).model);
        CpuRun<double> legacy(model);
        legacy.initialize();
        for (int i = 0; i < 700; ++i)
            legacy.advance();
        save_checkpoint(temp / "legacy-v2.chk", model.mesh, legacy.state());
        CpuRun<double> converted(model);
        converted.import_v2(temp / "legacy-v2.chk");
        converted.save(temp / "converted.sdbrst");
        CpuRun<double> converted_again(model);
        converted_again.restore(temp / "converted.sdbrst");
        while (legacy.advance()) {
        }
        while (converted_again.advance()) {
        }
        check(native::pack(legacy.snapshot()) == native::pack(converted_again.snapshot()),
              "v2 conversion exact trajectory");
        legacy.state().data().time += 1;
        save_checkpoint(temp / "invalid-plan-v2.chk", model.mesh, legacy.state());
        auto before_invalid = native::pack(converted_again.snapshot());
        rejects([&] { converted_again.import_v2(temp / "invalid-plan-v2.chk"); }, "analysis plan");
        check(before_invalid == native::pack(converted_again.snapshot()),
              "failed v2 conversion preserves active run");
        auto changed_definition = parse(source).model;
        changed_definition.steps[0].increment = .0002;
        auto changed = ModelCompiler{}.compile<double>(std::move(changed_definition));
        CpuRun<double> wrong_config(changed);
        rejects([&] { wrong_config.restore(temp / "converted.sdbrst"); }, "configuration");
        write_text(temp / "atomic.bin", "old committed data");
        rejects(
            [&] {
                native::atomic_stream(temp / "atomic.bin", [](auto &out) {
                    out << "partial replacement";
                    throw std::runtime_error("injected atomic failure");
                });
            },
            "injected");
        check(read_text(temp / "atomic.bin") == "old committed data", "atomic old file preserved");
        auto multi = parse(replaced(source, "0.0001,0.2", "0.0001,0.1") +
                           "*Step,name=RELEASE,nlgeom=NO\n*Dynamic,Explicit,Direct User "
                           "Control\n0.0002,0.1\n*Cload,op=NEW\n*End Step\n")
                         .model;
        auto multimodel = ModelCompiler{}.compile<double>(std::move(multi));
        CpuRun<double> multirun(multimodel);
        multirun.initialize();
        while (multirun.advance()) {
            if (multirun.state().data().step == 1000)
                multirun.save(temp / "multi.sdbrst");
        }
        double u1 = .06 * (1 - std::cos(2.)), v1 = 1.2 * std::sin(2.);
        double ref = u1 * std::cos(2.) + v1 / 20 * std::sin(2.);
        check(std::abs(multirun.state().data().displacement.read()(3, 0) - ref) < 1e-6,
              "step load removal/dt rephase analytic reference");
        CpuRun<double> multirestart(multimodel);
        multirestart.restore(temp / "multi.sdbrst");
        while (multirestart.advance()) {
        }
        check(native::pack(multirun.snapshot()) == native::pack(multirestart.snapshot()),
              "step-boundary exact restart");
        auto ramptext = replaced(source, "*Cload\nFREE,1,1", "*Cload,amplitude=RAMP\nFREE,1,1");
        ramptext = replaced(ramptext, "*Step,name=FORCE",
                            "*Amplitude,name=RAMP,definition=TABULAR\n0,0,0.2,1\n*Step,name=FORCE");
        auto rampmodel = ModelCompiler{}.compile<double>(parse(ramptext).model);
        CpuRun<double> ramprun(rampmodel);
        while (ramprun.advance()) {
        }
        double rampref = .3 * (.2 - std::sin(4.) / 20);
        check(std::abs(ramprun.state().data().displacement.read()(3, 0) - rampref) < 2e-7,
              "tabular load analytic response");
        auto bcmodel = parse(replaced(source, "0.0001,0.2", "0.0001,0.1") +
                             "*Step,name=BC_RELEASE,nlgeom=NO\n*Dynamic,Explicit,Direct User "
                             "Control\n0.0001,0.1\n*Boundary,op=NEW\nFIXED,1,3\nFREE,2,3\n*End Step\n")
                           .model;
        bcmodel.initial_boundaries[0].actions.push_back({"FREE", "", 0, 0, 0, {"BC", 1, 1}});
        auto bcc = ModelCompiler{}.compile<double>(std::move(bcmodel));
        CpuRun<double> bcrun(bcc);
        while (bcrun.advance()) {
        }
        check(std::abs(bcrun.state().data().displacement.read()(3, 0) - u1) < 1e-7,
              "step boundary activation/release");
        auto empty =
            ModelCompiler{}.compile<double>(parse(replaced(source, "frequency=100", "frequency=0")).model);
        for (auto &outputs : empty.outputs)
            for (auto &o : outputs)
                o.frequency = 0;
        CpuRun<double> emptyrun(empty);
        auto rm = ResultModel::from(empty, emptyrun.configuration());
        {
            NativeResultStorage storage(temp / "empty.sdbres", rm);
            ResultWriter writer(storage);
            writer.write(emptyrun.snapshot());
        }
        check(ResultReader(temp / "empty.sdbres").index.empty(), "empty output file");
        // emptyrun only initialized and published once: exactly the initialize evaluation,
        // because frequency=0 never schedules a frame (eager publication cost one more).
        check(emptyrun.force_evaluations() == 1, "no-output publication costs no force evaluations");
        // R1.01: publication must not re-evaluate forces on non-output increments,
        // and a pure-U frame needs no constitutive data at all.
        {
            auto full = ModelCompiler{}.compile<double>(parse(source).model);
            CpuRun<double> lazy(full);
            lazy.initialize();
            (void)lazy.snapshot(); // increment 0 is due and carries non-U fields
            std::uint64_t due_frames = 1;
            while (lazy.advance()) {
                auto before = lazy.force_evaluations();
                auto frame = lazy.snapshot();
                auto after = lazy.force_evaluations();
                auto inc = lazy.increment();
                bool due = inc % 100 == 0; // fixture: frequency=100, 2000 increments
                check(after - before == (due ? 1u : 0u), "publication evaluates only due increments");
                check(frame.fields.empty() != due, "due increments carry fields");
                due_frames += due;
            }
            check(due_frames == 21, "due frame count");
            // 1 evaluation per completed increment, 1 for initialize, 21 for due publications.
            check(lazy.force_evaluations() == 2000 + 1 + 21, "lazy publication total evaluations");
            auto u_ir = parse(source).model;
            u_ir.steps[0].outputs.clear();
            IROutput u_only;
            u_only.frequency = 100;
            u_only.variables = {"U"};
            u_ir.steps[0].outputs.push_back(u_only);
            auto u_model = ModelCompiler{}.compile<double>(std::move(u_ir));
            CpuRun<double> u_run(u_model);
            u_run.initialize();
            std::uint64_t u_frames = 1; // increment 0 snapshot is due
            (void)u_run.snapshot();
            while (u_run.advance()) {
                auto before = u_run.force_evaluations();
                auto frame = u_run.snapshot();
                u_frames += !frame.fields.empty();
                check(u_run.force_evaluations() == before, "U-only publication skips force evaluation");
            }
            check(u_frames == 21, "U-only due frame count");
            check(u_run.force_evaluations() == 2001, "U-only run evaluation total");
            check(u_run.state().data().displacement.read()(3, 0) ==
                      lazy.state().data().displacement.read()(3, 0),
                  "publication laziness leaves trajectory bit-identical");
        }
        // R1.02: publication is read-only, repeatable and reports one coherent
        // physical time layer (U/V/A/S at t[n] via current-displacement evaluation).
        {
            auto m =
                ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixtures / "forced_tet.inp").model);
            CpuRun<double> run(m);
            run.initialize();
            auto gather = [](auto view, unsigned comps, std::vector<double> &out) {
                for (std::size_t i = 0; i < view.count; ++i)
                    for (unsigned c = 0; c < comps; ++c)
                        out.push_back(double(view(i, c)));
            };
            auto trajectory = [&]() {
                std::vector<double> sig;
                auto &d = run.state().data();
                gather(d.displacement.read(), 3, sig);
                gather(d.velocity.read(), 3, sig);
                gather(d.acceleration.read(), 3, sig);
                gather(d.external_force.read(), 3, sig);
                gather(d.contact_force.read(), 3, sig);
                gather(d.internal_force.read(), 3, sig);
                gather(d.stress.read(), 6, sig);
                gather(d.strain.read(), 6, sig);
                gather(d.history.read(), 1, sig);
                sig.push_back(double(d.step));
                sig.push_back(d.time);
                sig.push_back(d.dt);
                return sig;
            };
            double published_frames = 0;
            for (std::uint64_t n = 0; n <= 2000; ++n) {
                if (n) {
                    if (!run.advance())
                        throw std::runtime_error("unexpected end of run");
                    // loads() ran twice inside advance; a late re-application must not
                    // change the published layer either way.
                }
                bool due = n % 100 == 0;
                auto before = trajectory();
                check(!run.state().in_trial(), "publication outside trial state");
                auto f1 = run.snapshot();
                auto f2 = run.snapshot();
                check(trajectory() == before, "publication leaves trajectory untouched");
                check(native::pack(f1) == native::pack(f2), "publication is repeatable");
                check(f1.fields.empty() != due, "due increments publish fields");
                if (due) {
                    ++published_frames;
                    double t = f1.info.time;
                    check(t == run.state().data().time, "frame time equals state time");
                    auto field = [&](const char *name) -> const ResultField & {
                        for (auto &v : f1.fields)
                            if (v.descriptor.name == name && !v.descriptor.history)
                                return v;
                        throw std::runtime_error(std::string("missing field ") + name);
                    };
                    auto &u = field("U"), &vel = field("V"), &acc = field("A"), &str = field("S");
                    double un = u.values[3 * 3]; // free node index 3, component X
                    check(std::abs(un - .06 * (1 - std::cos(20 * t))) < 1e-7, "published U layer");
                    check(std::abs(vel.values[3 * 3] - 1.2 * std::sin(20 * t)) < 2e-5,
                          "published V reconstructed to full time");
                    check(std::abs(acc.values[3 * 3] - 24 * std::cos(20 * t)) < 1e-4, "published A layer");
                    check(std::abs(str.values[0] - 100 * un) < 1e-11, "published S coherent with U");
                }
            }
            check(published_frames == 21, "contract run frame count");
            // Cross-step dt change: publication stays coherent across the rephase
            // boundary (load release at t=0.1, dt 0.0001 -> 0.0002).
            auto multi =
                ModelCompiler{}.compile<double>(parse(replaced(source, "0.0001,0.2", "0.0001,0.1") +
                                                      "*Step,name=RELEASE,nlgeom=NO\n*Dynamic,Explicit,"
                                                      "Direct User Control\n0.0002,0.1\n*Cload,op=NEW\n*"
                                                      "End Step\n")
                                                    .model);
            CpuRun<double> released(multi);
            released.initialize();
            double u1 = .06 * (1 - std::cos(2.)), v1 = 1.2 * std::sin(2.);
            auto released_ref = [&](double t) {
                double tau = 20 * (t - .1);
                return std::array<double, 3>{u1 * std::cos(tau) + v1 / 20 * std::sin(tau),
                                             -20 * u1 * std::sin(tau) + v1 * std::cos(tau), 0.};
            };
            while (released.advance()) {
                auto inc = released.increment();
                auto step = released.step_index();
                if (step != 1 || inc % 100 != 0)
                    continue;
                auto frame = released.snapshot();
                double t = frame.info.time;
                auto [ur, vr, ar] = released_ref(t);
                ar = -400 * ur; // released oscillator: a = -k u / m
                const ResultField *u = nullptr, *vel = nullptr, *acc = nullptr;
                for (auto &v : frame.fields) {
                    if (v.descriptor.history)
                        continue; // history requests carry regional subsets; use full fields
                    if (v.descriptor.name == "U")
                        u = &v;
                    else if (v.descriptor.name == "V")
                        vel = &v;
                    else if (v.descriptor.name == "A")
                        acc = &v;
                }
                check(u && vel && acc, "released-step fields present");
                check(std::abs(u->values[3 * 3] - ur) < 2e-6, "released-step U layer");
                check(std::abs(vel->values[3 * 3] - vr) < 3e-5, "released-step V layer across dt change");
                check(std::abs(acc->values[3 * 3] - ar) < 2e-3, "released-step A layer across dt change");
            }
            // Restart publication equivalence at a mid-step cursor is already proven
            // byte-for-byte by the split/restart append checks in numerical<T>();
            // the read-only contract above guarantees no history commit happened.
        }
        std::cout << "lifecycle checks=" << checks << " PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
