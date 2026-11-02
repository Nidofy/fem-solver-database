#include <sdb/lifecycle.hpp>
#include <iostream>

using namespace sdb;
using namespace sdb::lifecycle;
namespace fs = std::filesystem;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected controls rejection");
}
void check_close(double a, double b, double tol, const char *message) {
    check(std::abs(a - b) <= tol, message);
}

template <class T> void mechanics(const fs::path &dir, const char *fixture) {
    auto model = ModelCompiler{}.compile<T>(ReaderRegistry{}.read(dir / fixture).model);
    StateDB<T> base(model.mesh), scaled(model.mesh);
    CpuExplicit<T> reference(model.mesh, base);
    ExplicitControls c;
    c.mass_factor = 4;
    CpuExplicit<T> heavy(model.mesh, scaled, CpuPolicy::Serial, Assembly::Deterministic, c);
    for (Index n = 0; n < model.mesh.nodes(); ++n) {
        check_close(scaled.data().mass.read()(n), 4 * base.data().mass.read()(n), 1e-6, "scaled mass");
        check_close(scaled.data().inverse_mass.read()(n), base.data().inverse_mass.read()(n) / 4, 1e-6,
                    "inverse mass");
    }
    check_close(heavy.plan().stable_step, 2 * reference.plan().stable_step, 1e-7, "global mass factor dt");
    c.mass_factor = 1;
    c.bulk_linear = .06;
    c.bulk_quadratic = 1.2;
    c.max_compression_rate = .5;
    StateDB<T> state(model.mesh);
    CpuExplicit<T> cpu(model.mesh, state, CpuPolicy::Serial, Assembly::Deterministic, c);
    rejects([&] { cpu.forces(base); });
    rejects([&] { reference.forces(state); });
    cpu.set_constraints({}, state);
    check(cpu.plan().stable_step < reference.plan().stable_step, "viscosity reduces dt");
    auto x = model.mesh.reference();
    for (double rate : {-2., -.25, 0., .25}) {
        auto v = state.data().velocity.write();
        for (Index n = 0; n < model.mesh.nodes(); ++n) {
            v(n, 0) = T(rate * x(n, 0) + .2 * x(n, 1));
            v(n, 1) = T(.3);
            v(n, 2) = 0;
        }
        cpu.forces(state);
#ifdef _OPENMP
        StateDB<T> parallel_state = state;
        CpuExplicit<T> parallel_cpu(model.mesh, parallel_state, CpuPolicy::OpenMP, Assembly::Deterministic,
                                    c);
        parallel_cpu.set_constraints({}, parallel_state);
        parallel_cpu.forces(parallel_state);
        for (Index n = 0; n < model.mesh.nodes(); ++n)
            for (unsigned i = 0; i < 3; ++i)
                check(parallel_state.data().internal_force.read()(n, i) ==
                          state.data().internal_force.read()(n, i),
                      "serial/OpenMP bulk force parity");
#endif
        double power = 0, expected = 0, sum[3]{};
        for (Index n = 0; n < model.mesh.nodes(); ++n)
            for (unsigned k = 0; k < 3; ++k) {
                double f = state.data().internal_force.read()(n, k);
                power += f * v(n, k);
                sum[k] += f;
            }
        for (Index e = 0; e < model.mesh.elements(); ++e) {
            auto &m = model.mesh.materials()[model.mesh.element(e).material];
            double rho = m.parameters[0], E = m.parameters[1], nu = m.parameters[2];
            double wave = std::sqrt(E * (1 - nu) / (rho * (1 + nu) * (1 - 2 * nu)));
            double V = cpu.plan().volume.read()(e), L = std::cbrt(V);
            expected +=
                V * rate * rate * rho * (.06 * wave * L + std::pow(1.2 * L, 2) * std::clamp(-rate, 0., .5));
        }
        check_close(power, expected, sizeof(T) == 4 ? 2e-5 : 1e-12, "affine volumetric damping power");
        for (double f : sum)
            check_close(f, 0, 1e-6, "force equilibrium");
        for (std::size_t ip = 0; ip < state.data().stress.size(); ++ip)
            for (unsigned k = 0; k < 6; ++k)
                check(state.data().stress.read()(ip, k) == 0, "physical stress unchanged");
    }
}

template <class T> void trajectory(const fs::path &dir, const fs::path &scratch) {
    auto ir = ReaderRegistry{}.read(dir / "forced_tet.inp").model;
    ir.steps[0].direct = false;
    ir.steps[0].increment = 0;
    auto model = ModelCompiler{}.compile<T>(ir);
    ExplicitControls c;
    c.refined_tet_dt = true;
    CpuRun<T> legacy(model), refined(model, c);
    double row_step = 1.9 / std::sqrt(2400.);
    check_close(refined.state().data().stable_dt.read()(0), row_step, 1e-7, "independent TET row sum");
    check(refined.plans()[0].increments < legacy.plans()[0].increments, "fewer automatic increments");
    while (refined.advance()) {
    }
    auto n = model.mesh.node_index(4); // compiled source label 40
    double u_exact = .06 * (1 - std::cos(4.));
    check_close(refined.state().data().displacement.read()(n, 0), u_exact, .003,
                "unscaled automatic displacement budget");
    std::cout << "auto increments " << legacy.plans()[0].increments << " -> " << refined.plans()[0].increments
              << '\n';
    c.mass_elements = {0};
    c.target_element_dt = 2 * row_step;
    CpuRun<T> selected(model, c);
    check_close(selected.controls().added_mass, .5, 1e-6, "only first tet gains mass");
    check_close(selected.state().data().mass.read()(model.mesh.node_index(1)), 5. / 24, 1e-6, "shared mass");
    check(selected.controls().controlling_element == 1, "unscaled neighbor controls");
    check_close(selected.state().data().stable_dt.read()(0), 2 * row_step, 1e-7, "local dt target");
    c.max_added_mass_fraction = 1.;
    rejects([&] { CpuRun<T> over_budget(model, c); });
    c.max_added_mass_fraction = 20.;
    c.bulk_linear = .06;
    c.bulk_quadratic = .5;
    c.max_compression_rate = 1;
    ir.steps[0].increment = .0002;
    auto forced = ModelCompiler{}.compile<T>(ir);
    CpuRun<T> full(forced, c), split(forced, c), wrong(forced);
    for (unsigned i = 0; i < 31; ++i)
        full.advance();
    full.save(scratch / "control.sdbrst");
    split.restore(scratch / "control.sdbrst");
    StateDB<T> raw(forced.mesh);
    rejects([&] { RestartDB::load(scratch / "control.sdbrst", forced, raw, full.configuration()); });
    CpuExplicit<T> bound(forced.mesh, raw, CpuPolicy::Serial, Assembly::Deterministic, c);
    RestartDB::load(scratch / "control.sdbrst", forced, raw, full.configuration());
    bound.step(raw);
    rejects([&] { wrong.restore(scratch / "control.sdbrst"); });
    while (full.advance())
        check(split.advance(), "restart cursor");
    check(native::pack(full.snapshot()) == native::pack(split.snapshot()), "exact controlled restart");
    rejects([&] { CpuExplicit<T> mismatch(forced.mesh, full.state()); });
    // Closed-form underdamped free vibration, and a work-energy budget for 0.2 s.
    auto osc = oscillator_model<T>();
    StateDB<T> damped(osc);
    ExplicitControls viscosity;
    viscosity.bulk_linear = .15;
    CpuExplicit<T> dynamics(osc, damped, CpuPolicy::Serial, Assembly::Deterministic, viscosity);
    auto free_node = osc.node_index(40);
    const double mass = 1. / 24, stiffness = 100. / 6, initial = .01;
    const double damping = viscosity.bulk_linear * 10 * std::cbrt(1. / 6) / 6;
    const double decay = damping / (2 * mass), frequency = std::sqrt(stiffness / mass - decay * decay);
    damped.data().displacement.write()(free_node, 0) = T(initial);
    const T step = T(.0001);
    dynamics.initialize(damped, step);
    double work = 0, energy = 0;
    for (unsigned k = 0; k < 2000; ++k) {
        dynamics.step(damped);
        double v = damped.data().velocity.read()(free_node, 0),
               u = damped.data().displacement.read()(free_node, 0);
        work += step * damping * v * v;
        energy = .5 * (mass * v * v + stiffness * u * u);
    }
    const double exact = initial * std::exp(-decay * .2) *
                         (std::cos(frequency * .2) + decay / frequency * std::sin(frequency * .2));
    check_close(damped.data().displacement.read()(free_node, 0), exact, 4e-6,
                "damped oscillator displacement");
    check_close((energy + work) / (.5 * stiffness * initial * initial), 1, .003, "damped work-energy budget");
    // Switching a displaced free node to a zero boundary fails after velocity rephasing.
    // The failed transition must be retryable from exactly the same final first-step frame.
    auto transition = ModelCompiler{}.compile<T>(ir);
    auto next_step = transition.analysis.steps.front();
    next_step.boundaries.push_back({n, 0, 0});
    transition.analysis.steps.push_back(next_step);
    auto output = transition.outputs.front();
    transition.outputs.push_back(output);
    CpuRun<T> two_steps(transition, viscosity);
    while (two_steps.increment() < two_steps.plans()[0].increments) two_steps.advance();
    const auto previous_frame = native::pack(two_steps.snapshot());
    for (unsigned retry = 0; retry < 2; ++retry) {
        rejects([&]{ two_steps.advance(); });
        check(two_steps.step_index() == 0 && native::pack(two_steps.snapshot()) == previous_frame,
              "failed viscous rephase restores cursor and state");
    }
    // The scaled, undamped one-DOF oscillator has twice the period and the same static compliance.
    c = {};
    c.mass_factor = 4;
    CpuRun<T> mass_run(forced, c);
    while (mass_run.advance()) {
    }
    check_close(mass_run.state().data().displacement.read()(n, 0), .06 * (1 - std::cos(2.)), 2e-6,
                "scaled oscillator");
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixtures and scratch required");
        fs::create_directories(argv[2]);
        for (auto f : {"forced_tet.inp", "forced_hex.inp", "forced_hex_reduced.inp", "mixed_hex_tet.inp"}) {
            mechanics<float>(argv[1], f);
            mechanics<double>(argv[1], f);
        }
        trajectory<float>(argv[1], argv[2]);
        trajectory<double>(argv[1], argv[2]);
        std::cout << "Explicit control contracts PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
