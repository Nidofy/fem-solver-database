#include "sdb/general_contact/search.hpp"
#include "sdb/general_contact/io.hpp"
#include "sdb/general_contact/model.hpp"
#include "sdb/general_contact/bridge.hpp"
#include "sdb/general_contact/results.hpp"
#include "sdb/dynamics.hpp"
#include <iostream>
#include <fstream>
#ifdef _OPENMP
#include <omp.h>
#endif
namespace gc = sdb::general_contact;
using namespace sdb;
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool caught = false;
    try {
        fn();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, "expected rejection");
}
bool approximately_equal(double a, double b, double tol = 1e-8) {
    return std::abs(a - b) <= tol * (1 + std::abs(a) + std::abs(b));
}
template <class T> gc::Witness<T> witness() {
    gc::Witness<T> w;
    w.key = {1, {gc::FeatureKind::Node, 10, 0, 1}, {gc::FeatureKind::Node, 20, 0, 1}, 0};
    w.nodes = {0, 1};
    w.weights = {1, -1};
    w.count = 2;
    w.gap = T(-.01);
    w.area = T(2);
    w.geometry_epoch = 7;
    return w;
}
MeshDB<double> nodes_mesh() {
    ModelBuilder<double> b;
    b.add_node(10, {0, 0, 0});
    b.add_node(20, {0, 0, 1});
    return b.finalize();
}
void topology_and_search() {
    auto two = oscillator_model<double>();
    auto ext = gc::exterior(two);
    check(ext.size() == 6, "interior facet cancellation");
    for (std::size_t i = 1; i < ext.size(); ++i)
        check(ext[i - 1].identity < ext[i].identity, "stable exterior order");
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 10, .2}, 0});
    b.add_part(1);
    std::vector<gc::Vec<double>> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (Id i = 0; i < positions.size(); ++i)
        b.add_node(i + 1, positions[i]);
    std::vector<gc::SearchPoint<double>> points;
    for (unsigned i = 0; i < 25; ++i)
        for (unsigned j = 0; j < 25; ++j) {
            auto node = index_of(positions.size());
            positions.push_back({double(i) / 20 - .1, double(j) / 20 - .1, .01});
            b.add_node(node + 1, positions.back());
            points.push_back({node, .1});
        }
    b.add_element(1, Topology::Tet4, {1, 2, 3, 4}, 1, 1);
    auto mesh = b.finalize();
    auto faces = gc::exterior(mesh);
    faces.resize(1);
    check(faces[0].identity.local == 0, "selected horizontal face");
    auto serial =
        gc::detect_node_triangle<double>(mesh, faces, points, positions, positions, .02, 1, 1, 7, 1000);
    std::set<Id> actual;
    for (const auto &w : serial.witnesses) {
        actual.insert(w.key.a.entity);
        check(approximately_equal(w.gap, -.01), "signed normal gap");
    }
    // Analytical oracle on z=0, x>=0,y>=0,x+y<=1, excluding roundoff-sensitive boundary.
    for (const auto &p : points) {
        const auto &x = positions[p.node];
        if (std::abs(x[0]) < 1e-12 || std::abs(x[1]) < 1e-12 || std::abs(x[0] + x[1] - 1) < 1e-12)
            continue;
        check(actual.contains(mesh.node_id(p.node)) == (x[0] > 0 && x[1] > 0 && x[0] + x[1] < 1),
              "search versus independent analytical oracle");
    }
    check(serial.statistics.box_candidates >= serial.statistics.accepted, "search counters");
#ifdef _OPENMP
    omp_set_num_threads(4);
    auto parallel = gc::detect_node_triangle<double>(mesh, faces, points, positions, positions, .02, 1, 1, 7,
                                                     1000, gc::Execution::OpenMP);
    check(parallel.witnesses.size() == serial.witnesses.size(), "parallel detection size");
    for (std::size_t i = 0; i < serial.witnesses.size(); ++i)
        check(parallel.witnesses[i].key == serial.witnesses[i].key &&
                  parallel.witnesses[i].weights == serial.witnesses[i].weights,
              "parallel detection identity/geometry");
#endif
    rejects([&] {
        gc::detect_node_triangle<double>(mesh, faces, points, positions, positions, .02, 1, 1, 7, 1);
    });
    std::set<gc::Key> exclusions;
    for (const auto &w : serial.witnesses)
        exclusions.insert(w.key);
    check(gc::detect_node_triangle<double>(mesh, faces, points, positions, positions, .02, 1, 1, 7, 1000,
                                           gc::Execution::Serial, exclusions)
              .witnesses.empty(),
          "explicit exclusion rule");
    auto bad = positions;
    bad[2] = bad[1];
    rejects(
        [&] { gc::detect_node_triangle<double>(mesh, faces, points, bad, positions, .02, 1, 1, 7, 1000); });
    auto badfaces = faces;
    badfaces.push_back(faces[0]);
    rejects([&] {
        gc::detect_node_triangle<double>(mesh, badfaces, points, positions, positions, .02, 1, 1, 7, 1000);
    });
    gc::Database<double> db({mesh.fingerprint(), 1, 0, 0, 0});
    db.begin_trial();
    std::vector<gc::Property<double>> laws{{1, 1000, 0, 0}};
    auto frame = gc::evaluate<double>(db, serial.witnesses, laws, mesh.nodes(), 7, gc::Analysis::Explicit);
    check(approximately_equal(frame.monitor.force_balance[2], 0, 1e-10), "search to force action/reaction");
    db.rollback();
}
template <class T> void laws_and_transactions() {
    auto w = witness<T>();
    gc::Property<T> p{1, T(1000), T(100), T(.3)};
    p.validate();
    w.validate(2, 7);
    auto normal = gc::penalty(w, p, gc::History<T>{});
    normal.validate();
    check(approximately_equal(normal.traction[2], 10, 1e-5), "normal penalty traction");
    check(approximately_equal(normal.elastic_energy, .1, 1e-5), "normal stored energy");
    for (T dx : {T(.001), T(.2)}) {
        w.relative_increment = {dx, T(.0003), 0};
        auto r = gc::penalty(w, p, gc::History<T>{});
        r.validate();
        check(r.history.status == (dx < T(.1) ? gc::Status::Stick : gc::Status::Slip),
              "stick/slip transition");
        const T eps = std::is_same_v<T, float> ? T(1e-5) : T(1e-7);
        for (unsigned j = 0; j < 3; ++j) {
            auto a = w, b = w;
            a.relative_increment[j] += eps;
            b.relative_increment[j] -= eps;
            a.gap += eps * w.normal[j];
            b.gap -= eps * w.normal[j];
            auto ra = gc::penalty(a, p, gc::History<T>{}), rb = gc::penalty(b, p, gc::History<T>{});
            for (unsigned i = 0; i < 3; ++i)
                check(approximately_equal((ra.traction[i] - rb.traction[i]) / (2 * eps),
                                          r.derivative[i * 3 + j], std::is_same_v<T, float> ? .015 : 1e-6),
                      "finite difference full frozen-geometry derivative");
        }
        if (dx > T(.1)) {
            check(r.friction_increment > 0, "positive friction dissipation");
            check(std::abs(r.derivative[2]) > 0, "sliding normal/tangential coupling");
        }
    }
    std::vector<gc::Property<T>> props{p};
    gc::Binding binding{123, gc::property_fingerprint(props), 456, 0, 0};
    gc::Database<T> db(binding);
    rejects([&] { db.stage({}); });
    db.begin_trial();
    rejects([&] { db.begin_trial(); });
    auto frame = gc::evaluate<T>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    auto first = db.trial().at(0);
    gc::evaluate<T>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    check(db.trial().at(0).accumulated_slip == first.accumulated_slip,
          "Newton trials must not accumulate slip repeatedly");
    check(approximately_equal(frame.nodal_force[0][2] + frame.nodal_force[1][2], 0),
          "equal and opposite nodal force");
    db.rollback();
    check(db.committed().size() == 0, "rollback new contacts");
    db.begin_trial();
    gc::evaluate<T>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    auto next = binding;
    next.step = 1;
    next.time = .1;
    next.mechanical_state = 789;
    db.commit(next);
    check(db.committed().at(0).accumulated_slip == first.accumulated_slip, "accepted history commit");
    db.begin_trial();
    w.gap = T(.1);
    gc::evaluate<T>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    check(db.trial().at(0).status == gc::Status::Open &&
              gc::dot(db.trial().at(0).elastic_slip, db.trial().at(0).elastic_slip) == 0,
          "separation clears elastic traction");
    db.rollback();
    check(db.committed().at(0).status == gc::Status::Slip, "rollback preserves closed history");
    db.begin_trial();
    auto bad = w;
    bad.normal = {1, 0, 0};
    rejects([&] { gc::evaluate<T>(db, {bad}, props, 2, 7, gc::Analysis::Explicit); });
    rejects([&] { (void)db.trial(); });
    db.rollback();
    db.begin_trial();
    gc::evaluate<T>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    bad = w;
    bad.weights[1] = T(-.5);
    rejects([&] { gc::evaluate<T>(db, {bad}, props, 2, 7, gc::Analysis::Explicit); });
    next.step = 2;
    rejects([&] { db.commit(next); });
    db.rollback();
    rejects([&] { gc::HistoryTable<T> table({{w.key, first}, {w.key, first}}); });
    const T dissipated = db.friction_dissipation();
    check(dissipated > 0, "accepted global friction ledger");
    db.begin_trial();
    db.stage({});
    next = db.binding();
    ++next.step;
    next.time += .1;
    db.commit(next);
    check(db.committed().size() == 0 && db.friction_dissipation() == dissipated,
          "retiring contact points preserves cumulative dissipation");
}
void implicit_and_explicit() {
    auto mesh = nodes_mesh();
    DofMap dofs(mesh);
    gc::Database<double> db({mesh.fingerprint(), 1, 0, 0, 0});
    std::vector<gc::Property<double>> props{{1, 1000, 0, 0}};
    auto w = witness<double>();
    w.area = 1;
    db.begin_trial();
    double u = 0;
    unsigned iterations = 0;
    for (; iterations < 8; ++iterations) {
        w.gap = .1 + u;
        w.relative_increment = {0, 0, u};
        auto f =
            gc::evaluate<double>(db, {w}, props, 2, 7, gc::Analysis::Implicit, gc::Execution::Serial, &dofs);
        const auto eq = dofs.equation(0, 2);
        double residual = 2 * u + 1 + f.residual[static_cast<std::size_t>(eq)], tangent = 2;
        for (const auto &e : f.tangent)
            if (e.row == eq && e.column == eq)
                tangent += e.value;
        if (std::abs(residual) < 1e-12)
            break;
        u -= residual / tangent;
    }
    check(iterations < 8 && approximately_equal(u, -101. / 1002.), "implicit contact Newton equilibrium");
    check(approximately_equal(2 * u + 1 + 1000 * (.1 + u), 0), "independent static equilibrium");
    db.rollback();
    // Undamped unit mass impact on a fixed plane: leapfrog, analytic half-period pi/sqrt(k).
    double x = 0, v = -1, dt = 1e-4;
    const double initial_energy = .5;
    double max_drift = 0;
    unsigned step = 0;
    for (; step < 2000; ++step) {
        w.gap = x;
        w.relative_increment = {};
        db.begin_trial();
        auto f = gc::evaluate<double>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
        double a = f.nodal_force[0][2];
        v += .5 * dt * a;
        x += dt * v;
        w.gap = x;
        f = gc::evaluate<double>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
        v += .5 * dt * f.nodal_force[0][2];
        double energy = .5 * v * v + f.monitor.elastic_energy;
        max_drift = std::max(max_drift, std::abs(energy - initial_energy));
        auto b = db.binding();
        ++b.step;
        b.time += dt;
        db.commit(b);
        if (step > 1 && x >= 0)
            break;
    }
    check(step < 2000 && approximately_equal(v, 1, 1e-5), "explicit elastic rebound");
    check(std::abs((step + 1) * dt - std::acos(-1.) / std::sqrt(1000.)) < 2 * dt,
          "explicit impact contact duration");
    check(max_drift < 2e-6, "explicit impact energy drift");
    w.gap = -.01;
    std::vector<gc::Witness<double>> ws{w};
    std::vector<double> inv{1, 0};
    const double one = gc::penalty_step_bound<double>(ws, props, inv, 7);
    auto second = w;
    second.key.point = 1;
    ws.push_back(second);
    check(approximately_equal(gc::penalty_step_bound<double>(ws, props, inv, 7), one / std::sqrt(2.)),
          "multiple constraints tighten assembled contact timestep");
    props[0].friction = .2;
    props[0].tangential_penalty = 100;
    rejects([&] { gc::penalty_step_bound<double>(ws, props, inv, 7); });
}
void assembly_and_parallel() {
    auto mesh = nodes_mesh();
    DofMap dofs(mesh);
    std::vector<gc::Property<double>> props{{1, 1000, 100, .3}};
    std::vector<gc::Witness<double>> ws;
    for (unsigned i = 0; i < 257; ++i) {
        auto w = witness<double>();
        w.key.point = i;
        w.relative_increment = {.01 + double(i % 7) * .03, 0, 0};
        ws.push_back(w);
    }
    gc::Database<double> db({mesh.fingerprint(), 1, 0, 0, 0});
    db.begin_trial();
    auto a = gc::evaluate<double>(db, ws, props, 2, 7, gc::Analysis::Implicit, gc::Execution::Serial, &dofs);
#ifdef _OPENMP
    for (int threads : {1, 4}) {
        omp_set_num_threads(threads);
        auto b =
            gc::evaluate<double>(db, ws, props, 2, 7, gc::Analysis::Implicit, gc::Execution::OpenMP, &dofs);
        check(a.nodal_force == b.nodal_force && a.residual == b.residual,
              "bitwise parallel deterministic nodal assembly");
        check(a.tangent.size() == b.tangent.size(), "parallel tangent shape");
        for (std::size_t i = 0; i < a.tangent.size(); ++i)
            check(a.tangent[i].row == b.tangent[i].row && a.tangent[i].column == b.tangent[i].column &&
                      a.tangent[i].value == b.tangent[i].value,
                  "bitwise parallel tangent reduction");
    }
#else
    rejects([&] {
        gc::evaluate<double>(db, ws, props, 2, 7, gc::Analysis::Implicit, gc::Execution::OpenMP, &dofs);
    });
#endif
    // Assembled residual derivative, including slip pressure coupling and both body signs.
    const double eps = 1e-7;
    for (unsigned col = 0; col < 6; ++col) {
        auto plus = ws, minus = ws;
        unsigned node = col / 3, c = col % 3;
        const double sign = node == 0 ? 1. : -1.;
        for (auto &w : plus) {
            w.relative_increment[c] += sign * eps;
            w.gap += sign * eps * w.normal[c];
        }
        for (auto &w : minus) {
            w.relative_increment[c] -= sign * eps;
            w.gap -= sign * eps * w.normal[c];
        }
        auto fp =
            gc::evaluate<double>(db, plus, props, 2, 7, gc::Analysis::Implicit, gc::Execution::Serial, &dofs);
        auto fm = gc::evaluate<double>(db, minus, props, 2, 7, gc::Analysis::Implicit, gc::Execution::Serial,
                                       &dofs);
        for (unsigned row = 0; row < 6; ++row) {
            double k = 0;
            for (const auto &e : a.tangent)
                if (e.row == row && e.column == col)
                    k = e.value;
            check(approximately_equal((fp.residual[row] - fm.residual[row]) / (2 * eps), k, 1e-6),
                  "assembled tangent finite difference");
        }
    }
    auto bad = ws;
    bad.push_back(ws[0]);
    rejects([&] { gc::evaluate<double>(db, bad, props, 2, 7, gc::Analysis::Explicit); });
    db.rollback();
}
void persistence(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    auto path = dir / "contact.sdbc";
    std::vector<gc::Property<double>> props{{1, 1000, 100, .3}};
    gc::Binding b{123, gc::property_fingerprint(props), 987, 1, .1};
    gc::Database<double> db(b);
    db.begin_trial();
    auto w = witness<double>();
    w.relative_increment = {.2, 0, 0};
    gc::evaluate<double>(db, {w}, props, 2, 7, gc::Analysis::Explicit);
    rejects([&] { gc::save(path, db); });
    b.step = 2;
    b.time = .2;
    b.mechanical_state = 998;
    db.commit(b);
    gc::save(path, db);
    gc::Database<double> restored(b);
    gc::load(path, restored);
    check(restored.friction_dissipation() == db.friction_dissipation(), "restart global dissipation ledger");
    check(lifecycle::native::pack(restored.committed().records()) ==
              lifecycle::native::pack(db.committed().records()),
          "exact contact history restart");
    db.begin_trial();
    restored.begin_trial();
    w.relative_increment = {.01, .02, 0};
    auto a = gc::evaluate<double>(db, {w}, props, 2, 7, gc::Analysis::Explicit),
         r = gc::evaluate<double>(restored, {w}, props, 2, 7, gc::Analysis::Explicit);
    check(a.nodal_force == r.nodal_force && lifecycle::native::pack(db.trial().records()) ==
                                                lifecycle::native::pack(restored.trial().records()),
          "restart continuation equality");
    db.rollback();
    restored.rollback();
    auto wrong = b;
    wrong.mechanical_state++;
    gc::Database<double> mismatch(wrong);
    rejects([&] { gc::load(path, mismatch); });
    wrong = b;
    wrong.configuration++;
    gc::Database<double> badconfig(wrong);
    rejects([&] { gc::load(path, badconfig); });
    rejects([&] { gc::load(path, restored, 8); });
    rejects([&] { gc::save(path, restored, 8); });
    gc::load(path, restored); // failed bounded save preserved the previous complete file
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(-1, std::ios::end);
        file.put('\x7f');
    }
    const auto before = lifecycle::native::pack(restored.committed().records());
    rejects([&] { gc::load(path, restored); });
    check(before == lifecycle::native::pack(restored.committed().records()),
          "corrupt restart leaves database unchanged");
}
void model_bridge_results(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    auto mesh = oscillator_model<double>();
    gc::Definition<double> definition;
    definition.mesh = mesh.fingerprint();
    definition.properties = {{1, 1000, 0, 0}, {2, 2000, 0, 0}};
    definition.rules = {{1, 0, 0, 1, 0}};
    definition.exclude_shared_nodes = false;
    auto faces = gc::exterior(mesh);
    auto feature = [](FaceId f) { return gc::Feature{gc::FeatureKind::Face, f.element, f.local, f.side}; };
    definition.regions = {{1, {feature(faces[0].identity)}}, {2, {feature(faces[1].identity)}}};
    gc::Model<double> model(mesh, definition);
    check(model.facets().size() == 6 && !model.edges().empty(), "compiled contact topology and adjacency");
    check(model.resolve(0, 1)->property == 1 && !model.resolve(0, 0),
          "default inclusion and same-face exclusion");
    auto specific = definition;
    specific.rules.push_back({2, 1, 2, 2, 10});
    gc::Model<double> assigned(mesh, specific);
    check(assigned.resolve(0, 1)->property == 2 && assigned.resolve(1, 0)->property == 2,
          "symmetric prioritized assignment");
    specific.rules.push_back({3, 1, 2, 0, -10});
    gc::Model<double> excluded(mesh, specific);
    check(!excluded.resolve(0, 1), "exclusion overrides any inclusion priority");
    auto conflict = definition;
    conflict.rules.push_back({2, 0, 0, 2, 0});
    gc::Model<double> ambiguous(mesh, conflict);
    rejects([&] { ambiguous.resolve(0, 1); });
    auto invalid = definition;
    invalid.rules[0].region_a = 99;
    rejects([&] { gc::Model<double> bad(mesh, invalid); });
    invalid = definition;
    invalid.regions[0].faces.push_back(invalid.regions[0].faces[0]);
    rejects([&] { gc::Model<double> bad(mesh, invalid); });
    auto model_path = dir / "contact.sdbcm";
    gc::save_model(model_path, model);
    auto loaded = gc::load_model<double>(model_path, mesh);
    check(loaded.fingerprint() == model.fingerprint(), "contact model persistence");
    std::reverse(definition.properties.begin(), definition.properties.end());
    std::reverse(definition.regions.begin(), definition.regions.end());
    gc::Model<double> reordered(mesh, definition);
    check(reordered.fingerprint() == model.fingerprint(), "canonical definition fingerprint");
    auto other = nodes_mesh();
    rejects([&] { gc::load_model<double>(model_path, other); });
    StateDB<double> state(other);
    gc::Database<double> db(gc::bind(state, 987));
    auto w = witness<double>();
    std::vector<gc::Property<double>> laws{{1, 1000, 0, 0}};
    const auto origin = gc::state_fingerprint(state);
    {
        gc::CoupledTrial<double> trial(state, db);
        auto frame = gc::evaluate<double>(db, {w}, laws, other.nodes(), 7, gc::Analysis::Explicit);
        trial.apply_force(frame);
        state.data().displacement.write()(0, 2) = -.01;
        check(state.data().contact_force.read()(0, 2) > 0, "V2 contact_force bridge");
    }
    check(gc::state_fingerprint(state) == origin && db.committed().size() == 0,
          "RAII coupled state/history rollback");
    gc::Frame<double> accepted;
    {
        gc::CoupledTrial<double> trial(state, db);
        auto old = gc::evaluate<double>(db, {w}, laws, other.nodes(), 7, gc::Analysis::Explicit);
        accepted = gc::evaluate<double>(db, {w}, laws, other.nodes(), 7, gc::Analysis::Explicit);
        rejects([&] { trial.apply_force(old); });
        trial.apply_force(accepted);
        rejects([&] { gc::accepted_result<double>(db, accepted, {w}); });
        state.data().step = 1;
        state.data().time = .1;
        state.data().dt = .1;
        trial.commit();
    }
    check(db.binding() == gc::bind(state, 987), "committed exact mechanical state binding");
    save_checkpoint(dir / "paired-mechanical.sdbrst", other, state);
    gc::save(dir / "paired-contact.sdbc", db);
    StateDB<double> recovered_state(other);
    load_checkpoint(dir / "paired-mechanical.sdbrst", other, recovered_state);
    gc::Database<double> recovered_contact(gc::bind(recovered_state, 987));
    gc::load(dir / "paired-contact.sdbc", recovered_contact);
    check(gc::state_fingerprint(recovered_state) == gc::state_fingerprint(state) &&
              recovered_contact.committed().keys() == db.committed().keys(),
          "paired mechanical/contact disk restart");
    recovered_state.data().dt = 0;
    rejects([&] { save_checkpoint(dir / "paired-mechanical.sdbrst", other, recovered_state); });
    load_checkpoint(dir / "paired-mechanical.sdbrst", other, recovered_state);
    check(gc::state_fingerprint(recovered_state) == gc::state_fingerprint(state),
          "invalid metadata save preserves prior checkpoint");
    auto result = gc::accepted_result<double>(db, accepted, {w});
    auto path = dir / "contact.sdbcr";
    gc::save_result(path, result);
    auto reread = gc::load_result<double>(path, db.binding());
    check(reread.rows[0].key == w.key && reread.rows[0].traction[2] == 20 / 2.,
          "independent contact result location/query");
    rejects([&] { gc::load_result<float>(path, db.binding()); });
    auto wrong = db.binding();
    wrong.step++;
    rejects([&] { gc::load_result<double>(path, wrong); });
    {
        gc::CoupledTrial<double> trial(state, db);
        trial.rollback();
    }
    rejects([&] { gc::accepted_result<double>(db, accepted, {w}); });
    state.data().displacement.write()(0, 0) = 1;
    rejects([&] { gc::CoupledTrial<double> mismatched(state, db); });
}
void geometry_and_constraint_edges() {
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 1, 0}, 0});
    b.add_part(1);
    const std::array<gc::Vec<double>, 8> cube{
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    for (Id i = 0; i < 8; ++i)
        b.add_node(i + 1, cube[i]);
    b.add_element(1, Topology::Hex8, {1, 2, 3, 4, 5, 6, 7, 8}, 1, 1);
    auto mesh = b.finalize();
    auto faces = gc::exterior(mesh);
    check(faces.size() == 6, "Hex8 exterior topology");
    gc::Definition<double> def;
    def.mesh = mesh.fingerprint();
    gc::Model<double> cm(mesh, def);
    check(cm.edges().size() == 12, "Hex8 edge incidence");
    for (const auto &edge : cm.edges())
        check(edge.incident_faces.size() == 2, "closed manifold edge adjacency");
    ModelBuilder<double> constrained;
    constrained.add_node(10, {0, 0, 0});
    constrained.add_node(20, {0, 0, 1});
    for (unsigned c = 0; c < 3; ++c)
        constrained.constrain(20, static_cast<std::uint8_t>(c));
    constrained.constrain(10, 0);
    constrained.constrain(10, 1);
    auto m = constrained.finalize();
    DofMap dofs(m);
    gc::Database<double> db({m.fingerprint(), 1, 0, 0, 0});
    db.begin_trial();
    auto w = witness<double>();
    std::vector<gc::Property<double>> laws{{1, 1000, 0, 0}};
    auto f = gc::evaluate<double>(db, {w}, laws, 2, 7, gc::Analysis::Implicit, gc::Execution::Serial, &dofs);
    check(f.residual.size() == 1 && f.residual[0] == -20 && f.nodal_force[1][2] == -20,
          "SPC elimination retains reaction force");
    check(f.tangent.size() == 1 && f.tangent[0].value == 2000, "SPC reduced tangent");
    auto bad = w;
    bad.gap = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { gc::evaluate<double>(db, {bad}, laws, 2, 7, gc::Analysis::Explicit); });
    rejects([&] { db.trial(); });
    db.rollback();
    w.normal = {1. / 3, 2. / 3, 2. / 3};
    w.relative_increment = {.2, -.1, .05};
    gc::Property<double> law{1, 1000, 100, .3};
    auto r = gc::penalty(w, law, gc::History<double>{});
    r.validate();
    check(r.history.status == gc::Status::Slip, "oblique sliding contact");
    for (unsigned j = 0; j < 3; ++j) {
        auto a = w, c = w;
        const double eps = 1e-7;
        a.relative_increment[j] += eps;
        c.relative_increment[j] -= eps;
        a.gap += eps * w.normal[j];
        c.gap -= eps * w.normal[j];
        auto ra = gc::penalty(a, law, gc::History<double>{}), rc = gc::penalty(c, law, gc::History<double>{});
        for (unsigned i = 0; i < 3; ++i)
            check(approximately_equal((ra.traction[i] - rc.traction[i]) / (2 * eps), r.derivative[i * 3 + j],
                                      1e-6),
                  "oblique contact tangent finite difference");
    }
}
int main(int argc, char **argv) {
    try {
        topology_and_search();
        laws_and_transactions<float>();
        laws_and_transactions<double>();
        implicit_and_explicit();
        assembly_and_parallel();
        persistence(argc > 1 ? argv[1] : "general-contact-scratch");
        model_bridge_results(argc > 1 ? argv[1] : "general-contact-scratch");
        geometry_and_constraint_edges();
        std::cout << "general contact: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "general contact failure after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
