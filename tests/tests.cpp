#include "sdb/checkpoint.hpp"
#include "sdb/dynamics.hpp"
#include "sdb/implicit.hpp"
#include "sdb/database.hpp"
#include <iostream>
#include <thread>
#ifdef SDB_HAS_CUDA
#include "sdb/cuda_memory.hpp"
#endif

using namespace sdb;
static unsigned checks = 0;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F &&f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "operation should reject invalid input");
}
template <class T> bool close(T a, T b, T tolerance = T(1e-5)) {
    return std::abs(a - b) <= tolerance * (1 + std::abs(a) + std::abs(b));
}
void memory_fields() {
    const auto live = host_memory.live.load();
    {
        AlignedVector<double> a(100);
        require(reinterpret_cast<std::uintptr_t>(a.data()) % 64 == 0, "alignment");
    }
    require(host_memory.live == live, "RAII accounting");
    rejects([] { product(std::numeric_limits<std::size_t>::max(), 2); });
    rejects([] { sum(std::numeric_limits<std::size_t>::max(), 1); });
    Arena arena(128);
    require(reinterpret_cast<std::uintptr_t>(arena.allocate(3)) % 64 == 0, "arena alignment");
    arena.allocate(64);
    rejects([&] { arena.allocate(128); });
    require(arena.peak() == 128, "arena peak");
    arena.reset();
    require(arena.used() == 0, "arena reset");
    rejects([&] { arena.allocate(1, 3); });
    auto &scratch = thread_scratch(1024);
    scratch.reset();
    require(scratch.allocate<double>(4) != nullptr, "scratch");
    Field<double> parallel_field(400);
    auto disjoint = parallel_field.write();
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < 4; ++t)
        workers.emplace_back([=] {
            for (unsigned i = 100 * t; i < 100 * (t + 1); ++i)
                disjoint(i) = i;
        });
    for (auto &worker : workers)
        worker.join();
    require(parallel_field.read()(399) == 399, "phase-owned disjoint writes");
    Field<double, 3> f(3);
    auto w = f.write();
    w(1, 2) = 9;
    auto checked = f.checked_read();
    require(checked.at(1, 2) == 9, "field write");
    rejects([&] { checked.at(3); });
    f.resize(9);
    rejects([&] { checked.at(1, 2); });
    require(f.read()(1, 2) == 9, "SoA resize preservation");
    auto dead = [] {
        Field<double> x(1);
        return x.checked_read();
    }();
    rejects([&] { dead.at(0); });
    auto pre_move = f.checked_read();
    Field<double, 3> moved(std::move(f));
    rejects([&] { pre_move.at(1, 2); });
    require(moved.checked_read().at(1, 2) == 9, "move preserves contents");
    f.resize(1);
    require(f.checked_read().at(0) == 0, "moved-from field reusable");
    Field<float, 3, AoSoA<4>> tile(7);
    for (std::size_t i = 0; i < 7; ++i)
        for (unsigned c = 0; c < 3; ++c)
            tile.write()(i, c) = float(i * 10 + c);
    require(tile.storage_size() == 24, "AoSoA padding");
    tile.resize(11);
    require(tile.read()(6, 2) == 62, "AoSoA resize");
    FieldRegistry registry;
    registry.add<Field<float, 3>>("velocity", 5, "kinematic");
    registry.freeze();
    require(registry.get<Field<float, 3>>("velocity").size() == 5, "registry handle");
    rejects([&] { registry.get<Field<double>>("velocity"); });
    rejects([&] { registry.add<Field<float>>("x", 1); });
}
template <class T> void model_state() {
    auto mesh = oscillator_model<T>();
    require(mesh.node_index(40) == 3, "sparse IDs");
    require(mesh.connectivity(1)[2] == 4, "connectivity resolution");
    require(mesh.batches().size() == 1, "material grouping");
    require(mesh.batches()[0].connectivity[2] == 3, "fixed topology SoA");
    rejects([&] { mesh.node_index(123456789); });
    rejects([&] { mesh.validate_face({100, 4, 1}); });
    ModelBuilder<T> mixed;
    mixed.add_material({1, 1, {T(1), T(10), T(.2)}, 2});
    mixed.add_material({2, 42, {T(1), T(2)}, 7});
    mixed.add_part(1, 99);
    for (Id i = 0; i < 8; ++i)
        mixed.add_node(100000000000ull + i, {T(i), T(i % 2), T(0)}, 6);
    mixed.add_element(1, Topology::Beam2, {100000000000ull, 100000000001ull}, 1, 1, 2);
    mixed.add_element(2, Topology::Polygon,
                      {100000000002ull, 100000000003ull, 100000000004ull, 100000000005ull, 100000000006ull},
                      2, 1, 3);
    mixed.add_node_set(8, {100000000000ull});
    mixed.add_element_set(9, {2});
    mixed.constrain(100000000000ull, 5);
    mixed.add_surface(6, {{2, 0, -1}});
    auto mm = mixed.finalize();
    rejects([&] { mixed.add_part(3); });
    StateDB<T> st(mm, true);
    require(st.history_offsets().back() == 25, "variable material history");
    require(st.ip_offsets().back() == 5, "IP offsets");
    require(st.data().rotation.has_value(), "optional rotation");
    require(mm.node_sets()[0].nodes[0] == 0 && mm.element_sets()[0].elements[0] == 1, "sets");
    st.data().history.write()(0) = T(4);
    st.begin_trial();
    st.data().history.write()(0) = T(8);
    rejects([&] { st.begin_trial(); });
    st.rollback();
    require(st.data().history.read()(0) == 4, "rollback");
    st.begin_trial();
    st.data().history.write()(0) = T(9);
    st.commit();
    require(st.data().history.read()(0) == 9, "commit");
    rejects([&] { st.rollback(); });
    DofMap dofs(mm);
    require(dofs.equation(0, 5) == -1 && dofs.size() == 47, "DOF map");
    rejects([&] { dofs.equation(0, 6); });
    ModelBuilder<T> duplicate;
    duplicate.add_node(1, {0, 0, 0});
    duplicate.add_node(1, {1, 0, 0});
    rejects([&] { duplicate.finalize(); });
    ModelBuilder<T> invalid;
    invalid.add_node(1, {0, 0, 0});
    invalid.add_part(1);
    invalid.add_material({1, 1, {T(1), T(1), T(0)}, 0});
    invalid.add_element(1, Topology::Beam2, {1, 2}, 1, 1);
    rejects([&] { invalid.finalize(); });
    rejects([] {
        MaterialDB<T> m;
        m.add({1, 1, {T(1), T(1), T(.5)}, 0});
    });
}
void contacts() {
    ContactDB<double> db(8);
    ContactKey key{{100, 0, 1}, {200, 0, -1}, 2, 3};
    auto h = db.touch(key, 1);
    auto history = db.get(h);
    history.tangential_slip[0] = 2;
    db.update(h, history);
    require(db.get(h).tangential_slip[0] == 2, "contact history");
    auto back = key;
    back.a.side = -1;
    auto other = db.touch(back, 1);
    require(other.slot != h.slot, "shell side identity");
    auto speculative_key = key;
    speculative_key.feature_a = 999;
    db.begin_trial();
    auto speculative = db.touch(speculative_key, 2);
    db.rollback();
    auto replacement = db.touch(speculative_key, 2);
    require(speculative.generation != replacement.generation, "rollback cannot resurrect speculative handle");
    rejects([&] { db.get(speculative); });
    db.begin_trial();
    history.gap = -3;
    db.update(h, history);
    db.rollback();
    require(db.get(h).gap == 0, "contact rollback");
    require(db.prune(3) == 3, "batch prune");
    rejects([&] { db.get(h); });
    auto fresh = db.touch(key, 2);
    require(fresh.generation != h.generation, "generation recycle");
    CandidateBuffer<std::uint64_t> candidates(1000);
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            for (unsigned i = 0; i < 300; ++i)
                candidates.append(t * 300 + i);
        });
    for (auto &t : threads)
        t.join();
    require(candidates.entries().size() == 1000 && candidates.dropped() == 200, "bounded parallel append");
    std::unordered_set<std::uint64_t> seen(candidates.entries().begin(), candidates.entries().end());
    require(seen.size() == 1000, "publication after join");
    candidates.reset();
    std::array<std::uint64_t, 600> batch{};
    require(candidates.append_batch(batch), "batch reservation");
    require(!candidates.append_batch(batch) && candidates.entries().size() == 600 &&
                candidates.dropped() == 600,
            "batch overflow is all-or-nothing");
    threads.clear();
    for (unsigned t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            for (unsigned i = 0; i < 50; ++i) {
                auto k = key;
                k.feature_a = t * 50 + i;
                db.touch(k, 3);
            }
        });
    for (auto &t : threads)
        t.join();
    require(db.snapshot().size() == 200, "concurrent contact management");
}
void implicit() {
    Database<double> database(oscillator_model<double>());
    require(!database.has_implicit(), "no mandatory implicit allocation");
    database.begin_trial();
    database.state().data().displacement.write()(0) = 3;
    database.rollback();
    require(database.state().data().displacement.read()(0) == 0, "coordinated rollback");
    database.begin_trial();
    database.commit();
    require(database.enable_implicit().dofs.size() == 1, "implicit opt-in");
    require(database.state().current_view(database.model())(3, 0) == 1, "typed current coordinate");
    SparseMatrix<double> a(2, {0, 2, 4}, {0, 1, 0, 1});
    auto values = a.values();
    values[0] = 2;
    values[1] = -1;
    values[2] = -1;
    values[3] = 2;
    std::array<double, 2> x{1, 3}, y{};
    a.apply(x, y);
    require(y[0] == -1 && y[1] == 5, "CSR apply");
    rejects([&] { a.apply(x, x); });
    SparseMatrix<double, 2> b(1, {0, 1}, {0});
    auto v = b.values();
    v[0] = 2;
    v[3] = 3;
    b.apply(x, y);
    require(y[0] == 2 && y[1] == 9, "BSR apply");
    rejects([] { SparseMatrix<double> bad(1, {0, 2}, {0, 0}); });
    auto mesh = oscillator_model<double>();
    StateDB<double> state(mesh);
    ImplicitExtension<double> ext(mesh);
    require(!ext.tangent && ext.dofs.size() == 1, "optional implicit allocation");
    std::array<double, 1> in{3}, out{};
    ext.matrix_free([](const auto &, const auto &, const auto &, auto src, auto dst) { dst[0] = 2 * src[0]; },
                    in, out, mesh, state);
    require(out[0] == 6, "matrix-free contract");
}
template <class T> void numerics_restart() {
    auto mesh = oscillator_model<T>();
    StateDB<T> state(mesh);
    CpuExplicit<T> solver(mesh, state);
    const auto n = mesh.node_index(40);
    FaceTable faces(mesh);
    require(faces.identities.size() == 2 && faces.offsets.back() == 6, "surface connectivity table");
    // Affine strain patch, independent constitutive reference (nu=0).
    auto u = state.data().displacement.write();
    for (Index i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            u(i, c) = T(.001) * mesh.reference()(i, c);
    solver.forces(state);
    for (Index e = 0; e < 2; ++e)
        for (unsigned c = 0; c < 3; ++c)
            require(close(state.data().stress.read()(e, c), T(.1)), "affine patch stress");
    for (unsigned c = 0; c < 3; ++c) {
        T total = 0;
        for (Index i = 0; i < mesh.nodes(); ++i)
            total += state.data().internal_force.read()(i, c);
        require(std::abs(total) < T(1e-6), "internal force balance");
    }
    state.data().displacement.fill(T(.4));
    solver.forces(state);
    for (Index i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            require(std::abs(state.data().internal_force.read()(i, c)) < T(1e-5), "rigid translation");
    state.data().displacement.fill(0);
    state.data().displacement.write()(n, 0) = T(.01);
    solver.initialize(state, T(.0001));
    for (int i = 0; i < 200; ++i)
        solver.step(state);
    const T expected = T(.01) * std::cos(T(20) * T(state.data().time));
    require(std::abs(state.data().displacement.read()(n) - expected) < T(3e-7), "analytic oscillator");
#ifdef _OPENMP
    StateDB<T> parallel(mesh), atomic(mesh);
    CpuExplicit<T> p(mesh, parallel, CpuPolicy::OpenMP), a(mesh, atomic, CpuPolicy::OpenMP, Assembly::Atomic);
    parallel.data().displacement.write()(n) = T(.01);
    atomic.data().displacement.write()(n) = T(.01);
    p.initialize(parallel, T(.0001));
    a.initialize(atomic, T(.0001));
    for (int i = 0; i < 200; ++i) {
        p.step(parallel);
        a.step(atomic);
    }
    require(parallel.data().displacement.read()(n) == state.data().displacement.read()(n),
            "deterministic OpenMP");
    require(close(atomic.data().displacement.read()(n), state.data().displacement.read()(n)),
            "atomic assembly");
#endif
    const std::filesystem::path path = sizeof(T) == 4 ? "float.checkpoint" : "double.checkpoint";
    ContactDB<T> contact;
    contact.import_surfaces(mesh);
    auto handle = contact.touch({{100, 0, 1}, {200, 0, -1}, 0, 0}, state.data().step);
    auto history = contact.get(handle);
    history.gap = T(-.1);
    contact.update(handle, history);
    save_checkpoint(path, mesh, state, &contact);
    StateDB<T> resumed(mesh);
    CpuExplicit<T> resumed_solver(mesh, resumed);
    ContactDB<T> restored_contact;
    load_checkpoint(path, mesh, resumed, &restored_contact);
    require(restored_contact.snapshot()[0].gap == T(-.1), "contact restart");
    require(restored_contact.surfaces().size() == mesh.surfaces().size(), "restart surface binding");
    for (int i = 0; i < 200; ++i) {
        solver.step(state);
        resumed_solver.step(resumed);
    }
    for (Index i = 0; i < mesh.nodes(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            require(resumed.data().displacement.read()(i, c) == state.data().displacement.read()(i, c),
                    "restart continuation exact");
    auto before = resumed.data().displacement.read()(n);
    std::ifstream input(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    auto write = [&](const auto &b) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(b.data(), static_cast<std::streamsize>(b.size()));
    };
    bytes.back() ^= 1;
    write(bytes);
    rejects([&] { load_checkpoint(path, mesh, resumed, &restored_contact); });
    require(resumed.data().displacement.read()(n) == before, "failed restore transactional");
    bytes.resize(27);
    write(bytes);
    rejects([&] { load_checkpoint(path, mesh, resumed, &restored_contact); });
    state.begin_trial();
    rejects([&] { save_checkpoint(path, mesh, state); });
    state.rollback();
    save_checkpoint(path, mesh, state);
    rejects([&] { load_checkpoint(path, mesh, resumed, nullptr, 4); });
    std::filesystem::remove(path);
    state.data().velocity.resize(1);
    rejects([&] { solver.step(state); });
}
void checkpoint_edges() {
    auto mesh = oscillator_model<double>();
    StateDB<double> state(mesh, true);
    CpuExplicit<double> solver(mesh, state);
    solver.initialize(state, .0001);
    solver.step(state);
    state.data().rotation->angle.write()(0, 2) = .25;
    state.data().history.write()(0) = 3.5;
    const std::filesystem::path path = "edges.checkpoint";
    save_checkpoint(path, mesh, state);
    StateDB<double> restored(mesh);
    load_checkpoint(path, mesh, restored);
    require(restored.data().rotation->angle.read()(0, 2) == .25 && restored.data().history.read()(0) == 3.5,
            "optional state persistence");
    // Plan construction after restoration must not overwrite scaled mass/state.
    restored.data().mass.write()(0) *= 2;
    restored.data().inverse_mass.write()(0) /= 2;
    auto mass = restored.data().mass.read()(0);
    CpuExplicit<double> after_restart(mesh, restored);
    require(restored.data().mass.read()(0) == mass, "restart plan preserves mass");
    auto float_mesh = oscillator_model<float>();
    StateDB<float> float_state(float_mesh);
    rejects([&] { load_checkpoint(path, float_mesh, float_state); });
    ModelBuilder<double> builder;
    builder.add_node(1, {0, 0, 0});
    auto different = builder.finalize();
    StateDB<double> mismatch(different);
    rejects([&] { load_checkpoint(path, different, mismatch); });
    // A checksummed but semantically invalid mass must be rejected transactionally.
    state.data().inverse_mass.write()(0) = -1;
    save_checkpoint(path, mesh, state);
    rejects([&] { load_checkpoint(path, mesh, restored); });
    std::filesystem::remove(path);
    ModelBuilder<double> bad;
    bad.add_material({1, 1, {1, 100, 0}, 0});
    bad.add_part(1);
    bad.add_node(0, {0, 0, 0});
    bad.add_node(1, {1, 0, 0});
    bad.add_node(2, {0, 1, 0});
    bad.add_node(3, {0, 0, -1});
    bad.add_element(1, Topology::Tet4, {0, 1, 2, 3}, 1, 1);
    rejects([&] { bad.finalize(); });
}
#ifdef SDB_HAS_CUDA
void cuda_memory_contracts() {
    auto ctx = std::make_shared<cuda::Context>();
    cuda::Mirror<double> mirror(ctx, 1024);
    mirror.host_write()(0) = 42;
    rejects([&] { mirror.device_read(); });
    mirror.upload();
    rejects([&] { mirror.host_write(); });
    mirror.finish();
    require(mirror.residency() == cuda::Residency::Synced, "mirror upload");
    mirror.device_write();
    rejects([&] { mirror.host_read(); });
    mirror.download();
    rejects([&] { mirror.device_write(); });
    mirror.finish();
    require(mirror.host_read()(0) == 42, "mirror transfer roundtrip");
    cuda::Event event(ctx);
    event.record();
    auto other = std::make_shared<cuda::Context>();
    event.wait_on(other);
    other->wait();
    event.wait();
    require(event.ready(), "stream event dependency");
}
#endif
int main() {
    try {
        memory_fields();
        model_state<float>();
        model_state<double>();
        contacts();
        implicit();
        numerics_restart<float>();
        numerics_restart<double>();
        checkpoint_edges();
#ifdef SDB_HAS_CUDA
        cuda_memory_contracts();
#endif
        std::cout << "PASS " << checks << " contract checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
