#include "sdb/general_contact/module.hpp"
#include "sdb/general_contact/bridge.hpp"
#include <iostream>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace sdb;
namespace gc = sdb::general_contact;
unsigned checks = 0;
std::filesystem::path scratch;
void check(bool x, const char *m) {
    ++checks;
    if (!x)
        throw std::runtime_error(m);
}
bool close_value(double a, double b, double tol = 1e-7) {
    return std::abs(a - b) <= tol * (1 + std::abs(a) + std::abs(b));
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "expected rejection");
}
MeshDB<double> point_mesh(const std::vector<gc::Vec<double>> &x) {
    ModelBuilder<double> b;
    for (Id i = 0; i < x.size(); ++i)
        b.add_node(i + 1, x[i]);
    return b.finalize();
}
void geometry_derivatives() {
    for (unsigned count : {3u, 4u}) {
        std::array<gc::Vec<double>, 4> face{{{0, 0, 0}, {2, 0, .1}, {2, 1, .4}, {0, 1, -.1}}};
        if (count == 3)
            face[2] = {0, 1, .2};
        const double u = .3, v = .4;
        std::array<double, 4> N =
            count == 3 ? std::array<double, 4>{1 - u - v, u, v, 0}
                       : std::array<double, 4>{(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v};
        gc::Vec<double> q{};
        for (unsigned j = 0; j < count; ++j)
            for (unsigned c = 0; c < 3; ++c)
                q[c] += N[j] * face[j][c];
        auto surface = gc::project(q, face, count);
        gc::Vec<double> p = q;
        for (unsigned c = 0; c < 3; ++c)
            p[c] -= .04 * surface.normal[c];
        auto projection = gc::project(p, face, count);
        check(projection.inside && close_value(projection.gap, -.04), "warped projection gap");
        check(close_value(projection.u, u) && close_value(projection.v, v),
              "real facet parametric coordinates");
        std::vector<gc::Vec<double>> x{p};
        for (unsigned j = 0; j < count; ++j)
            x.push_back(face[j]);
        auto mesh = point_mesh(x);
        gc::GeometryDB<double> geo(mesh);
        geo.update(mesh, x, x);
        gc::Facet facet{{1, 0, 1}, {1, 2, 3, 4}, count};
        auto d = gc::differentiate_gap(geo, 0, facet);
        const std::size_t n = x.size() * 3;
        const double eps = 2e-6;
        for (std::size_t col = 0; col < n; ++col) {
            auto plus = x, minus = x;
            plus[col / 3][col % 3] += eps;
            minus[col / 3][col % 3] -= eps;
            geo.update(mesh, plus, x);
            auto dp = gc::differentiate_gap(geo, 0, facet);
            geo.update(mesh, minus, x);
            auto dm = gc::differentiate_gap(geo, 0, facet);
            check(close_value((dp.gap - dm.gap) / (2 * eps), d.gradient[col], 1e-6),
                  "gap gradient finite difference");
            for (std::size_t row = 0; row < n; ++row) {
                check(close_value((dp.gradient[row] - dm.gradient[row]) / (2 * eps), d.hessian[row * n + col],
                                  2e-6),
                      "gap geometric Hessian finite difference");
                const double plus_res = 1000 * dp.gap * dp.gradient[row],
                             minus_res = 1000 * dm.gap * dm.gradient[row];
                const double tangent =
                    1000 * (d.gradient[row] * d.gradient[col] + d.gap * d.hessian[row * n + col]);
                check(close_value((plus_res - minus_res) / (2 * eps), tangent, 3e-6),
                      "full contact residual geometric tangent");
            }
        }
        auto degenerate = face;
        degenerate[1] = degenerate[0];
        rejects([&] { gc::project(p, degenerate, count); });
    }
}
MeshDB<double> blocks() {
    ModelBuilder<double> b;
    b.add_material({1, 1, {1, 100, .2}, 0});
    b.add_part(1);
    for (unsigned body = 0; body < 2; ++body) {
        double z = body ? .1 : -1.;
        std::array<gc::Vec<double>, 8> x{{{0, 0, z},
                                          {1, 0, z},
                                          {1, 1, z},
                                          {0, 1, z},
                                          {0, 0, z + 1},
                                          {1, 0, z + 1},
                                          {1, 1, z + 1},
                                          {0, 1, z + 1}}};
        std::vector<Id> ids;
        for (unsigned i = 0; i < 8; ++i) {
            Id id = body * 8 + i + 1;
            b.add_node(id, x[i]);
            ids.push_back(id);
        }
        b.add_element(body + 1, Topology::Hex8, ids, 1, 1);
    }
    return b.finalize();
}
void domain_and_transport() {
    auto mesh = blocks();
    gc::Definition<double> def;
    def.mesh = mesh.fingerprint();
    def.properties = {{1, 1000, 0, 0}};
    def.rules = {{1, 0, 0, 1, 0}};
    gc::Model<double> model(mesh, def);
    gc::GeometryDB<double> geometry(mesh);
    auto old = geometry.current(), current = old;
    for (unsigned i = 8; i < 16; ++i)
        current[i][2] -= .15;
    geometry.update(mesh, current, old);
    gc::DomainOptions<double> options;
    options.search_band = .2;
    auto batch = gc::detect_domain(mesh, model, geometry, options);
    check(batch.points.size() == 8, "whole domain balanced two-block contact count");
    gc::Database<double> db({mesh.fingerprint(), model.fingerprint(), 0, 0, 0});
    DofMap dofs(mesh);
    db.begin_trial();
    gc::GeometryDB<double> other_geometry(mesh);
    other_geometry.update(mesh, current, old);
    rejects([&] {
        gc::evaluate_surface(db, mesh, model, other_geometry, batch, gc::Analysis::Implicit,
                             gc::Execution::Serial, &dofs);
    });
    auto frame = gc::evaluate_surface(db, mesh, model, geometry, batch, gc::Analysis::Implicit,
                                      gc::Execution::Serial, &dofs);
    double f1 = 0, f2 = 0;
    for (unsigned i = 0; i < 8; ++i)
        f1 += frame.nodal_force[i][2];
    for (unsigned i = 8; i < 16; ++i)
        f2 += frame.nodal_force[i][2];
    check(close_value(f1, -50) && close_value(f2, 50), "balanced domain pressure patch force");
    check(close_value(frame.monitor.elastic_energy, 1.25), "domain penalty energy");
    std::vector<double> dense(dofs.size() * dofs.size());
    for (const auto &e : frame.tangent)
        dense[e.row * dofs.size() + e.column] = e.value;
    for (std::size_t i = 0; i < dofs.size(); ++i) {
        double translation = 0;
        for (std::size_t j = 0; j < dofs.size(); ++j) {
            check(close_value(dense[i * dofs.size() + j], dense[j * dofs.size() + i]),
                  "geometric tangent symmetry");
            if (j % 3 == 2)
                translation += dense[i * dofs.size() + j];
        }
        check(std::abs(translation) < 1e-8, "rigid translation tangent nullspace");
    }
#ifdef _OPENMP
    omp_set_num_threads(4);
    options.execution = gc::Execution::OpenMP;
    auto parallel = gc::detect_domain(mesh, model, geometry, options);
    auto pf = gc::evaluate_surface(db, mesh, model, geometry, parallel, gc::Analysis::Implicit,
                                   gc::Execution::OpenMP, &dofs);
    check(frame.nodal_force == pf.nodal_force && frame.residual == pf.residual,
          "parallel whole domain assembly equality");
    check(frame.tangent.size() == pf.tangent.size(), "parallel geometric tangent shape");
    for (std::size_t i = 0; i < frame.tangent.size(); ++i)
        check(frame.tangent[i].value == pf.tangent[i].value, "parallel geometric tangent values");
#endif
    options.candidate_capacity = 1;
    rejects([&] { gc::detect_domain(mesh, model, geometry, options); });
    geometry.update(mesh, current, old);
    rejects([&] {
        gc::evaluate_surface(db, mesh, model, geometry, batch, gc::Analysis::Implicit, gc::Execution::Serial,
                             &dofs);
    });
    rejects([&] { db.trial(); });
    db.rollback();
    def.rules.push_back({2, 0, 0, 0, 0});
    gc::Model<double> excluded(mesh, def);
    options.candidate_capacity = 1000000;
    check(gc::detect_domain(mesh, excluded, geometry, options).points.empty(),
          "whole domain exclusion actually controls detection");
    // Rotate a nonzero sticking spring about the normal: normal-only transport would miss this spin.
    gc::Witness<double> w;
    w.key = {1, {gc::FeatureKind::Node, 1, 0, 1}, {gc::FeatureKind::Node, 2, 0, 1}, 0};
    w.count = 2;
    w.nodes = {0, 1};
    w.weights = {1, -1};
    w.gap = -.1;
    w.relative_increment = {.001, 0, 0};
    std::vector<gc::Property<double>> laws{{1, 1000, 100, .3}};
    gc::Database<double> history({1, 2, 3, 0, 0});
    history.begin_trial();
    auto first = gc::evaluate<double>(history, {w}, laws, 2, 0, gc::Analysis::Explicit);
    auto binding = history.binding();
    binding.step = 1;
    binding.time = 1;
    history.commit(binding);
    history.begin_trial();
    w.objective = true;
    w.rotation = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    w.relative_increment = {};
    auto rotated = gc::evaluate<double>(history, {w}, laws, 2, 0, gc::Analysis::Explicit);
    check(close_value(rotated.responses[0].traction[1], first.responses[0].traction[0]) &&
              close_value(rotated.responses[0].traction[0], 0),
          "objective tangential frame spin");
    check(rotated.monitor.friction_increment == 0 &&
              close_value(rotated.monitor.elastic_energy, first.monitor.elastic_energy),
          "rigid spin no spurious dissipation");
    history.rollback();
}
void edges_and_ccd() {
    const gc::Vec<double> a0{0, 0, 0}, a1{1, 0, 0}, b0{.5, -1, .1}, b1{.5, 1, .1};
    auto p = gc::closest_edges(a0, a1, b0, b1);
    check(close_value(p.a, .5) && close_value(p.b, .5) && close_value(p.distance, .1),
          "edge-edge interior closest points");
    auto parallel = gc::closest_edges(a0, a1, gc::Vec<double>{0, 0, .1}, gc::Vec<double>{1, 0, .1});
    check(close_value(parallel.distance, .1), "parallel edge handling");
    std::array<gc::Vec<double>, 4> x{a0, a1, b0, b1};
    using D = gc::Jet<double, 12>;
    std::array<std::array<D, 3>, 4> dx;
    for (unsigned n = 0; n < 4; ++n)
        for (unsigned c = 0; c < 3; ++c)
            dx[n][c] = D::variable(x[n][c], 3 * n + c);
    auto d = gc::edge_gap(dx).distance - D(.15);
    const double eps = 1e-6;
    for (unsigned col = 0; col < 12; ++col) {
        auto plus = x, minus = x;
        plus[col / 3][col % 3] += eps;
        minus[col / 3][col % 3] -= eps;
        auto differentiate = [](const auto &coords) {
            std::array<std::array<D, 3>, 4> v;
            for (unsigned n = 0; n < 4; ++n)
                for (unsigned c = 0; c < 3; ++c)
                    v[n][c] = D::variable(coords[n][c], 3 * n + c);
            return gc::edge_gap(v).distance - D(.15);
        };
        auto dp = differentiate(plus), dm = differentiate(minus);
        for (unsigned row = 0; row < 12; ++row)
            check(close_value((dp.gradient[row] - dm.gradient[row]) / (2 * eps), d.hessian[row * 12 + col],
                              1e-6),
                  "edge distance geometric Hessian");
    }
    std::array<gc::Vec<double>, 4> triangle{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {}}};
    auto hit = gc::sweep_static_triangle(gc::Vec<double>{.2, .2, 2}, gc::Vec<double>{.2, .2, -3}, triangle);
    check(hit && close_value(*hit, .4), "CCD catches full step crossing");
    check(!gc::sweep_static_triangle(gc::Vec<double>{2, 2, 2}, gc::Vec<double>{2, 2, -3}, triangle),
          "CCD outside triangle rejected");
    rejects([&] {
        gc::sweep_static_triangle(gc::Vec<double>{.2, .2, -1}, gc::Vec<double>{.2, .2, -3}, triangle);
    });
}
void friction_jacobian() {
    for (unsigned count : {3u, 4u})
        for (double mu : {.002, .6}) {
            std::vector<gc::Vec<double>> old{{.4, .3, -.02}, {0, 0, 0}, {2, 0, 0}, {2, 1, .15}, {0, 1, 0}};
            if (count == 3) {
                old[3] = {0, 1, 0};
                old.resize(4);
            }
            auto mesh = point_mesh(old);
            gc::GeometryDB<double> geometry(mesh);
            auto current = old;
            current[0][0] += .03;
            current[1][2] += .005;
            current[2][1] += .012;
            gc::Facet face{{1, 0, 1}, {1, 2, 3, 4}, count};
            gc::Property<double> law{1, 1000, 100, mu};
            gc::History<double> history;
            history.status = gc::Status::Stick;
            history.elastic_slip = {.004, 0, 0};
            geometry.update(mesh, current, old);
            auto derivative = gc::differentiate_surface_force(geometry, 0, face, law, history, .7);
            auto forces = [&](const auto &coords) {
                std::array<gc::Vec<double>, 4> x{}, previous{};
                for (unsigned j = 0; j < count; ++j) {
                    x[j] = coords[j + 1];
                    previous[j] = old[j + 1];
                }
                auto p = gc::project(coords[0], x, count), q = gc::project(old[0], previous, count);
                auto rotation = gc::frame_rotation(p, q);
                gc::Witness<double> w;
                w.gap = p.gap;
                w.normal = p.normal;
                w.area = .7;
                gc::Vec<double> a = coords[0], b = old[0];
                auto h = history;
                h.elastic_slip = {};
                for (unsigned c = 0; c < 3; ++c)
                    for (unsigned j = 0; j < count; ++j) {
                        a[c] -= p.weights[j] * x[j][c];
                        b[c] -= p.weights[j] * previous[j][c];
                    }
                for (unsigned c = 0; c < 3; ++c) {
                    w.relative_increment[c] = a[c];
                    for (unsigned j = 0; j < 3; ++j) {
                        w.relative_increment[c] -= rotation[3 * c + j] * b[j];
                        h.elastic_slip[c] += rotation[3 * c + j] * history.elastic_slip[j];
                    }
                }
                auto response = gc::penalty(w, law, h);
                std::vector<double> force(coords.size() * 3);
                for (std::size_t i = 0; i < coords.size(); ++i)
                    for (unsigned c = 0; c < 3; ++c)
                        force[3 * i + c] = .7 * (i ? -p.weights[i - 1] : 1) * response.traction[c];
                return force;
            };
            const auto n = 3 * current.size();
            const double eps = 1e-6;
            for (std::size_t col = 0; col < n; ++col) {
                auto plus = current, minus = current;
                plus[col / 3][col % 3] += eps;
                minus[col / 3][col % 3] -= eps;
                const auto fp = forces(plus), fm = forces(minus);
                for (std::size_t row = 0; row < n; ++row)
                    check(
                        close_value(-(fp[row] - fm[row]) / (2 * eps), derivative.values[row * n + col], 3e-6),
                        "objective stick/slip full force tangent FD");
            }
        }
}
void facade_and_bundle() {
    auto mesh = blocks();
    gc::Definition<double> def;
    def.mesh = mesh.fingerprint();
    def.properties = {{1, 1000, 100, .3}};
    def.rules = {{1, 0, 0, 1, 0}};
    def.controls.band = .2;
    StateDB<double> state(mesh, true);
    gc::GeneralContact<double> module(mesh, state, def);
    const auto initial = gc::state_fingerprint(state);
    module.begin_increment();
    for (Index i = 8; i < 16; ++i)
        state.data().displacement.write()(i, 2) = -.15;
    state.data().dt = .01;
    state.data().time = .01;
    state.data().step = 1;
    auto result = module.evaluate(gc::Analysis::Implicit);
    check(result.witnesses.size() == 8, "facade domain detection");
    module.commit();
    const auto accepted = gc::state_fingerprint(state);
    check(accepted != initial, "facade accepts state");
    const auto path = scratch / "bundle-test.bin";
    module.save(path);
    module.begin_increment();
    state.data().displacement.write()(8, 2) -= .01;
    module.evaluate(gc::Analysis::Explicit);
    module.rollback();
    check(gc::state_fingerprint(state) == accepted, "facade coupled rollback");
    StateDB<double> restored(mesh);
    gc::GeneralContact<double> other(mesh, restored, def);
    other.restore(path);
    check(gc::state_fingerprint(restored) == accepted && restored.data().rotation.has_value(),
          "bundle full mechanical state restored");
    check(other.database().committed().size() == module.database().committed().size(),
          "bundle contact state restored");
    other.begin_increment();
    restored.data().node_active.write()(0) = 0;
    rejects([&] { other.evaluate(gc::Analysis::Explicit); });
    rejects([&] { other.commit(); });
    other.rollback();
    std::fstream corrupt(path, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(100);
    char byte = 0;
    corrupt.read(&byte, 1);
    byte ^= 1;
    corrupt.seekp(100);
    corrupt.write(&byte, 1);
    corrupt.close();
    rejects([&] { other.restore(path); });
    check(gc::state_fingerprint(restored) == accepted, "bundle corruption leaves destination intact");
    std::filesystem::remove(path);
    auto old = def;
    old.schema = 1;
    old.controls = {};
    gc::Model<double> legacy(mesh, old);
    gc::save_model(path, legacy);
    auto decoded = gc::load_model<double>(path, mesh);
    check(decoded.fingerprint() == legacy.fingerprint(), "schema 1 backward read");
    gc::Model<double> modern(mesh, def);
    gc::save_model(path, modern);
    auto next = gc::load_model<double>(path, mesh);
    check(next.definition().controls == def.controls, "schema 2 search controls persisted");
    std::filesystem::remove(path);
}
void rounded_edge_assembly() {
    auto mesh = blocks();
    gc::Definition<double> def;
    def.mesh = mesh.fingerprint();
    def.properties = {{1, 1000, 0, 0}};
    def.rules = {{1, 0, 0, 1, 0}};
    gc::Model<double> model(mesh, def);
    Index a = invalid_index, b = invalid_index;
    for (Index i = 0; i < model.edges().size(); ++i) {
        const auto n = model.edges()[i].nodes;
        if (n == std::array<Index, 2>{0, 1})
            a = i;
        if (n == std::array<Index, 2>{8, 9})
            b = i;
    }
    check(a != invalid_index && b != invalid_index, "edge identities found");
    gc::GeometryDB<double> geometry(mesh);
    auto x = geometry.current();
    x[0] = {0, 0, 0};
    x[1] = {1, 0, 0};
    x[8] = {.5, -1, .1};
    x[9] = {.5, 1, .1};
    geometry.update(mesh, x, x);
    gc::RoundedEdgeContact<double> contact{{a, b, 1, 1}, .075, .075, 1};
    std::vector<gc::RoundedEdgeContact<double>> contacts{contact};
    gc::Database<double> db({mesh.fingerprint(), model.fingerprint(), 0, 0, 0});
    db.begin_trial();
    DofMap dofs(mesh);
    auto frame = gc::evaluate_edges<double>(db, model, geometry, contacts, gc::Analysis::Implicit,
                                            gc::Execution::Serial, &dofs);
    check(close_value(frame.nodal_force[0][2] + frame.nodal_force[1][2], -50),
          "rounded edge force resultant");
    auto reversed = contact;
    std::swap(reversed.pair.a, reversed.pair.b);
    contacts.push_back(reversed);
    rejects([&] { gc::evaluate_edges<double>(db, model, geometry, contacts, gc::Analysis::Explicit); });
    contacts.resize(1);
    std::vector<double> matrix(dofs.size() * dofs.size());
    for (const auto &e : frame.tangent)
        matrix[e.row * dofs.size() + e.column] = e.value;
    for (Index node : {0u, 1u, 8u, 9u})
        for (unsigned c = 0; c < 3; ++c) {
            auto plus = x, minus = x;
            const double eps = 1e-6;
            plus[node][c] += eps;
            minus[node][c] -= eps;
            geometry.update(mesh, plus, x);
            auto fp = gc::evaluate_edges<double>(db, model, geometry, contacts, gc::Analysis::Implicit,
                                                 gc::Execution::Serial, &dofs);
            geometry.update(mesh, minus, x);
            auto fm = gc::evaluate_edges<double>(db, model, geometry, contacts, gc::Analysis::Implicit,
                                                 gc::Execution::Serial, &dofs);
            for (std::size_t row = 0; row < dofs.size(); ++row)
                check(close_value((fp.residual[row] - fm.residual[row]) / (2 * eps),
                                  matrix[row * dofs.size() + dofs.equation(node, c)], 3e-6),
                      "rounded edge assembled residual tangent FD");
        }
    db.rollback();
    def.controls.node_face = false;
    def.controls.band = .1;
    def.controls.rounded_edges = {{model.edge_identity(a), .075}, {model.edge_identity(b), .075}};
    gc::Model<double> edge_model(mesh, def);
    geometry.update(mesh, x, x);
    gc::Database<double> unified({mesh.fingerprint(), edge_model.fingerprint(), 0, 0, 0});
    unified.begin_trial();
    auto result = gc::evaluate_general(unified, mesh, edge_model, geometry, gc::Analysis::Implicit,
                                       gc::Execution::Serial, &dofs);
    check(result.rounded_edge_constraints == 1 && result.frame.nodal_force == frame.nodal_force,
          "persisted edge policy drives unified module");
    unified.rollback();
}
void chunked_bundle() {
    std::vector<gc::Vec<double>> positions(9000);
    for (std::size_t i = 0; i < positions.size(); ++i)
        positions[i] = {double(i), 0, 0};
    auto mesh = point_mesh(positions);
    StateDB<double> state(mesh, true);
    state.data().dt = 1;
    state.data().time = 1;
    state.data().step = 1;
    for (Index i = 0; i < mesh.nodes(); ++i) {
        state.data().displacement.write()(i, 1) = i * .001;
        state.data().mass.write()(i) = 2;
        state.data().inverse_mass.write()(i) = .5;
    }
    gc::Database<double> db(gc::bind(state, 77));
    std::vector<gc::Record<double>> records;
    for (Id i = 0; i < 2050; ++i) {
        gc::History<double> h;
        h.friction_work = .25;
        h.accumulated_slip = .1;
        records.push_back(
            {{1, {gc::FeatureKind::Node, i + 1, 0, 1}, {gc::FeatureKind::Face, 10001, 0, 1}, 0}, h});
    }
    db.restore(db.binding(), records, 600);
    const auto path = scratch / "chunks-test.bin";
    gc::save_bundle(path, state, db);
    StateDB<double> out(mesh);
    gc::Database<double> restored(gc::bind(out, 77));
    gc::load_bundle(path, mesh, out, restored);
    check(gc::state_fingerprint(out) == gc::state_fingerprint(state) && restored.committed().size() == 2050 &&
              restored.friction_dissipation() == 600,
          "multi-chunk fields/contact/retired ledger round trip");
    const auto fingerprint = gc::state_fingerprint(out);
    rejects([&] { gc::load_bundle(path, mesh, out, restored, 100); });
    rejects([&] { gc::load_bundle(path, mesh, out, restored, 100000000, 2000); });
    check(gc::state_fingerprint(out) == fingerprint, "bundle budget rejection atomicity");
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 4);
    rejects([&] { gc::load_bundle(path, mesh, out, restored); });
    check(gc::state_fingerprint(out) == fingerprint && restored.committed().size() == 2050,
          "late truncation atomicity");
    std::filesystem::remove(path);
}
void float_geometry_bundle() {
    ModelBuilder<float> b;
    b.add_node(1, {.3f, .4f, -.02f});
    b.add_node(2, {0, 0, 0});
    b.add_node(3, {1, 0, 0});
    b.add_node(4, {1, 1, 0});
    b.add_node(5, {0, 1, 0});
    auto mesh = b.finalize();
    gc::GeometryDB<float> geometry(mesh);
    gc::Facet facet{{1, 0, 1}, {1, 2, 3, 4}, 4};
    auto d = gc::differentiate_gap(geometry, 0, facet);
    check(close_value(d.gap, -.02, 1e-6), "float quad AD gap");
    auto j = gc::differentiate_surface_force(geometry, 0, facet, gc::Property<float>{1, 1000, 100, .3f},
                                             gc::History<float>{}, 1.f);
    check(close_value(j.values[2 * 15 + 2], 1000, 1e-5), "float objective geometric stiffness");
    StateDB<float> state(mesh);
    gc::Database<float> db(gc::bind(state, 3));
    const auto path = scratch / "float-bundle.bin";
    gc::save_bundle(path, state, db);
    StateDB<float> restored(mesh);
    gc::Database<float> other(gc::bind(restored, 3));
    gc::load_bundle(path, mesh, restored, other);
    check(gc::state_fingerprint(restored) == gc::state_fingerprint(state), "float bundle round trip");
    std::filesystem::remove(path);
}
int main(int argc, char **argv) {
    try {
        scratch = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("geometry-scratch");
        std::filesystem::create_directories(scratch);
        geometry_derivatives();
        domain_and_transport();
        edges_and_ccd();
        friction_jacobian();
        facade_and_bundle();
        rounded_edge_assembly();
        chunked_bundle();
        float_geometry_bundle();
        std::cout << "geometry contact: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "geometry contact failure after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
