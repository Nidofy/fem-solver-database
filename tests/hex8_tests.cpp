#include <sdb/lifecycle.hpp>
#include <sdb/results/export.hpp>
#include <iostream>
#include <numbers>
using namespace sdb;
using namespace sdb::lifecycle;
void check(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
constexpr int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                              {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
template <class T> MeshDB<T> cube(bool mixed = false, int geometry = 1, bool oscillator = false) {
    ModelBuilder<T> b;
    b.add_material({1, 1, {T(2), T(100), oscillator ? T(0) : T(.25)}, 0});
    b.add_part(1);
    for (unsigned a = 0; a < 8; ++a) {
        T x = T(corner[a][0]), y = T(corner[a][1]), z = T(corner[a][2]);
        if (geometry == 2)
            b.add_node(a + 1, {x + T(.2) * y + T(.1) * z + (a == 6 ? T(.05) : T(0)),
                               T(.3) * x + T(1.3) * y + T(.1) * z, T(.1) * x + T(.2) * y + T(.9) * z});
        else
            b.add_node(a + 1, {geometry < 0 ? -x : x, y, geometry == 0 ? T(0) : z});
    }
    b.add_element(100, Topology::Hex8, {1, 2, 3, 4, 5, 6, 7, 8}, 1, 1, 8);
    if (mixed) {
        b.add_node(9, {T(-1), 0, 0});
        b.add_element(200, Topology::Tet4, {1, 4, 9, 5}, 1, 1, 1);
    }
    if (oscillator)
        for (unsigned a = 0; a < 8; ++a)
            for (unsigned c = 0; c < 3; ++c)
                if (a != 1 || c != 0)
                    b.constrain(a + 1, std::uint8_t(c));
    return b.finalize();
}
// Independent exact polynomial integral on the unit cube: integral dNa/dxi dNb/dxj.
double gram(unsigned a, unsigned i, unsigned b, unsigned j) {
    double value = (corner[a][i] ? 1. : -1.) * (corner[b][j] ? 1. : -1.);
    for (unsigned k = 0; k < 3; ++k) {
        if (k == i && k == j)
            continue;
        if (k == i || k == j)
            value *= .5;
        else
            value *= corner[a][k] == corner[b][k] ? 1. / 3 : 1. / 6;
    }
    return value;
}
template <class T> void mechanics() {
    const double tol = sizeof(T) == 4 ? 2e-5 : 1e-12;
    auto mesh = cube<T>();
    StateDB<T> s(mesh);
    CpuExplicit<T> cpu(mesh, s);
    double mass = 0;
    for (Index n = 0; n < mesh.nodes(); ++n)
        mass += s.data().mass.read()(n);
    check(std::abs(mass - 2) < tol, "HEX8 total row-sum mass");
    check(cpu.plan().stable_step > 0, "HEX8 stable step");
    auto u = s.data().displacement.write();
    for (Index n = 0; n < mesh.nodes(); ++n)
        for (unsigned c = 0; c < 3; ++c)
            u(n, c) = T(.001 * (c + 1));
    cpu.forces(s);
    for (std::size_t p = 0; p < 8; ++p)
        for (unsigned c = 0; c < 6; ++c)
            check(std::abs(s.data().stress.read()(p, c)) < tol, "rigid translation zero stress");
    for (Index n = 0; n < mesh.nodes(); ++n) {
        u(n, 0) = T(-.0001 * corner[n][1]);
        u(n, 1) = T(.0001 * corner[n][0]);
        u(n, 2) = 0;
    }
    cpu.forces(s);
    for (std::size_t p = 0; p < 8; ++p)
        for (unsigned c = 0; c < 6; ++c)
            check(std::abs(s.data().stress.read()(p, c)) < tol, "infinitesimal rigid rotation zero stress");
    const double A[3][3] = {{.001, .002, .003}, {.004, -.002, .005}, {.006, .007, .004}};
    for (Index n = 0; n < mesh.nodes(); ++n)
        for (unsigned i = 0; i < 3; ++i) {
            u(n, i) = 0;
            for (unsigned j = 0; j < 3; ++j)
                u(n, i) += T(A[i][j] * corner[n][j]);
        }
    cpu.forces(s);
    double eps[6] = {.001, -.002, .004, .003, .006, .0045};
    for (std::size_t p = 0; p < 8; ++p)
        for (unsigned c = 0; c < 6; ++c) {
            check(std::abs(s.data().strain.read()(p, c) - eps[c]) < tol, "eight-IP affine strain patch");
            check(std::abs(s.data().stress.read()(p, c) - (80 * eps[c] + (c < 3 ? .12 : 0))) < tol,
                  "eight-IP isotropic stress patch");
        }
    for (unsigned c = 0; c < 3; ++c) {
        double total = 0;
        for (Index n = 0; n < mesh.nodes(); ++n)
            total += s.data().internal_force.read()(n, c);
        check(std::abs(total) < tol, "force equilibrium");
    }
    for (unsigned a = 0; a < 8; ++a)
        for (unsigned c = 0; c < 3; ++c)
            u(a, c) = T(.001 * std::sin(double(a * 3 + c + 1)));
    cpu.forces(s);
    for (unsigned a = 0; a < 8; ++a)
        for (unsigned i = 0; i < 3; ++i) {
            double force = 0;
            for (unsigned b = 0; b < 8; ++b)
                for (unsigned j = 0; j < 3; ++j) {
                    double K = 40 * gram(a, i, b, j) + 40 * gram(a, j, b, i);
                    if (i == j)
                        for (unsigned k = 0; k < 3; ++k)
                            K += 40 * gram(a, k, b, k);
                    force += K * double(u(b, j));
                }
            check(std::abs(force - s.data().internal_force.read()(a, i)) < tol,
                  "independent polynomial stiffness matrix");
        }
    auto distorted = cube<T>(false, 2);
    StateDB<T> ds(distorted);
    CpuExplicit<T> dc(distorted, ds);
    auto dx = distorted.reference();
    auto du = ds.data().displacement.write();
    for (Index n = 0; n < distorted.nodes(); ++n)
        for (unsigned i = 0; i < 3; ++i) {
            du(n, i) = 0;
            for (unsigned j = 0; j < 3; ++j)
                du(n, i) += T(A[i][j]) * dx(n, j);
        }
    dc.forces(ds);
    for (std::size_t p = 0; p < 8; ++p)
        for (unsigned c = 0; c < 6; ++c)
            check(std::abs(ds.data().strain.read()(p, c) - eps[c]) < tol,
                  "skew warped HEX8 Jacobian transform patch");
    auto mixed = cube<T>(true);
    StateDB<T> ms(mixed);
    CpuExplicit<T> mc(mixed, ms);
    check(ms.ip_offsets() == std::vector<std::size_t>({0, 8, 9}), "mixed integration point offsets");
    auto mu = ms.data().displacement.write();
    auto x = mixed.reference();
    for (Index n = 0; n < mixed.nodes(); ++n)
        for (unsigned i = 0; i < 3; ++i) {
            mu(n, i) = 0;
            for (unsigned j = 0; j < 3; ++j)
                mu(n, i) += T(A[i][j]) * x(n, j);
        }
    mc.forces(ms);
    mass = 0;
    for (Index n = 0; n < mixed.nodes(); ++n)
        mass += ms.data().mass.read()(n);
    check(std::abs(mass - 7. / 3) < tol, "mixed shared-node mass");
    for (std::size_t p = 0; p < 9; ++p)
        for (unsigned c = 0; c < 6; ++c)
            check(std::abs(ms.data().strain.read()(p, c) - eps[c]) < tol, "mixed affine patch all IPs");
    for (unsigned c = 0; c < 3; ++c) {
        double total = 0;
        for (Index n = 0; n < mixed.nodes(); ++n)
            total += ms.data().internal_force.read()(n, c);
        check(std::abs(total) < tol, "mixed shared-node equilibrium");
    }
#ifdef _OPENMP
    auto reference = ms.data().internal_force;
    CpuExplicit<T> omp(mixed, ms, CpuPolicy::OpenMP);
    omp.forces(ms);
    for (Index n = 0; n < mixed.nodes(); ++n)
        for (unsigned c = 0; c < 3; ++c)
            check(reference.read()(n, c) == ms.data().internal_force.read()(n, c),
                  "mixed deterministic OpenMP exact");
    CpuExplicit<T> atomic(mixed, ms, CpuPolicy::OpenMP, Assembly::Atomic);
    atomic.forces(ms);
    for (Index n = 0; n < mixed.nodes(); ++n)
        for (unsigned c = 0; c < 3; ++c)
            check(std::abs(reference.read()(n, c) - ms.data().internal_force.read()(n, c)) < tol,
                  "mixed atomic OpenMP tolerance");
#endif
    ms.data().element_active.write()(0) = 0;
    mc.forces(ms);
    for (std::size_t p = 0; p < 8; ++p)
        for (unsigned c = 0; c < 6; ++c)
            check(ms.data().stress.read()(p, c) == 0, "inactive hex clears all IPs");
    rejects([&] {
        auto bad = cube<T>(false, -1);
        StateDB<T> state(bad);
        CpuExplicit<T> kernel(bad, state);
    });
    rejects([&] {
        auto bad = cube<T>(false, 0);
        StateDB<T> state(bad);
        CpuExplicit<T> kernel(bad, state);
    });
    // The legacy CUDA/TET plan must continue rejecting HEX rather than silently running four nodes.
    rejects([&] {
        StateDB<T> state(mesh);
        TetPlan<T> tet(mesh, state);
    });
}
double temporal(double dt) {
    auto mesh = cube<double>(false, 1, true);
    StateDB<double> s(mesh);
    CpuExplicit<double> cpu(mesh, s);
    s.data().displacement.write()(1, 0) = .0001;
    cpu.initialize(s, dt);
    auto steps = std::llround(.1 / dt);
    for (long long i = 0; i < steps; ++i)
        cpu.step(s);
    auto omega = std::sqrt((200. / 9) / (.25));
    return std::abs(s.data().displacement.read()(1, 0) - .0001 * std::cos(omega * .1));
}
double spatial(unsigned cells) {
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 100, 0}, 0});
    b.add_part(1);
    for (unsigned layer = 0; layer <= cells; ++layer)
        for (unsigned a = 0; a < 4; ++a) {
            Id id = 4 * layer + a + 1;
            b.add_node(id, {double(layer) / cells, double(a % 2), double(a / 2)});
            b.constrain(id, 1);
            b.constrain(id, 2);
            if (!layer)
                b.constrain(id, 0);
        }
    for (unsigned e = 0; e < cells; ++e) {
        Id l = 4 * e + 1, r = l + 4;
        b.add_element(e + 1, Topology::Hex8, {l, r, r + 1, l + 1, l + 2, r + 2, r + 3, l + 3}, 1, 1, 8);
    }
    auto mesh = b.finalize();
    StateDB<double> s(mesh);
    CpuExplicit<double> cpu(mesh, s);
    for (Index n = 0; n < mesh.nodes(); ++n)
        s.data().displacement.write()(n, 0) = .0001 * std::sin(std::numbers::pi * mesh.reference()(n, 0) / 2);
    cpu.initialize(s, .0001);
    for (unsigned i = 0; i < 1000; ++i)
        cpu.step(s);
    return std::abs(s.data().displacement.read()(mesh.node_index(4 * cells + 1), 0) -
                    .0001 * std::cos(5 * std::numbers::pi * .1));
}
template <class T>
void hex_lifecycle(const std::filesystem::path &fixture, const std::filesystem::path &dir) {
    auto model = ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    check(model.report.executable_cpu, "hex executable capability");
    CpuRun<T> full(model);
    full.initialize();
    auto output = dir / (fixture.stem().string() + std::to_string(sizeof(T)) + ".sdbres");
    std::filesystem::remove(output);
    auto rst = output;
    rst += ".sdbrst";
    {
        NativeResultStorage storage(output, ResultModel::from(model, full.configuration()), false, false,
                                    native::default_limit, {}, true);
        ResultWriter writer(storage);
        writer.write(full.snapshot());
        while (full.advance()) {
            writer.write(full.snapshot());
            if (full.increment() == 333)
                full.save(rst);
        }
    }
    double reference = .000045 * (1 - std::cos((40. / 3) * .1));
    auto node = model.sources.lookup(true, {"", "", 2}).index;
    check(std::abs(double(full.state().data().displacement.read()(node, 0)) - reference) <
              (sizeof(T) == 4 ? 2e-10 : 2e-11),
          "hex analytic forced response");
    CpuRun<T> split(model);
    split.restore(rst);
    while (split.advance()) {
    }
    check(native::pack(full.snapshot()) == native::pack(split.snapshot()), "mixed all-IP exact restart");
    ResultReader reader(output);
    auto last = reader.index.size() - 1;
    check(reader.field(last, "S", "", false, {}, 8).ids.size() == 1, "IP8 belongs to hex only");
    check(reader.field(last, "S").ids.size() == model.mesh.elements(), "IP1 includes mixed types");
    check(ResultQuery::history(reader, "S", 1, 0, "", true, 0, 1, 8).size() == 21, "IP8 history sampling");
    ResultExporter::vtk(reader, dir / (fixture.stem().string() + std::to_string(sizeof(T)) + "-vtk"));
    ResultExporter::xdmf(reader, output.string() + ".xdmf");
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture scratch");
        std::filesystem::path dir = argv[2];
        std::filesystem::create_directories(dir);
        mechanics<float>();
        mechanics<double>();
        double t1 = temporal(.002), t2 = temporal(.001), t3 = temporal(.0005);
        check(t2 < t1 * .3 && t3 < t2 * .3, "second-order time convergence");
        double h1 = spatial(2), h2 = spatial(4), h3 = spatial(8);
        check(h2 < h1 * .3 && h3 < h2 * .3, "axial mesh convergence");
        for (auto file : {"forced_hex.inp", "mixed_hex_tet.inp"}) {
            hex_lifecycle<float>(std::filesystem::path(argv[1]) / file, dir);
            hex_lifecycle<double>(std::filesystem::path(argv[1]) / file, dir);
        }
        std::cout << "HEX8 float/double mechanics mixed/IP/restart PASS time_errors=" << t1 << ',' << t2
                  << ',' << t3 << " mesh_errors=" << h1 << ',' << h2 << ',' << h3 << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
