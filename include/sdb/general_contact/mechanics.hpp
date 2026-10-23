#pragma once
#include "database.hpp"
#include "../implicit.hpp"

namespace sdb::general_contact {
enum class Execution { Serial, OpenMP };
enum class Analysis { Explicit, Implicit };
// B maps nodal translations to relative motion. Positive weights sum to one,
// negative weights to minus one. Includes the opposing nodes, even when fixed.
template <class T> struct Witness {
    Key key;
    Id property = 1;
    std::uint64_t geometry_epoch = 0;
    std::array<Index, 8> nodes{};
    std::array<T, 8> weights{};
    std::uint32_t count = 0;
    Vec<T> normal{0, 0, 1}, relative_increment{};
    bool objective = false;
    std::array<T, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1}; // committed contact frame -> trial frame
    T gap = 0, area = 1;
    void validate(std::size_t node_count, std::uint64_t epoch) const {
        general_contact::validate(key);
        finite(gap);
        finite(area);
        if (count < 2 || count > 8 || area <= 0 || geometry_epoch != epoch)
            throw std::invalid_argument("invalid or stale contact witness");
        for (T x : normal)
            finite(x);
        for (T x : relative_increment)
            finite(x);
        T positive = 0, negative = 0;
        for (unsigned i = 0; i < count; ++i) {
            finite(weights[i]);
            if (nodes[i] >= node_count)
                throw std::out_of_range("contact stencil node");
            for (unsigned j = 0; j < i; ++j)
                if (nodes[i] == nodes[j])
                    throw std::invalid_argument("duplicate stencil node");
            if (weights[i] >= 0)
                positive += weights[i];
            else
                negative += weights[i];
        }
        const T tol = T(256) * std::numeric_limits<T>::epsilon();
        if (objective) {
            for (T x : rotation)
                finite(x);
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < 3; ++j) {
                    T value = 0;
                    for (unsigned k = 0; k < 3; ++k)
                        value += rotation[k * 3 + i] * rotation[k * 3 + j];
                    if (std::abs(value - (i == j ? T(1) : T(0))) > tol)
                        throw std::invalid_argument("nonorthogonal contact frame transport");
                }
            const T det = rotation[0] * (rotation[4] * rotation[8] - rotation[5] * rotation[7]) -
                          rotation[1] * (rotation[3] * rotation[8] - rotation[5] * rotation[6]) +
                          rotation[2] * (rotation[3] * rotation[7] - rotation[4] * rotation[6]);
            if (std::abs(det - 1) > tol)
                throw std::invalid_argument("improper contact frame transport");
        }
        if (std::abs(dot(normal, normal) - 1) > tol || std::abs(positive - 1) > tol ||
            std::abs(negative + 1) > tol)
            throw std::invalid_argument("contact normal/partition of unity");
    }
};
template <class T> struct Response {
    Vec<T> traction{};
    std::array<T, 9> derivative{}; // d traction / d relative displacement, frozen geometry
    History<T> history;
    T elastic_energy = 0, friction_increment = 0;
    void validate() const {
        for (T x : traction)
            finite(x);
        for (T x : derivative)
            finite(x);
        finite(elastic_energy);
        finite(friction_increment);
        history.validate();
        if (elastic_energy < 0 || friction_increment < 0)
            throw std::runtime_error("invalid contact energy");
    }
};
// No allocation, exceptions, global state, or mutex in the parallel law loop.
// Inputs are checked before dispatch; finite outputs are checked after the join.
template <class T> Response<T> penalty(const Witness<T> &w, const Property<T> &p, History<T> h) noexcept {
    Response<T> r;
    r.history = h;
    r.history.normal = w.normal;
    r.history.elastic_slip = {};
    r.history.status = Status::Open;
    if (w.gap >= 0)
        return r;
    const T pressure = -p.normal_penalty * w.gap;
    const T normal_increment = dot(w.normal, w.relative_increment);
    Vec<T> trial{};
    for (unsigned i = 0; i < 3; ++i)
        trial[i] = h.elastic_slip[i] + w.relative_increment[i] - normal_increment * w.normal[i];
    const T length = std::sqrt(dot(trial, trial));
    const T limit = p.friction * pressure;
    const bool slip = p.tangential_penalty * length > limit && length > 0;
    const T scale = (p.friction == 0 || p.tangential_penalty == 0)
                        ? T(0)
                        : (slip ? limit / (p.tangential_penalty * length) : T(1));
    r.history.status = slip && p.friction > 0 ? Status::Slip : Status::Stick;
    const T plastic = p.friction > 0 ? length * (1 - scale) : T(0);
    r.history.accumulated_slip += plastic;
    r.friction_increment = w.area * limit * plastic;
    r.history.friction_work += r.friction_increment;
    for (unsigned i = 0; i < 3; ++i) {
        r.history.elastic_slip[i] = scale * trial[i];
        r.traction[i] = pressure * w.normal[i] - p.tangential_penalty * r.history.elastic_slip[i];
        for (unsigned j = 0; j < 3; ++j) {
            const T nn = w.normal[i] * w.normal[j], projection = (i == j ? T(1) : T(0)) - nn;
            T value = -p.normal_penalty * nn;
            if (p.friction > 0 && p.tangential_penalty > 0) {
                if (!slip)
                    value -= p.tangential_penalty * projection;
                else
                    value += p.friction * p.normal_penalty * (trial[i] / length) * w.normal[j] -
                             limit / length * (projection - trial[i] * trial[j] / (length * length));
            }
            r.derivative[i * 3 + j] = value;
        }
    }
    r.elastic_energy = w.area * T(.5) *
                       (p.normal_penalty * w.gap * w.gap +
                        p.tangential_penalty * dot(r.history.elastic_slip, r.history.elastic_slip));
    return r;
}
template <class T> struct Triplet {
    std::int64_t row = 0, column = 0;
    T value = 0;
};
template <class T> struct Monitor {
    std::size_t witnesses = 0, open = 0, stick = 0, slip = 0;
    T maximum_penetration = 0, elastic_energy = 0, friction_increment = 0;
    Vec<T> force_balance{};
};
enum class TangentKind { None, FrozenGeometry, ConsistentGeometry };
template <class T> struct Frame {
    Binding origin;
    std::uint64_t state_epoch = 0;
    std::uint64_t geometry_epoch = 0;
    std::vector<Key> keys; // Contact location has its OWN identity table, not element row IDs.
    std::vector<Response<T>> responses;
    std::vector<Vec<T>> nodal_force;
    std::vector<T> residual;
    std::vector<Triplet<T>> tangent; // sorted unique free-equation entries; may be unsymmetric
    Monitor<T> monitor;
    TangentKind tangent_kind = TangentKind::None;
};
// Reference engine for supplied, validated frozen-geometry witnesses. Search is a separate stage.
template <class T>
Frame<T> evaluate(Database<T> &db, std::vector<Witness<T>> witnesses, std::span<const Property<T>> properties,
                  std::size_t nodes, std::uint64_t geometry_epoch, Analysis analysis,
                  Execution execution = Execution::Serial, const DofMap *dofs = nullptr) {
    if (!db.in_trial())
        throw std::logic_error("contact evaluation requires trial");
    db.invalidate_trial(); // failed reevaluation cannot leave an older iteration committable
    index_of(nodes);
    if (analysis != Analysis::Explicit && analysis != Analysis::Implicit)
        throw std::invalid_argument("contact analysis");
    if (execution != Execution::Serial && execution != Execution::OpenMP)
        throw std::invalid_argument("contact execution");
    if (analysis == Analysis::Implicit && !dofs)
        throw std::invalid_argument("implicit contact needs DOF map");
#ifndef _OPENMP
    if (execution == Execution::OpenMP)
        throw std::runtime_error("OpenMP contact backend not compiled");
#endif
    std::map<Id, Property<T>> laws;
    for (const auto &p : properties) {
        p.validate();
        if (!laws.emplace(p.id, p).second)
            throw std::invalid_argument("duplicate contact property");
    }
    std::sort(witnesses.begin(), witnesses.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
    std::vector<Property<T>> selected;
    std::vector<History<T>> histories;
    selected.reserve(witnesses.size());
    histories.reserve(witnesses.size());
    for (std::size_t i = 0; i < witnesses.size(); ++i) {
        const auto &w = witnesses[i];
        w.validate(nodes, geometry_epoch);
        if (i && w.key == witnesses[i - 1].key)
            throw std::invalid_argument("duplicate witness ownership");
        selected.push_back(laws.at(w.property));
        auto h = db.committed().find(w.key);
        if (laws.at(w.property).friction == 0) {
            h.normal = w.normal;
            h.elastic_slip = {};
        }
        if (w.objective && h.status != Status::Open) {
            Vec<T> elastic{}, normal{};
            for (unsigned a = 0; a < 3; ++a)
                for (unsigned b = 0; b < 3; ++b) {
                    elastic[a] += w.rotation[a * 3 + b] * h.elastic_slip[b];
                    normal[a] += w.rotation[a * 3 + b] * h.normal[b];
                }
            h.elastic_slip = elastic;
            h.normal = normal;
        }
        if (h.status != Status::Open &&
            dot(h.normal, w.normal) < T(1) - T(256) * std::numeric_limits<T>::epsilon())
            throw std::invalid_argument("contact history frame does not match supplied objective transport");
        histories.push_back(h);
        if (dofs)
            for (unsigned k = 0; k < w.count; ++k)
                for (unsigned c = 0; c < 3; ++c)
                    (void)dofs->equation(w.nodes[k], c);
    }
    Frame<T> frame;
    frame.tangent_kind = analysis == Analysis::Implicit ? TangentKind::FrozenGeometry : TangentKind::None;
    frame.geometry_epoch = geometry_epoch;
    frame.responses.resize(witnesses.size());
    frame.nodal_force.resize(nodes);
    if (analysis == Analysis::Implicit)
        frame.residual.resize(dofs->size());
    const auto count = static_cast<std::int64_t>(witnesses.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (execution == Execution::OpenMP)
#endif
    for (std::int64_t i = 0; i < count; ++i)
        frame.responses[static_cast<std::size_t>(i)] =
            penalty(witnesses[static_cast<std::size_t>(i)], selected[static_cast<std::size_t>(i)],
                    histories[static_cast<std::size_t>(i)]);
    // Ordered reduction is deterministic for the same scalar/compiler, independent of worker count.
    std::vector<Record<T>> records;
    records.reserve(witnesses.size());
    for (std::size_t i = 0; i < witnesses.size(); ++i) {
        const auto &w = witnesses[i];
        const auto &r = frame.responses[i];
        r.validate();
        records.push_back({w.key, r.history});
        frame.keys.push_back(w.key);
        auto &m = frame.monitor;
        ++m.witnesses;
        if (r.history.status == Status::Open)
            ++m.open;
        else if (r.history.status == Status::Stick)
            ++m.stick;
        else
            ++m.slip;
        m.maximum_penetration = std::max(m.maximum_penetration, -w.gap);
        m.elastic_energy += r.elastic_energy;
        m.friction_increment += r.friction_increment;
        for (unsigned a = 0; a < w.count; ++a)
            for (unsigned c = 0; c < 3; ++c) {
                frame.nodal_force[w.nodes[a]][c] += w.weights[a] * w.area * r.traction[c];
                if (analysis == Analysis::Implicit) {
                    const auto row = dofs->equation(w.nodes[a], c);
                    if (row < 0)
                        continue;
                    for (unsigned b = 0; b < w.count; ++b)
                        for (unsigned d = 0; d < 3; ++d) {
                            const auto col = dofs->equation(w.nodes[b], d);
                            if (col < 0)
                                continue;
                            T value = -w.weights[a] * w.weights[b] * w.area * r.derivative[c * 3 + d];
                            if (value != 0)
                                frame.tangent.push_back({row, col, value});
                        }
                }
            }
    }
    for (Index n = 0; n < nodes; ++n)
        for (unsigned c = 0; c < 3; ++c) {
            finite(frame.nodal_force[n][c]);
            frame.monitor.force_balance[c] += frame.nodal_force[n][c];
            if (analysis == Analysis::Implicit) {
                const auto eq = dofs->equation(n, c);
                if (eq >= 0)
                    frame.residual[static_cast<std::size_t>(eq)] = -frame.nodal_force[n][c];
            }
        }
    std::stable_sort(frame.tangent.begin(), frame.tangent.end(), [](const auto &a, const auto &b) {
        return std::tie(a.row, a.column) < std::tie(b.row, b.column);
    });
    std::vector<Triplet<T>> merged;
    for (const auto &entry : frame.tangent) {
        if (!merged.empty() && merged.back().row == entry.row && merged.back().column == entry.column)
            merged.back().value += entry.value;
        else
            merged.push_back(entry);
    }
    for (const auto &e : merged)
        finite(e.value);
    finite(frame.monitor.elastic_energy);
    finite(frame.monitor.friction_increment);
    for (T x : frame.monitor.force_balance)
        finite(x);
    frame.tangent = std::move(merged);
    db.stage(std::move(records)); // only publish a completely validated result
    frame.origin = db.binding();
    frame.state_epoch = db.epoch();
    return frame;
}
// Contact-only undamped penalty frequency bound, assembled over ALL simultaneous witnesses.
// Global integration must also account for material stiffness, damping and constraints.
template <class T>
T penalty_step_bound(std::span<const Witness<T>> witnesses, std::span<const Property<T>> properties,
                     std::span<const T> inverse_mass, std::uint64_t epoch) {
    std::map<Id, Property<T>> laws;
    for (const auto &p : properties) {
        p.validate();
        if (p.friction != 0)
            throw std::invalid_argument("step bound supports frictionless contact only");
        if (!laws.emplace(p.id, p).second)
            throw std::invalid_argument("duplicate contact property");
    }
    for (T m : inverse_mass) {
        finite(m);
        if (m < 0)
            throw std::invalid_argument("negative inverse mass");
    }
    std::vector<T> bound(inverse_mass.size());
    std::set<Key> seen;
    for (const auto &w : witnesses) {
        w.validate(inverse_mass.size(), epoch);
        if (!seen.insert(w.key).second)
            throw std::invalid_argument("duplicate timestep witness");
        const auto &p = laws.at(w.property);
        if (w.gap >= 0)
            continue;
        T sum_weights = 0, normal_l1 = 0;
        for (unsigned i = 0; i < w.count; ++i)
            sum_weights += std::abs(w.weights[i]);
        for (T x : w.normal)
            normal_l1 += std::abs(x);
        const T max_normal = std::max({std::abs(w.normal[0]), std::abs(w.normal[1]), std::abs(w.normal[2])});
        for (unsigned i = 0; i < w.count; ++i)
            bound[w.nodes[i]] += p.normal_penalty * w.area * std::abs(w.weights[i]) * sum_weights *
                                 normal_l1 * max_normal * inverse_mass[w.nodes[i]];
    }
    T maximum = 0;
    for (T b : bound) {
        finite(b);
        maximum = std::max(maximum, b);
    }
    return maximum > 0 ? T(2) / std::sqrt(maximum) : std::numeric_limits<T>::infinity();
}
} // namespace sdb::general_contact
