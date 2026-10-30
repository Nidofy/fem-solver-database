#pragma once
#include "model.hpp"
#include "geometry.hpp"
#include <exception>

namespace sdb::general_contact {
template <class T> struct DomainOptions {
    T search_band = T(.01), minimum_opposition = 0;
    std::size_t candidate_capacity = 1000000;
    Execution execution = Execution::Serial;
};
template <class T> struct SurfacePoint {
    Witness<T> witness;
    Index target_face = invalid_index;
    T u = 0, v = 0;
    Vec<T> position{};
};
template <class T> struct SurfaceBatch {
    std::uint64_t mesh = 0, configuration = 0, geometry_revision = 0, geometry_owner = 0;
    std::vector<SurfacePoint<T>> points;
    SearchStatistics statistics;
    std::vector<Witness<T>> witnesses() const {
        std::vector<Witness<T>> out;
        out.reserve(points.size());
        for (const auto &p : points)
            out.push_back(p.witness);
        return out;
    }
};
template <class T>
std::array<Vec<T>, 4> facet_coordinates(std::span<const Vec<T>> positions, const Facet &face) {
    std::array<Vec<T>, 4> x{};
    for (unsigned j = 0; j < face.count; ++j)
        x[j] = positions[face.nodes[j]];
    return x;
}
template <class T> T facet_area(const std::array<Vec<T>, 4> &x, unsigned count) {
    auto a = subtract(x[1], x[0]), b = subtract(x[count == 3 ? 2 : 3], x[0]);
    if (count == 3) {
        auto n = cross(a, b);
        return T(.5) * std::sqrt(dot(n, n));
    }
    Vec<T> twist;
    for (unsigned c = 0; c < 3; ++c)
        twist[c] = x[0][c] - x[1][c] + x[2][c] - x[3][c];
    T area = 0;
    const T d = T(.5) / std::sqrt(T(3));
    for (T u : {T(.5) - d, T(.5) + d})
        for (T v : {T(.5) - d, T(.5) + d}) {
            Vec<T> du, dv;
            for (unsigned c = 0; c < 3; ++c) {
                du[c] = a[c] + v * twist[c];
                dv[c] = b[c] + u * twist[c];
            }
            auto n = cross(du, dv);
            area += T(.25) * std::sqrt(dot(n, n));
        }
    return area;
}
// Whole-domain balanced NTS scheduling. Source/target regions and exclusions are evaluated
// from Model; no hand-selected probe list or property argument is required.
template <class T>
SurfaceBatch<T> detect_domain(const MeshDB<T> &mesh, const Model<T> &model, const GeometryDB<T> &geometry,
                              DomainOptions<T> options = {}) {
    if (mesh.fingerprint() != model.definition().mesh || geometry.model_id() != mesh.fingerprint())
        throw std::invalid_argument("contact domain model mismatch");
    finite(options.search_band);
    finite(options.minimum_opposition);
    if (options.search_band <= 0 || options.minimum_opposition < 0 || options.minimum_opposition >= 1)
        throw std::invalid_argument("contact domain search controls");
    const auto &faces = model.facets();
    const auto &positions = geometry.current();
    std::vector<Vec<T>> reference(mesh.nodes());
    for (Index n = 0; n < mesh.nodes(); ++n)
        for (unsigned c = 0; c < 3; ++c)
            reference[n][c] = mesh.reference()(n, c);
    std::vector<detail::Box<T>> boxes(faces.size());
    std::vector<Vec<T>> normals(faces.size());
    std::vector<T> areas(faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        auto x = facet_coordinates<T>(positions, faces[i]);
        Vec<T> center{};
        for (unsigned j = 0; j < faces[i].count; ++j) {
            boxes[i].add(x[j]);
            for (unsigned c = 0; c < 3; ++c)
                center[c] += x[j][c] / T(faces[i].count);
        }
        normals[i] = project(center, x, faces[i].count).normal;
        areas[i] = facet_area(facet_coordinates<T>(reference, faces[i]), faces[i].count);
        finite(areas[i]);
        if (areas[i] <= 0)
            throw std::invalid_argument("invalid reference contact area");
    }
    detail::BVH<T> bvh(boxes);
    struct Candidate {
        Index node, face;
        Assignment assignment;
        T area;
    };
    std::vector<Candidate> candidates;
    SurfaceBatch<T> batch;
    batch.mesh = mesh.fingerprint();
    batch.configuration = model.fingerprint();
    batch.geometry_revision = geometry.revision();
    batch.geometry_owner = geometry.identity();
    // Policy is deliberately outside parallel kernels: conflicting assignments throw here.
    std::vector<Index> hits;
    hits.reserve(faces.size());
    for (Index n = 0; n < mesh.nodes(); ++n) {
        if (model.node_faces()[n].empty())
            continue;
        ++batch.statistics.queries;
        hits.clear();
        bvh.query(positions[n], options.search_band, [&](Index f) { hits.push_back(f); });
        std::sort(hits.begin(), hits.end());
        for (Index target : hits) {
            ++batch.statistics.box_candidates;
            std::map<Id, std::pair<Id, T>> contributions;
            bool incident = false;
            for (unsigned j = 0; j < faces[target].count; ++j)
                incident = incident || faces[target].nodes[j] == n;
            if (incident)
                continue;
            for (Index source : model.node_faces()[n]) {
                auto assignment = model.resolve(source, target);
                if (!assignment || dot(normals[source], normals[target]) >= -options.minimum_opposition)
                    continue;
                auto &entry = contributions[assignment->rule];
                entry.first = assignment->property;
                entry.second += T(.5) * areas[source] / T(faces[source].count);
            }
            for (auto [rule, value] : contributions) {
                if (candidates.size() >= options.candidate_capacity)
                    throw std::length_error("domain contact capacity exceeded");
                candidates.push_back({n, target, {rule, value.first}, value.second});
            }
        }
    }
    std::vector<SurfacePoint<T>> rows(candidates.size());
    std::vector<std::uint8_t> valid(candidates.size());
    std::vector<std::exception_ptr> errors(candidates.size());
    detail::for_each(candidates.size(), options.execution, [&](std::size_t i) {
        try {
            const auto &c = candidates[i];
            const auto &f = faces[c.face];
            auto x = facet_coordinates<T>(positions, f);
            auto projection = project(positions[c.node], x, f.count);
            if (!projection.inside || std::abs(projection.gap) > options.search_band)
                return;
            auto old_x = facet_coordinates<T>(geometry.previous(), f);
            auto old_projection = project(geometry.previous()[c.node], old_x, f.count);
            auto &out = rows[i];
            out.target_face = c.face;
            out.u = projection.u;
            out.v = projection.v;
            auto &w = out.witness;
            w.key = {c.assignment.rule,
                     {FeatureKind::Node, mesh.node_id(c.node), 0, 1},
                     {FeatureKind::Face, f.identity.element, f.identity.local, f.identity.side},
                     0};
            w.property = c.assignment.property;
            w.geometry_epoch = geometry.revision();
            w.count = f.count + 1;
            w.nodes[0] = c.node;
            w.weights[0] = 1;
            w.gap = projection.gap;
            w.normal = projection.normal;
            w.area = c.area;
            w.objective = true;
            w.rotation = frame_rotation(projection, old_projection);
            Vec<T> relative = positions[c.node], previous = geometry.previous()[c.node];
            for (unsigned j = 0; j < f.count; ++j) {
                w.nodes[j + 1] = f.nodes[j];
                w.weights[j + 1] = -projection.weights[j];
                for (unsigned k = 0; k < 3; ++k) {
                    out.position[k] += projection.weights[j] * x[j][k];
                    relative[k] -= projection.weights[j] * x[j][k];
                    previous[k] -= projection.weights[j] * old_x[j][k];
                }
            }
            w.relative_increment = relative;
            for (unsigned a = 0; a < 3; ++a)
                for (unsigned b = 0; b < 3; ++b)
                    w.relative_increment[a] -= w.rotation[a * 3 + b] * previous[b];
            w.validate(mesh.nodes(), geometry.revision());
            valid[i] = 1;
        } catch (...) {
            errors[i] = std::current_exception();
        }
    });
    for (const auto &error : errors)
        if (error)
            std::rethrow_exception(error);
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (valid[i])
            batch.points.push_back(rows[i]);
    std::sort(batch.points.begin(), batch.points.end(),
              [](const auto &a, const auto &b) { return a.witness.key < b.witness.key; });
    // Coplanar shared seams may project to two faces. Keep one deterministic owner;
    // distinct normals (e.g. a corner) remain separate constraints.
    std::vector<SurfacePoint<T>> unique;
    for (const auto &p : batch.points) {
        bool duplicate = false;
        for (auto it = unique.rbegin(); it != unique.rend(); ++it) {
            if (it->witness.key.interaction != p.witness.key.interaction ||
                it->witness.key.a != p.witness.key.a)
                break;
            auto delta = subtract(it->position, p.position);
            T scale = std::max(
                {T(1), std::sqrt(dot(it->position, it->position)), std::sqrt(dot(p.position, p.position))});
            T tol = T(256) * std::numeric_limits<T>::epsilon();
            if (dot(it->witness.normal, p.witness.normal) > 1 - tol &&
                std::sqrt(dot(delta, delta)) <= tol * scale) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            unique.push_back(p);
    }
    batch.points = std::move(unique);
    batch.statistics.accepted = batch.points.size();
    return batch;
}
// History transfer across facets needs a material surface coordinate and integration-area
// remapping. Reject an observed transfer instead of silently resetting a loaded spring.
template <class T>
void validate_surface_history(const Database<T> &db, const SurfaceBatch<T> &batch, const Model<T> &model) {
    std::set<Key> current;
    std::set<std::pair<Id, Feature>> friction_sources;
    std::map<Id, T> friction;
    for (const auto &p : model.definition().properties)
        friction.emplace(p.id, p.friction);
    for (const auto &p : batch.points) {
        current.insert(p.witness.key);
        if (p.witness.gap < 0 && friction.at(p.witness.property) > 0)
            friction_sources.emplace(p.witness.key.interaction, p.witness.key.a);
    }
    for (std::size_t i = 0; i < db.committed().size(); ++i) {
        const auto &key = db.committed().keys()[i];
        if (key.b.kind == FeatureKind::Face && db.committed().at(i).status != Status::Open &&
            !current.contains(key) && friction_sources.contains({key.interaction, key.a}))
            throw std::invalid_argument("friction history transfer across facets is not implemented");
    }
}
// Full geometric/objective force tangent for INTERIOR node/facet constraints.
// Reference integration weights are frozen, while projection coordinates/normals are differentiated.
template <class T>
Frame<T> evaluate_surface(Database<T> &db, const MeshDB<T> &mesh, const Model<T> &model,
                          const GeometryDB<T> &geometry, const SurfaceBatch<T> &batch, Analysis analysis,
                          Execution execution = Execution::Serial, const DofMap *dofs = nullptr) {
    if (!db.in_trial())
        throw std::logic_error("surface evaluation requires contact trial");
    db.invalidate_trial();
    if (batch.mesh != mesh.fingerprint() || batch.configuration != model.fingerprint() ||
        batch.geometry_revision != geometry.revision() || batch.geometry_owner != geometry.identity() ||
        db.binding().mesh != batch.mesh || db.binding().configuration != batch.configuration ||
        geometry.model_id() != batch.mesh)
        throw std::invalid_argument("surface batch/model/state binding mismatch");
    std::map<Id, Property<T>> properties;
    for (const auto &p : model.definition().properties)
        properties.emplace(p.id, p);
    try {
        validate_surface_history(db, batch, model);
        auto frame = evaluate<T>(db, batch.witnesses(), model.definition().properties, mesh.nodes(),
                                 geometry.revision(), analysis, execution, dofs);
        if (analysis != Analysis::Implicit)
            return frame;
        std::vector<LocalJacobian<T>> derivatives(batch.points.size());
        std::vector<std::exception_ptr> errors(batch.points.size());
        detail::for_each(batch.points.size(), execution, [&](std::size_t i) {
            try {
                const auto &p = batch.points[i];
                derivatives[i] = differentiate_surface_force(
                    geometry, p.witness.nodes[0], model.facets().at(p.target_face),
                    properties.at(p.witness.property), db.committed().find(p.witness.key), p.witness.area);
            } catch (...) {
                errors[i] = std::current_exception();
            }
        });
        for (const auto &error : errors)
            if (error)
                std::rethrow_exception(error);
        frame.tangent.clear();
        for (std::size_t i = 0; i < batch.points.size(); ++i) {
            const auto &w = batch.points[i].witness;
            const auto &d = derivatives[i];
            if (w.gap >= 0)
                continue;
            const auto n = d.nodes.size() * 3;
            for (std::size_t a = 0; a < n; ++a) {
                auto row = dofs->equation(d.nodes[a / 3], static_cast<unsigned>(a % 3));
                if (row < 0)
                    continue;
                for (std::size_t b = 0; b < n; ++b) {
                    auto col = dofs->equation(d.nodes[b / 3], static_cast<unsigned>(b % 3));
                    if (col < 0)
                        continue;
                    T value = d.values[a * n + b];
                    finite(value);
                    if (value != 0)
                        frame.tangent.push_back({row, col, value});
                }
            }
        }
        std::stable_sort(frame.tangent.begin(), frame.tangent.end(), [](const auto &a, const auto &b) {
            return std::tie(a.row, a.column) < std::tie(b.row, b.column);
        });
        std::vector<Triplet<T>> merged;
        for (const auto &e : frame.tangent) {
            if (!merged.empty() && merged.back().row == e.row && merged.back().column == e.column)
                merged.back().value += e.value;
            else
                merged.push_back(e);
        }
        for (const auto &e : merged)
            finite(e.value);
        frame.tangent = std::move(merged);
        frame.tangent_kind = TangentKind::ConsistentGeometry;
        return frame;
    } catch (...) {
        db.invalidate_trial();
        throw;
    }
}
} // namespace sdb::general_contact
