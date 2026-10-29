#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    ++checks;
    try { f(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected rejection");
}
template <class T> HistorySchema history_schema() {
    HistorySchema h;
    h.id = "synthetic-storage-fixture.history";
    HistoryVariable a, b;
    a.field.name = "damage";
    a.field.scalar = scalar_type<T>();
    a.initial = {.25};
    b.field.name = "plastic_strain";
    b.field.scalar = scalar_type<T>();
    b.field.shape = {3};
    b.field.components = {"XX", "YY", "XY"};
    b.field.shear = ShearConvention::Tensor;
    b.initial = {1, 2, 3};
    h.variables = {a, b};
    return h;
}
template <class T> ModelIR definition(const std::filesystem::path &fixture) {
    auto ir = ReaderRegistry{}.read(fixture).model;
    ir.schema = 2;
    ir.history_bindings.push_back({ir.materials.at(0).name, 1, history_schema<T>()});
    return ir;
}
void descriptor_contracts() {
    auto h = history_schema<double>();
    // Independent wire oracle: 00 01 00 00 is 256, not Float32 (0).
    rejects([&] { native::unpack<ScalarType>(native::Bytes{0, 1, 0, 0}); });
    check(native::unpack<ScalarType>(native::Bytes{1, 0, 0, 0}) == ScalarType::Float64,
          "little endian enum oracle");
    rejects([&] { native::unpack<bool>(native::Bytes{2}); });
    check(h.validate<double>() == 4, "component count");
    check(h.offset("plastic_strain") == 1, "named offset");
    check(native::pack(native::unpack<HistorySchema>(native::pack(h))) == native::pack(h), "schema bytes");
    auto bad = h;
    bad.variables[0].field.shape = {0}; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].field.version = 2; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].field.scalar = ScalarType(255); rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].field.restart = RestartPolicy::Omit; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].field.missing = MissingPolicy::ValidityMask; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].field.name = "plastic_strain"; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[1].field.components[1] = "XX"; rejects([&] { bad.validate<double>(); });
    bad = h; bad.variables[0].initial = {}; rejects([&] { bad.validate<double>(); });
    bad = h; bad.version = 0; rejects([&] { bad.validate<double>(); });
    rejects([&] { h.validate<float>(); });
    auto f = h.variables[1].field;
    f.shape = {UINT32_MAX, UINT32_MAX, UINT32_MAX}; rejects([&] { f.validate(); });
    rejects([&] { index_of(std::numeric_limits<Index>::max()); });
    FieldRegistry registry;
    auto &field = registry.add<Field<double, 3, AoSoA<4>>>(h.variables[1].field, 5);
    field.write()(4, 2) = 12;
    check(registry.descriptor("plastic_strain").components[2] == "XY", "registry metadata");
    rejects([&] { registry.add<Field<float>>(h.variables[0].field, 1); });
    registry.freeze();
    rejects([&] { registry.add<Field<double>>(h.variables[0].field, 1); });
    auto view = field.checked_read();
    field.resize(6);
    rejects([&] { (void)view.at(0); });
    check(field.read()(4, 2) == 12, "layout-preserving resize");
}
template <class T> void history_contracts(const std::filesystem::path &fixture,
                                         const std::filesystem::path &dir) {
    const auto tag = sizeof(T) == 4 ? "float" : "double";
    auto ir = definition<T>(fixture);
    auto m = ModelCompiler{}.compile<T>(ir);
    StateDB<T> s(m.mesh);
    check(!m.report.executable_cpu, "storage-only capability");
    rejects([&] { m.require_cpu(); });
    rejects([&] { CpuRun<T> run(m); });
    rejects([&] { CpuExplicit<T> run(m.mesh, s); });
    check(s.data().history.size() == 36, "mixed 8+1 IP history shape");
    for (Index e = 0; e < m.mesh.elements(); ++e)
        for (std::size_t ip = 0; ip < m.mesh.element(e).integration_points; ++ip) {
            auto base = s.history_address(m.mesh, e, ip, "damage");
            // Independent manually inspectable oracle: each IP is [.25,1,2,3].
            check(base == (e == 0 ? ip * 4 : 32), "independent history offset oracle");
            check(s.data().history.read()(base) == T(.25), "initial damage");
            check(s.data().history.read()(base + 3) == T(3), "initial shear");
        }
    rejects([&] { s.history_address(m.mesh, 0, 8, "damage"); });
    rejects([&] { s.history_address(m.mesh, 0, 0, "absent"); });
    rejects([&] { s.history_address(m.mesh, 0, 0, "damage", 1); });
    s.data().dt = .001;
    s.data().history.write()(35) = T(17);
    s.begin_trial(); s.data().history.write()(35) = T(91); s.rollback();
    check(s.data().history.read()(35) == T(17), "history rollback");
    auto model_path = dir / (std::string(tag) + ".sdbmodel");
    auto rst_path = dir / (std::string(tag) + ".sdbrst");
    ModelStore::save(model_path, m);
    auto loaded = ModelStore::load<T>(model_path);
    check(native::pack(m.definition) == native::pack(loaded.definition), "model schema roundtrip");
    check(loaded.mesh.materials()[0].history_schema.id == ir.history_bindings[0].history_schema.id,
          "material query after reload");
    RestartInfo info; info.configuration = 123;
    RestartDB::save(rst_path, m, s, info);
    StateDB<T> restored(loaded.mesh);
    auto got = RestartDB::load(rst_path, loaded, restored, 123);
    check(got.model_schema == 2 && restored.data().history.read()(35) == T(17), "schema2 restart");
    auto low_path = dir / (std::string(tag) + ".checkpoint");
    save_checkpoint(low_path, m.mesh, s);
    load_checkpoint(low_path, m.mesh, restored);
    check(restored.data().history.read()(35) == T(17), "lowlevel history restart");
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto changed = ir;
        auto &h = changed.history_bindings[0].history_schema;
        if (mutation == 0) h.variables[0].field.name = "different_damage";
        if (mutation == 1) std::swap(h.variables[0], h.variables[1]);
        if (mutation == 2) ++h.version;
        if (mutation == 3) h.variables[1].field.shear = ShearConvention::Engineering;
        if (mutation == 4) std::swap(h.variables[1].field.components[0], h.variables[1].field.components[1]);
        if (mutation == 5) h.variables[0].initial[0] = .5;
        auto other = ModelCompiler{}.compile<T>(changed);
        check(other.mesh.fingerprint() != m.mesh.fingerprint(), "same-count semantic identity");
        StateDB<T> target(other.mesh);
        target.data().history.fill(T(72));
        rejects([&] { RestartDB::load(rst_path, other, target, 123); });
        rejects([&] { load_checkpoint(low_path, other.mesh, target); });
        check(target.data().history.read()(0) == T(72), "failed restore atomicity");
    }
    auto unknown = ir;
    unknown.history_bindings[0].model = 700;
    auto storage = ModelCompiler{}.compile<T>(unknown);
    ModelStore::save(dir / (std::string(tag) + "-unknown.sdbmodel"), storage);
    rejects([&] { storage.require_cpu(); });
    auto invalid = ir; invalid.schema = 1;
    rejects([&] { ModelCompiler{}.compile<T>(invalid); });
    rejects([&] { native::pack(invalid); });
    invalid = ir; invalid.history_bindings[0].material = "absent";
    rejects([&] { ModelCompiler{}.compile<T>(invalid); });
    invalid = ir; invalid.history_bindings.push_back(invalid.history_bindings[0]);
    rejects([&] { ModelCompiler{}.compile<T>(invalid); });
    invalid = ir; invalid.schema = 90;
    rejects([&] { ModelCompiler{}.compile<T>(invalid); });
    StateDB<T> rotational(m.mesh, true);
    rotational.data().rotation->angle.fill(T(9));
    rotational.data().history.fill(T(8));
    rejects([&] { RestartDB::load(rst_path, m, rotational, 123); });
    check(rotational.data().rotation && rotational.data().rotation->angle.read()(0) == T(9) &&
          rotational.data().history.read()(0) == T(8), "rotation rejection preserves all state");
    auto bytes = native::pack(ir);
    bytes.pop_back(); rejects([&] { native::unpack<ModelIR>(bytes); });
    rejects([&] { native::unpack<ModelIR>(native::pack(ir), 8); });
}
// A separate process loads files produced by the writer invocation.
void process_read(const std::filesystem::path &dir) {
    auto m = ModelStore::load<double>(dir / "double.sdbmodel");
    StateDB<double> s(m.mesh);
    RestartDB::load(dir / "double.sdbrst", m, s, 123);
    check(m.definition.schema == 2 && s.data().history.size() == 36, "independent reader metadata");
    check(s.data().history.read()(35) == 17, "independent reader sample");
    check(s.data().history.read()(0) == .25, "independent reader initial");
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--read") process_read(argv[2]);
        else {
            if (argc != 3) throw std::runtime_error("fixture scratch expected");
            std::filesystem::create_directories(argv[2]);
            descriptor_contracts();
            history_contracts<float>(argv[1], argv[2]);
            history_contracts<double>(argv[1], argv[2]);
        }
        std::cout << checks << " foundation checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
