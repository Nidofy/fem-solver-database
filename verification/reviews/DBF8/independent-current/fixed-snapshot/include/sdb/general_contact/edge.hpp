#pragma once
#include "domain.hpp"

namespace sdb::general_contact {
template <class X> X clamp_unit(const X &x) {
    return scalar_value(x) < 0 ? X(0) : (scalar_value(x) > 1 ? X(1) : x);
}
template <class X> struct EdgeGap {
    X a{}, b{}, distance{};
    std::array<X, 3> normal{};
};
template <class X> EdgeGap<X> edge_gap(const std::array<std::array<X, 3>, 4> &x) {
    using T = decltype(scalar_value(X{}));
    const auto u = difference(x[1], x[0]), v = difference(x[3], x[2]), r = difference(x[0], x[2]);
    const X a = inner(u, u), e = inner(v, v), b = inner(u, v), c = inner(u, r), f = inner(v, r),
            den = a * e - b * b;
    if (scalar_value(a) <= 0 || scalar_value(e) <= 0)
        throw std::invalid_argument("zero length edge constraint");
    X s(0);
    if (scalar_value(den) > T(128) * std::numeric_limits<T>::epsilon() * scalar_value(a * e))
        s = clamp_unit((b * f - c * e) / den);
    X t = (b * s + f) / e;
    if (scalar_value(t) < 0) {
        t = X(0);
        s = clamp_unit(-c / a);
    } else if (scalar_value(t) > 1) {
        t = X(1);
        s = clamp_unit((b - c) / a);
    }
    std::array<X, 3> d;
    for (unsigned i = 0; i < 3; ++i)
        d[i] = r[i] + s * u[i] - t * v[i];
    X distance = root(inner(d, d));
    if (!(scalar_value(distance) > 0) || !std::isfinite(scalar_value(distance)))
        throw std::invalid_argument("intersecting edges need a resolved normal/CCD policy");
    EdgeGap<X> out{s, t, distance, {}};
    for (unsigned i = 0; i < 3; ++i)
        out.normal[i] = d[i] / distance;
    return out;
}
struct EdgePair {
    Index a = invalid_index, b = invalid_index;
    Id interaction = 0, property = 0;
};
template <class T> struct RoundedEdgeContact {
    EdgePair pair;
    T radius_a = 0, radius_b = 0, area = 1;
};
template <class T> Feature edge_feature(const Model<T> &model, Index edge) {
    const auto &e = model.edges().at(edge);
    const auto &face = model.facets().at(e.incident_faces.at(0));
    for (unsigned j = 0; j < face.count; ++j) {
        std::array<Index, 2> nodes{face.nodes[j], face.nodes[(j + 1) % face.count]};
        std::sort(nodes.begin(), nodes.end());
        if (nodes == e.nodes)
            return {FeatureKind::Edge, face.identity.element,
                    static_cast<std::uint32_t>(face.identity.local) * 4 + j, 1};
    }
    throw std::logic_error("edge incidence identity mismatch");
}
template <class T>
std::pair<Witness<T>, GapDerivatives<T>> rounded_edge(const Model<T> &model, const GeometryDB<T> &geometry,
                                                      RoundedEdgeContact<T> contact, bool tangent) {
    if (model.definition().mesh != geometry.model_id())
        throw std::invalid_argument("edge geometry model mismatch");
    if (contact.pair.b < contact.pair.a) {
        std::swap(contact.pair.a, contact.pair.b);
        std::swap(contact.radius_a, contact.radius_b);
    }
    for (T value : {contact.radius_a, contact.radius_b, contact.area})
        finite(value);
    if (contact.radius_a < 0 || contact.radius_b < 0 || contact.radius_a + contact.radius_b <= 0 ||
        contact.area <= 0 || contact.pair.a == contact.pair.b)
        throw std::invalid_argument("invalid rounded edge definition");
    const auto &a = model.edges().at(contact.pair.a);
    const auto &b = model.edges().at(contact.pair.b);
    std::array<Index, 4> nodes{a.nodes[0], a.nodes[1], b.nodes[0], b.nodes[1]};
    std::set<Index> unique(nodes.begin(), nodes.end());
    if (unique.size() != 4)
        throw std::invalid_argument("incident edge contact");
    std::array<Vec<T>, 4> x;
    for (unsigned i = 0; i < 4; ++i)
        x[i] = geometry.current().at(nodes[i]);
    const auto p = edge_gap(x);
    Witness<T> w;
    w.key = {contact.pair.interaction, edge_feature(model, contact.pair.a),
             edge_feature(model, contact.pair.b), 0};
    w.property = contact.pair.property;
    w.geometry_epoch = geometry.revision();
    w.count = 4;
    std::copy(nodes.begin(), nodes.end(), w.nodes.begin());
    w.weights = {T(1) - p.a, p.a, -(T(1) - p.b), -p.b};
    w.normal = p.normal;
    w.gap = p.distance - contact.radius_a - contact.radius_b;
    w.area = contact.area;
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned c = 0; c < 3; ++c)
            w.relative_increment[c] += w.weights[i] * (x[i][c] - geometry.previous().at(nodes[i])[c]);
    w.validate(geometry.current().size(), geometry.revision());
    GapDerivatives<T> d;
    if (tangent) {
        using D = Jet<T, 12>;
        std::array<std::array<D, 3>, 4> dx;
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned c = 0; c < 3; ++c)
                dx[i][c] = D::variable(x[i][c], 3 * i + c);
        const auto gap = edge_gap(dx).distance - D(contact.radius_a + contact.radius_b);
        d.nodes.assign(nodes.begin(), nodes.end());
        d.gap = gap.value;
        d.gradient.assign(gap.gradient.begin(), gap.gradient.end());
        d.hessian.assign(gap.hessian.begin(), gap.hessian.end());
        for (T value : d.gradient)
            finite(value);
        for (T value : d.hessian)
            finite(value);
    }
    return {w, std::move(d)};
}
template <class T>
Frame<T> evaluate_edges(Database<T> &db, const Model<T> &model, const GeometryDB<T> &geometry,
                        std::span<const RoundedEdgeContact<T>> contacts, Analysis analysis,
                        Execution execution = Execution::Serial, const DofMap *dofs = nullptr) {
    if (!db.in_trial())
        throw std::logic_error("edge evaluation requires contact trial");
    db.invalidate_trial();
    if (db.binding().mesh != geometry.model_id() || db.binding().configuration != model.fingerprint())
        throw std::invalid_argument("edge database binding mismatch");
    try {
        std::map<Id, Property<T>> properties;
        for (const auto &p : model.definition().properties)
            properties.emplace(p.id, p);
        std::map<Key, GapDerivatives<T>> derivatives;
        std::vector<Witness<T>> witnesses;
        witnesses.reserve(contacts.size());
        for (const auto &c : contacts) {
            const auto &property = properties.at(c.pair.property);
            if (property.friction != 0)
                throw std::invalid_argument("rounded edge engine currently frictionless");
            auto [w, d] = rounded_edge(model, geometry, c, analysis == Analysis::Implicit);
            if (!derivatives.emplace(w.key, std::move(d)).second)
                throw std::invalid_argument("duplicate edge constraint");
            witnesses.push_back(w);
        }
        auto frame = evaluate<T>(db, witnesses, model.definition().properties, geometry.current().size(),
                                 geometry.revision(), analysis, execution, dofs);
        if (analysis == Analysis::Implicit) {
            std::map<std::pair<std::int64_t, std::int64_t>, T> matrix;
            for (const auto &w : witnesses) {
                if (w.gap >= 0)
                    continue;
                const auto &d = derivatives.at(w.key);
                const auto n = d.gradient.size();
                const T stiffness = w.area * properties.at(w.property).normal_penalty;
                for (std::size_t i = 0; i < n; ++i) {
                    auto row = dofs->equation(d.nodes[i / 3], static_cast<unsigned>(i % 3));
                    if (row < 0)
                        continue;
                    for (std::size_t j = 0; j < n; ++j) {
                        auto col = dofs->equation(d.nodes[j / 3], static_cast<unsigned>(j % 3));
                        if (col < 0)
                            continue;
                        matrix[{row, col}] +=
                            stiffness * (d.gradient[i] * d.gradient[j] + d.gap * d.hessian[i * n + j]);
                    }
                }
            }
            frame.tangent.clear();
            frame.tangent_kind = TangentKind::ConsistentGeometry;
            for (auto [entry, value] : matrix) {
                finite(value);
                if (value != 0)
                    frame.tangent.push_back({entry.first, entry.second, value});
            }
        }
        return frame;
    } catch (...) {
        db.invalidate_trial();
        throw;
    }
}
} // namespace sdb::general_contact
