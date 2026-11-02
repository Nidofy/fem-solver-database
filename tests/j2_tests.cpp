#include <sdb/lifecycle.hpp>
#include <iostream>

using namespace sdb;
using namespace sdb::lifecycle;

void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
template <class T> void material_point() {
    // nu=0, E=200, hence G=100. Axial strain with fixed lateral strains
    // gives q_trial=200*eps_xx. The three diagonal flow components are 1,-1/2,-1/2.
    Material<T> m{1, j2_model, {1, 200, 0, 2, 0, 3, T(.01)}, 7, j2_schema<T>()};
    J2Law<T> law(m);
    const double tol = sizeof(T) == 4 ? 2e-6 : 2e-13;
    std::array<T, 7> old{};
    auto load = law.trial({T(.02), 0, 0, 0, 0, 0}, old);
    double dp = (4. - 2) / (300 + 100);
    check(std::abs(load.history[6] - dp) < tol && std::abs(load.history[0] - dp) < tol,
          "hardening consistency and axial plastic strain");
    check(std::abs(load.stress[0] - 3) < tol && std::abs(load.stress[1] - .5) < tol,
          "independent axial stress");
    auto unload = law.trial({T(.015), 0, 0, 0, 0, 0}, load.history);
    check(unload.history == load.history && std::abs(unload.stress[0] - 2) < tol, "elastic unloading slope");
    auto reverse = law.trial({T(-.03), 0, 0, 0, 0, 0}, unload.history);
    check(reverse.history[6] > T(.01) && std::abs(reverse.stress[1] - reverse.stress[0] - 3) < tol,
          "reverse plastic flow crosses hardening knot onto plateau");
    double plastic_work = 0;
    for (unsigned c = 0; c < 6; ++c)
        plastic_work += (c < 3 ? 1. : 2.) * reverse.stress[c] * (reverse.history[c] - unload.history[c]);
    check(std::abs(plastic_work - 3 * (reverse.history[6] - unload.history[6])) < tol && plastic_work > 0,
          "positive incremental plastic work");
    auto hydro = law.trial({T(.1), T(.1), T(.1), 0, 0, 0}, old);
    check(hydro.history == old, "hydrostatic loading has no plastic flow");
    m.parameters.resize(5);
    auto shear = J2Law<T>(m).trial({0, 0, 0, T(.04), 0, 0}, old);
    check(std::abs(shear.stress[3] - 2 / std::sqrt(3.)) < tol &&
              std::abs(shear.history[3] - std::sqrt(.75) * shear.history[6]) < tol,
          "perfect plasticity in pure tensor shear");
}
template <class T> void trajectory(const std::filesystem::path &fixture, const std::filesystem::path &dir) {
    auto ir = ReaderRegistry{}.read(fixture).model;
    ReaderOptions mesh_only;
    mesh_only.mode = ImportMode::MeshOnly;
    auto opaque = ReaderRegistry{}.read(fixture, mesh_only).model;
    check(opaque.plastic.empty() && !opaque.opaque.empty(), "mesh-only plastic card remains opaque");
    auto model = ModelCompiler{}.compile<T>(ir);
    check(ir.schema == 5 && model.report.executable_cpu, "executable J2 INP");
    ModelStore::save(dir / "m.sdbmodel", model);
    auto loaded = ModelStore::load<T>(dir / "m.sdbmodel");
    CpuRun<T> run(loaded);
    run.initialize();
    auto eq = run.state().history_address(loaded.mesh, 0, 0, "PEEQ");
    while (run.state().data().history.read()(eq) == 0 && run.advance()) {
    }
    check(run.state().data().history.read()(eq) > 0, "first accepted yield");
    auto before = run.state().data().history;
    auto requests = loaded.outputs[0];
    for (auto &r : requests) {
        r.frequency = 1;
        r.intervals = 0;
    }
    CpuExplicit<T> probe(loaded.mesh, run.state());
    const auto increment = run.state().data().step;
    run.state().begin_trial();
    probe.step(run.state());
    check(run.state().data().history.read()(eq) > before.read()(eq), "trial advances plastic strain");
    run.state().rollback();
    check(run.state().data().step == increment && run.state().data().history.read()(eq) == before.read()(eq),
          "rollback restores committed PEEQ and time level");
    auto frame = ResultPublisher::publish(loaded, run.state(), probe, {}, requests, 1);
    // Only the first tet deforms. Compute recoverable energy from stress and elastic compliance.
    double expected_energy = 0;
    auto current = probe.output_forces(run.state()).stress.read();
    for (unsigned c = 0; c < 6; ++c)
        expected_energy += (c < 3 ? .5 : 1.) * current(0, c) * current(0, c) / 100. / 6.;
    for (const auto &f : frame.fields)
        if (f.descriptor.name == "ALLSE")
            check(std::abs(f.values.at(0) - expected_energy) < (sizeof(T) == 4 ? 1e-8 : 1e-14),
                  "elastic energy after yield");
    bool found = false;
    for (const auto &f : frame.fields)
        if (f.descriptor.name == "PEEQ") {
            found = true;
            check(f.values.at(0) == before.read()(eq) && f.values.at(1) == 0,
                  "current PEEQ and independent points");
        }
    check(found, "published PEEQ");
    for (std::size_t i = 0; i < before.size(); ++i)
        check(before.read()(i) == run.state().data().history.read()(i), "read-only output");
    run.save(dir / "split.sdbrst");
    CpuRun<T> split(loaded);
    split.restore(dir / "split.sdbrst");
    while (run.advance())
        check(split.advance(), "split cursor");
    check(native::pack(run.snapshot()) == native::pack(split.snapshot()), "identical restart frames");
    for (std::size_t i = 0; i < before.size(); ++i)
        check(run.state().data().history.read()(i) == split.state().data().history.read()(i),
              "exact restart history");
    // A nonzero origin must be rejected even when it rounds to zero in FP32.
    ir.plastic[0].points[0].second = 1e-50;
    rejects([&] { ModelCompiler{}.compile<T>(ir); });
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture and scratch required");
        std::filesystem::create_directories(argv[2]);
        material_point<float>();
        material_point<double>();
        trajectory<float>(argv[1], argv[2]);
        trajectory<double>(argv[1], argv[2]);
        std::cout << "J2 FP32/FP64 material, INP, current history, publication, restart PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
