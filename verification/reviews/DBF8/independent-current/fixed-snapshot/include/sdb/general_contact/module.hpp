#pragma once
#include "edge.hpp"
#include "checkpoint.hpp"

namespace sdb::general_contact {
template <class T>
std::vector<RoundedEdgeContact<T>> detect_rounded_edges(const Model<T> &model,
                                                        const GeometryDB<T> &geometry) {
    const auto &settings = model.definition().controls;
    std::map<Feature, Index> identities;
    for (Index i = 0; i < model.edges().size(); ++i)
        identities.emplace(model.edge_identity(i), i);
    std::vector<Index> selected;
    std::vector<T> radii;
    std::vector<detail::Box<T>> boxes;
    for (const auto &assignment : settings.rounded_edges) {
        Index edge = identities.at(assignment.feature);
        selected.push_back(edge);
        radii.push_back(assignment.radius);
        detail::Box<T> box;
        for (Index node : model.edges()[edge].nodes)
            box.add(geometry.current()[node]);
        for (unsigned c = 0; c < 3; ++c) {
            box.lower[c] -= assignment.radius;
            box.upper[c] += assignment.radius;
        }
        boxes.push_back(box);
    }
    detail::BVH<T> bvh(boxes);
    std::vector<Index> hits;
    hits.reserve(selected.size());
    std::vector<RoundedEdgeContact<T>> result;
    std::size_t candidates = 0;
    for (Index i = 0; i < selected.size(); ++i) {
        const auto &a = model.edges()[selected[i]];
        Vec<T> center;
        T extent = 0;
        for (unsigned c = 0; c < 3; ++c) {
            center[c] = (geometry.current()[a.nodes[0]][c] + geometry.current()[a.nodes[1]][c]) / 2;
            extent = std::max(
                extent, std::abs(geometry.current()[a.nodes[1]][c] - geometry.current()[a.nodes[0]][c]) / 2);
        }
        hits.clear();
        bvh.query(center, extent + radii[i] + settings.band, [&](Index j) {
            if (j > i)
                hits.push_back(j);
        });
        std::sort(hits.begin(), hits.end());
        for (Index j : hits) {
            if (++candidates > settings.candidate_capacity)
                throw std::length_error("rounded edge candidate capacity exceeded");
            const auto &b = model.edges()[selected[j]];
            bool incident = false;
            for (Index n : a.nodes)
                for (Index m : b.nodes)
                    incident = incident || n == m;
            if (incident)
                continue;
            std::optional<Assignment> assignment;
            for (Index f : a.incident_faces)
                for (Index g : b.incident_faces) {
                    auto next = model.resolve(f, g);
                    if (!next)
                        continue;
                    if (assignment && assignment->property != next->property)
                        throw std::invalid_argument("edge pair spans conflicting contact properties");
                    if (!assignment || next->rule < assignment->rule)
                        assignment = next;
                }
            if (!assignment)
                continue;
            auto p = closest_edges(geometry.current()[a.nodes[0]], geometry.current()[a.nodes[1]],
                                   geometry.current()[b.nodes[0]], geometry.current()[b.nodes[1]]);
            if (p.distance > radii[i] + radii[j] + settings.band)
                continue;
            result.push_back({{selected[i], selected[j], assignment->rule, assignment->property},
                              radii[i],
                              radii[j],
                              settings.edge_area});
        }
    }
    return result;
}
template <class T> struct Evaluation {
    Frame<T> frame;
    std::vector<Witness<T>> witnesses;
    SearchStatistics surface_statistics;
    std::size_t rounded_edge_constraints = 0;
};
// Combined evaluation stages all surface AND edge histories once. Calling two independent
// evaluate functions in sequence would incorrectly retire the first family's histories.
template <class T>
Evaluation<T> evaluate_general(Database<T> &db, const MeshDB<T> &mesh, const Model<T> &model,
                               const GeometryDB<T> &geometry, Analysis analysis, Execution execution,
                               const DofMap *dofs) {
    if (!db.in_trial())
        throw std::logic_error("general contact evaluation requires trial");
    db.invalidate_trial();
    if (db.binding().mesh != mesh.fingerprint() || db.binding().configuration != model.fingerprint() ||
        geometry.model_id() != mesh.fingerprint())
        throw std::invalid_argument("general contact definition binding");
    try {
        Evaluation<T> result;
        const auto &settings = model.definition().controls;
        SurfaceBatch<T> surface;
        if (settings.node_face) {
            DomainOptions<T> options;
            options.search_band = settings.band;
            options.minimum_opposition = settings.minimum_opposition;
            options.candidate_capacity = static_cast<std::size_t>(settings.candidate_capacity);
            options.execution = execution;
            surface = detect_domain(mesh, model, geometry, options);
            result.witnesses = surface.witnesses();
            result.surface_statistics = surface.statistics;
        }
        validate_surface_history(db, surface, model);
        auto edges = detect_rounded_edges(model, geometry);
        result.rounded_edge_constraints = edges.size();
        std::map<Id, Property<T>> properties;
        for (const auto &p : model.definition().properties)
            properties.emplace(p.id, p);
        std::map<Key, LocalJacobian<T>> derivatives;
        if (analysis == Analysis::Implicit) {
            std::vector<LocalJacobian<T>> parts(surface.points.size());
            std::vector<std::exception_ptr> errors(surface.points.size());
            detail::for_each(surface.points.size(), execution, [&](std::size_t i) {
                try {
                    const auto &p = surface.points[i];
                    parts[i] = differentiate_surface_force(
                        geometry, p.witness.nodes[0], model.facets()[p.target_face],
                        properties.at(p.witness.property), db.committed().find(p.witness.key),
                        p.witness.area);
                } catch (...) {
                    errors[i] = std::current_exception();
                }
            });
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (errors[i])
                    std::rethrow_exception(errors[i]);
                derivatives.emplace(surface.points[i].witness.key, std::move(parts[i]));
            }
        }
        for (const auto &edge : edges) {
            if (properties.at(edge.pair.property).friction != 0)
                throw std::invalid_argument("rounded-edge friction not implemented");
            auto [w, d] = rounded_edge(model, geometry, edge, analysis == Analysis::Implicit);
            result.witnesses.push_back(w);
            if (analysis == Analysis::Implicit) {
                LocalJacobian<T> j;
                j.nodes = d.nodes;
                const auto n = d.gradient.size();
                j.values.resize(n * n);
                const T k = w.area * properties.at(w.property).normal_penalty;
                if (w.gap < 0)
                    for (std::size_t a = 0; a < n; ++a)
                        for (std::size_t b = 0; b < n; ++b)
                            j.values[a * n + b] =
                                k * (d.gradient[a] * d.gradient[b] + d.gap * d.hessian[a * n + b]);
                derivatives.emplace(w.key, std::move(j));
            }
        }
        result.frame = evaluate<T>(db, result.witnesses, model.definition().properties, mesh.nodes(),
                                   geometry.revision(), analysis, execution, dofs);
        if (analysis == Analysis::Implicit) {
            std::map<std::pair<std::int64_t, std::int64_t>, T> matrix;
            for (const auto &w : result.witnesses) {
                if (w.gap >= 0)
                    continue;
                const auto &d = derivatives.at(w.key);
                const auto n = 3 * d.nodes.size();
                for (std::size_t i = 0; i < n; ++i) {
                    auto row = dofs->equation(d.nodes[i / 3], static_cast<unsigned>(i % 3));
                    if (row < 0)
                        continue;
                    for (std::size_t j = 0; j < n; ++j) {
                        auto col = dofs->equation(d.nodes[j / 3], static_cast<unsigned>(j % 3));
                        if (col < 0)
                            continue;
                        matrix[{row, col}] += d.values[i * n + j];
                    }
                }
            }
            result.frame.tangent.clear();
            result.frame.tangent_kind = TangentKind::ConsistentGeometry;
            for (auto [entry, value] : matrix) {
                finite(value);
                if (value != 0)
                    result.frame.tangent.push_back({entry.first, entry.second, value});
            }
        }
        return result;
    } catch (...) {
        db.invalidate_trial();
        throw;
    }
}
template <class T> class GeneralContact {
    const MeshDB<T> &mesh_;
    StateDB<T> &state_;
    Model<T> model_;
    GeometryDB<T> geometry_;
    Database<T> contact_;
    DofMap dofs_;
    std::vector<Vec<T>> accepted_positions_;
    std::optional<CoupledTrial<T>> trial_;
    std::uint64_t evaluated_epoch_ = 0;
    std::vector<Vec<T>> coordinates(const StateDB<T> &state) const {
        auto view = state.current_view(mesh_);
        std::vector<Vec<T>> x(mesh_.nodes());
        for (Index n = 0; n < mesh_.nodes(); ++n)
            for (unsigned c = 0; c < 3; ++c)
                x[n][c] = view(n, c);
        return x;
    }

  public:
    GeneralContact(const MeshDB<T> &mesh, StateDB<T> &state, Definition<T> definition)
        : mesh_(mesh), state_(state), model_(mesh, std::move(definition)), geometry_(mesh),
          contact_(bind(state, model_.fingerprint())), dofs_(mesh), accepted_positions_(coordinates(state)) {}
    const Model<T> &model() const { return model_; }
    const Database<T> &database() const { return contact_; }
    const DofMap &dofs() const { return dofs_; }
    void begin_increment() {
        if (trial_)
            throw std::logic_error("nested general contact increment");
        trial_.emplace(state_, contact_);
        evaluated_epoch_ = 0;
    }
    Evaluation<T> evaluate(Analysis analysis, Execution execution = Execution::Serial) {
        if (!trial_)
            throw std::logic_error("general contact evaluation outside increment");
        evaluated_epoch_ = 0;
        contact_.invalidate_trial();
        state_.validate_shape();
        for (const auto *field : {&state_.data().node_active, &state_.data().element_active})
            for (std::size_t i = 0; i < field->size(); ++i)
                if (field->read()(i) != 1)
                    throw std::invalid_argument(
                        "general contact dynamic deletion requires topology rebuild (unsupported)");
        auto x = coordinates(state_);
        geometry_.update(mesh_, x, accepted_positions_);
        auto result = evaluate_general(contact_, mesh_, model_, geometry_, analysis, execution,
                                       analysis == Analysis::Implicit ? &dofs_ : nullptr);
        trial_->apply_force(result.frame);
        evaluated_epoch_ = contact_.epoch();
        return result;
    }
    void commit() {
        if (!trial_ || !evaluated_epoch_ || evaluated_epoch_ != contact_.epoch())
            throw std::logic_error("general contact has no valid evaluation");
        auto x = coordinates(state_);
        if (x != geometry_.current())
            throw std::logic_error("geometry changed after contact evaluation");
        trial_->commit();
        accepted_positions_.swap(x);
        trial_.reset();
        evaluated_epoch_ = 0;
    }
    void rollback() {
        if (!trial_)
            throw std::logic_error("general contact rollback outside increment");
        trial_->rollback();
        trial_.reset();
        evaluated_epoch_ = 0;
    }
    void save(const std::filesystem::path &path) const { save_bundle(path, state_, contact_); }
    void restore(const std::filesystem::path &path) {
        if (trial_)
            throw std::logic_error("general contact restore during trial");
        StateDB<T> state(mesh_);
        Database<T> contact(bind(state, model_.fingerprint()));
        load_bundle(path, mesh_, state, contact);
        auto x = coordinates(state);
        contact_.adopt(std::move(contact));
        state_.data() = std::move(state.data());
        accepted_positions_.swap(x);
        evaluated_epoch_ = 0;
    }
};
} // namespace sdb::general_contact
