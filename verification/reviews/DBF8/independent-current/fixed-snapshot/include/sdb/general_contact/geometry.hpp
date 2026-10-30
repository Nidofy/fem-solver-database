#pragma once
#include "search.hpp"

namespace sdb::general_contact {
// Second-order forward differentiation for a local node/facet stencil (at most 15 DOFs).
// These are local geometric derivatives, not finite differences of assembled force.
template <class T, std::size_t N> struct Jet {
    T value{};
    std::array<T, N> gradient{};
    std::array<T, N * N> hessian{};
    Jet() = default;
    Jet(T v) : value(v) {}
    static Jet variable(T v, std::size_t i) {
        Jet r(v);
        r.gradient.at(i) = 1;
        return r;
    }
};
template <class T, std::size_t N> Jet<T, N> operator+(const Jet<T, N> &a, const Jet<T, N> &b) {
    Jet<T, N> r(a.value + b.value);
    for (std::size_t i = 0; i < N; ++i)
        r.gradient[i] = a.gradient[i] + b.gradient[i];
    for (std::size_t i = 0; i < N * N; ++i)
        r.hessian[i] = a.hessian[i] + b.hessian[i];
    return r;
}
template <class T, std::size_t N> Jet<T, N> operator-(const Jet<T, N> &a, const Jet<T, N> &b) {
    Jet<T, N> r(a.value - b.value);
    for (std::size_t i = 0; i < N; ++i)
        r.gradient[i] = a.gradient[i] - b.gradient[i];
    for (std::size_t i = 0; i < N * N; ++i)
        r.hessian[i] = a.hessian[i] - b.hessian[i];
    return r;
}
template <class T, std::size_t N> Jet<T, N> operator-(const Jet<T, N> &a) {
    return Jet<T, N>(0) - a;
}
template <class T, std::size_t N> Jet<T, N> operator*(const Jet<T, N> &a, const Jet<T, N> &b) {
    Jet<T, N> r(a.value * b.value);
    for (std::size_t i = 0; i < N; ++i) {
        r.gradient[i] = a.gradient[i] * b.value + a.value * b.gradient[i];
        for (std::size_t j = 0; j < N; ++j)
            r.hessian[i * N + j] = a.hessian[i * N + j] * b.value + a.value * b.hessian[i * N + j] +
                                   a.gradient[i] * b.gradient[j] + b.gradient[i] * a.gradient[j];
    }
    return r;
}
template <class T, std::size_t N> Jet<T, N> unary(const Jet<T, N> &a, T value, T first, T second) {
    Jet<T, N> r(value);
    for (std::size_t i = 0; i < N; ++i) {
        r.gradient[i] = first * a.gradient[i];
        for (std::size_t j = 0; j < N; ++j)
            r.hessian[i * N + j] = first * a.hessian[i * N + j] + second * a.gradient[i] * a.gradient[j];
    }
    return r;
}
template <class T, std::size_t N> Jet<T, N> operator/(const Jet<T, N> &a, const Jet<T, N> &b) {
    const T inv = T(1) / b.value;
    return a * unary(b, inv, -inv * inv, T(2) * inv * inv * inv);
}
template <class T> T scalar_value(T x) {
    return x;
}
template <class T, std::size_t N> T scalar_value(const Jet<T, N> &x) {
    return x.value;
}
template <class T> T root(T x) {
    return std::sqrt(x);
}
template <class T, std::size_t N> Jet<T, N> root(const Jet<T, N> &x) {
    T r = std::sqrt(x.value);
    return unary(x, r, T(.5) / r, -T(.25) / (x.value * r));
}
template <class X> X inner(const std::array<X, 3> &a, const std::array<X, 3> &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
template <class X> std::array<X, 3> difference(const std::array<X, 3> &a, const std::array<X, 3> &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
template <class X> std::array<X, 3> vector_product(const std::array<X, 3> &a, const std::array<X, 3> &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
template <class X> std::array<X, 3> unit(const std::array<X, 3> &a) {
    auto length = root(inner(a, a));
    return {a[0] / length, a[1] / length, a[2] / length};
}
template <class X> struct Projection {
    X gap{};
    std::array<X, 4> weights{};
    std::array<X, 3> normal{}, tangent{};
    X u{}, v{};
    bool inside = false;
};
// Interior closest-point projection; boundary/edge ownership is handled separately.
// For a bilinear quad, Newton solves the orthogonality equations in its REAL parametric domain.
template <class X>
Projection<X> project(const std::array<X, 3> &point, const std::array<std::array<X, 3>, 4> &x,
                      unsigned count) {
    using T = decltype(scalar_value(X{}));
    if (count != 3 && count != 4)
        throw std::invalid_argument("projection requires triangle/quad");
    Projection<X> r;
    const auto e = difference(x[1], x[0]), f = difference(x[count == 3 ? 2 : 3], x[0]);
    if (count == 3) {
        auto n = vector_product(e, f);
        const auto n2 = inner(n, n), scale = inner(e, e) * inner(f, f);
        if (scalar_value(n2) <= T(256) * std::numeric_limits<T>::epsilon() * scalar_value(scale) ||
            scalar_value(n2) <= 0)
            throw std::invalid_argument("degenerate contact triangle");
        auto d = difference(point, x[0]);
        r.normal = unit(n);
        r.tangent = unit(e);
        r.gap = inner(d, r.normal);
        auto ee = inner(e, e), ef = inner(e, f), ff = inner(f, f), de = inner(d, e), df = inner(d, f);
        auto determinant = ee * ff - ef * ef;
        r.u = (de * ff - df * ef) / determinant;
        r.v = (df * ee - de * ef) / determinant;
        r.weights = {X(1) - r.u - r.v, r.u, r.v, X(0)};
        r.inside = scalar_value(r.weights[0]) >= 0 && scalar_value(r.u) >= 0 && scalar_value(r.v) >= 0;
        return r;
    }
    std::array<X, 3> twist;
    for (unsigned c = 0; c < 3; ++c)
        twist[c] = x[0][c] - x[1][c] + x[2][c] - x[3][c];
    // Reject folded/inverted parameterizations before solving. Normal at corners must agree.
    auto reference = vector_product(e, f);
    const T reference_norm = std::sqrt(scalar_value(inner(reference, reference)));
    if (!(reference_norm > 0) || !std::isfinite(reference_norm))
        throw std::invalid_argument("degenerate quad corner");
    for (unsigned corner = 0; corner < 4; ++corner) {
        std::array<X, 3> a, b;
        for (unsigned c = 0; c < 3; ++c) {
            a[c] = e[c] + X(corner / 2) * twist[c];
            b[c] = f[c] + X(corner % 2) * twist[c];
        }
        auto n = vector_product(a, b);
        T square = scalar_value(inner(n, n));
        if (!(square > 0) || !std::isfinite(square) ||
            scalar_value(inner(n, reference)) <=
                T(128) * std::numeric_limits<T>::epsilon() * reference_norm * std::sqrt(square))
            throw std::invalid_argument("folded or degenerate contact quad");
    }
    X u(T(.5)), v(T(.5));
    bool converged = false;
    unsigned extra = 0;
    for (unsigned iteration = 0; iteration < 40; ++iteration) {
        std::array<X, 3> q, a, b, d;
        for (unsigned c = 0; c < 3; ++c) {
            q[c] = x[0][c] + u * e[c] + v * f[c] + u * v * twist[c];
            a[c] = e[c] + v * twist[c];
            b[c] = f[c] + u * twist[c];
            d[c] = q[c] - point[c];
        }
        const X aa = inner(a, a), bb = inner(b, b), ab = inner(a, b) + inner(d, twist),
                det = aa * bb - ab * ab;
        if (!(scalar_value(det) > T(128) * std::numeric_limits<T>::epsilon() * scalar_value(aa * bb)))
            throw std::runtime_error("nonunique/ill-conditioned quad closest point");
        const X fa = inner(d, a), fb = inner(d, b), du = (bb * fa - ab * fb) / det,
                dv = (aa * fb - ab * fa) / det;
        u = u - du;
        v = v - dv;
        const T step = std::max(std::abs(scalar_value(du)), std::abs(scalar_value(dv)));
        if (!std::isfinite(step) || std::abs(scalar_value(u)) > T(1e6) || std::abs(scalar_value(v)) > T(1e6))
            throw std::runtime_error("quad projection diverged");
        if (step < T(32) * std::numeric_limits<T>::epsilon()) {
            converged = true;
            if (++extra == 3)
                break; // converge differentiated Newton iterates as well
        } else {
            extra = 0;
            converged = false;
        }
    }
    if (!converged)
        throw std::runtime_error("quad projection did not converge");
    r.u = u;
    r.v = v;
    r.weights = {(X(1) - u) * (X(1) - v), u * (X(1) - v), u * v, (X(1) - u) * v};
    std::array<X, 3> q{}, a, b;
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned j = 0; j < 4; ++j)
            q[c] = q[c] + r.weights[j] * x[j][c];
        a[c] = e[c] + v * twist[c];
        b[c] = f[c] + u * twist[c];
    }
    r.normal = unit(vector_product(a, b));
    r.tangent = unit(a);
    r.gap = inner(difference(point, q), r.normal);
    r.inside = scalar_value(u) >= 0 && scalar_value(u) <= 1 && scalar_value(v) >= 0 && scalar_value(v) <= 1;
    return r;
}
template <class T> class GeometryDB {
    inline static std::atomic<std::uint64_t> next_identity_{1};
    std::uint64_t identity_ = next_identity_.fetch_add(1);
    std::uint64_t mesh_ = 0, revision_ = 0;
    std::vector<Vec<T>> current_, previous_;

  public:
    GeometryDB(const GeometryDB &) = delete;
    GeometryDB &operator=(const GeometryDB &) = delete;
    explicit GeometryDB(const MeshDB<T> &mesh)
        : mesh_(mesh.fingerprint()), current_(mesh.nodes()), previous_(mesh.nodes()) {
        for (Index i = 0; i < mesh.nodes(); ++i)
            for (unsigned c = 0; c < 3; ++c)
                current_[i][c] = previous_[i][c] = mesh.reference()(i, c);
    }
    void update(const MeshDB<T> &mesh, std::span<const Vec<T>> current, std::span<const Vec<T>> previous) {
        if (mesh.fingerprint() != mesh_ || current.size() != current_.size() ||
            previous.size() != previous_.size())
            throw std::invalid_argument("geometry database model/shape mismatch");
        for (auto positions : {current, previous})
            for (const auto &x : positions)
                for (T v : x)
                    finite(v);
        std::vector<Vec<T>> next(current.begin(), current.end()), old(previous.begin(), previous.end());
        current_.swap(next);
        previous_.swap(old);
        ++revision_;
    }
    std::uint64_t model_id() const { return mesh_; }
    std::uint64_t identity() const { return identity_; }
    std::uint64_t revision() const { return revision_; }
    const std::vector<Vec<T>> &current() const { return current_; }
    const std::vector<Vec<T>> &previous() const { return previous_; }
};
template <class T> struct GapDerivatives {
    std::vector<Index> nodes;
    std::vector<T> gradient, hessian; // row-major; independent of mesh DOF constraints
    T gap = 0;
};
template <class T>
GapDerivatives<T> differentiate_gap(const GeometryDB<T> &geometry, Index point, const Facet &face) {
    using D = Jet<T, 15>;
    std::array<D, 3> p;
    std::array<std::array<D, 3>, 4> x{};
    if (face.count != 3 && face.count != 4)
        throw std::invalid_argument("gap derivative facet arity");
    GapDerivatives<T> r;
    r.nodes.push_back(point);
    for (unsigned j = 0; j < face.count; ++j)
        r.nodes.push_back(face.nodes[j]);
    std::set<Index> unique(r.nodes.begin(), r.nodes.end());
    if (unique.size() != r.nodes.size())
        throw std::invalid_argument("incident node/facet derivative");
    for (unsigned c = 0; c < 3; ++c)
        p[c] = D::variable(geometry.current().at(point)[c], c);
    for (unsigned j = 0; j < face.count; ++j)
        for (unsigned c = 0; c < 3; ++c)
            x[j][c] = D::variable(geometry.current().at(face.nodes[j])[c], 3 * (j + 1) + c);
    auto projection = project(p, x, face.count);
    if (!projection.inside)
        throw std::invalid_argument("gap derivative outside facet");
    const auto n = r.nodes.size() * 3;
    r.gap = projection.gap.value;
    finite(r.gap);
    r.gradient.resize(n);
    r.hessian.resize(n * n);
    for (std::size_t i = 0; i < n; ++i) {
        r.gradient[i] = projection.gap.gradient[i];
        finite(r.gradient[i]);
        for (std::size_t j = 0; j < n; ++j) {
            r.hessian[i * n + j] = projection.gap.hessian[i * 15 + j];
            finite(r.hessian[i * n + j]);
        }
    }
    return r;
}
template <class T>
std::array<T, 9> frame_rotation(const Projection<T> &current, const Projection<T> &previous) {
    const auto ct = cross(current.normal, current.tangent), pt = cross(previous.normal, previous.tangent);
    std::array<T, 9> R{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            R[i * 3 + j] = current.tangent[i] * previous.tangent[j] + ct[i] * pt[j] +
                           current.normal[i] * previous.normal[j];
    return R;
}
template <class T> struct LocalJacobian {
    std::vector<Index> nodes;
    std::vector<T> values; // d(-nodal contact force)/d(current coordinates), row-major
};
// Differentiate the same objective penalty/Coulomb force used by mechanics.hpp.
// Branches are held fixed locally; at contact/stick/slip transitions this is a one-sided tangent.
template <class T>
LocalJacobian<T> differentiate_surface_force(const GeometryDB<T> &geometry, Index node, const Facet &face,
                                             const Property<T> &law, const History<T> &history, T area) {
    using D = Jet<T, 15>;
    if (face.count != 3 && face.count != 4)
        throw std::invalid_argument("force derivative facet arity");
    law.validate();
    finite(area);
    if (area <= 0)
        throw std::invalid_argument("force derivative area");
    LocalJacobian<T> result;
    result.nodes.push_back(node);
    for (unsigned j = 0; j < face.count; ++j)
        result.nodes.push_back(face.nodes[j]);
    if (std::set<Index>(result.nodes.begin(), result.nodes.end()).size() != result.nodes.size())
        throw std::invalid_argument("incident force derivative stencil");
    const auto size = 3 * result.nodes.size();
    result.values.resize(size * size);
    std::array<D, 3> point;
    std::array<std::array<D, 3>, 4> coords{};
    std::array<Vec<T>, 4> old{};
    for (unsigned c = 0; c < 3; ++c)
        point[c] = D::variable(geometry.current().at(node)[c], c);
    for (unsigned j = 0; j < face.count; ++j)
        for (unsigned c = 0; c < 3; ++c) {
            coords[j][c] = D::variable(geometry.current().at(face.nodes[j])[c], 3 * (j + 1) + c);
            old[j][c] = geometry.previous().at(face.nodes[j])[c];
        }
    const auto now = project(point, coords, face.count);
    if (!now.inside)
        throw std::invalid_argument("force derivative outside facet");
    if (now.gap.value >= 0)
        return result;
    const auto before = project(geometry.previous().at(node), old, face.count);
    const auto second = vector_product(now.normal, now.tangent);
    const auto old_second = vector_product(before.normal, before.tangent);
    std::array<D, 9> rotation;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            rotation[3 * i + j] = now.tangent[i] * D(before.tangent[j]) + second[i] * D(old_second[j]) +
                                  now.normal[i] * D(before.normal[j]);
    std::array<D, 3> relative = point, prior{}, spring{};
    for (unsigned c = 0; c < 3; ++c) {
        prior[c] = D(geometry.previous().at(node)[c]);
        for (unsigned j = 0; j < face.count; ++j) {
            relative[c] = relative[c] - now.weights[j] * coords[j][c];
            prior[c] = prior[c] - now.weights[j] * D(old[j][c]);
        }
    }
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned j = 0; j < 3; ++j) {
            relative[c] = relative[c] - rotation[3 * c + j] * prior[j];
            spring[c] = spring[c] + rotation[3 * c + j] * D(history.elastic_slip[j]);
        }
    const auto normal_increment = inner(relative, now.normal);
    for (unsigned c = 0; c < 3; ++c)
        spring[c] = spring[c] + relative[c] - normal_increment * now.normal[c];
    const D pressure = -D(law.normal_penalty) * now.gap;
    D scale(0);
    if (law.friction > 0) {
        scale = D(1);
        const auto square = inner(spring, spring);
        if (square.value > 0) {
            const auto length = root(square);
            if (law.tangential_penalty * length.value > law.friction * pressure.value)
                scale = D(law.friction) * pressure / (D(law.tangential_penalty) * length);
        }
    }
    for (std::size_t a = 0; a < result.nodes.size(); ++a)
        for (unsigned c = 0; c < 3; ++c) {
            const D weight = a == 0 ? D(1) : -now.weights[a - 1];
            const D force =
                D(area) * weight * (pressure * now.normal[c] - D(law.tangential_penalty) * scale * spring[c]);
            for (std::size_t b = 0; b < size; ++b) {
                auto value = -force.gradient[b];
                finite(value);
                result.values[(3 * a + c) * size + b] = value;
            }
        }
    return result;
}
template <class T> struct EdgeProjection {
    T a = 0, b = 0, distance = 0;
    Vec<T> normal{};
};
// Segment-segment closest points including endpoint and parallel branches. A radius is a
// constitutive/geometric choice of the caller, not inferred from a zero-thickness mesh edge.
template <class T>
EdgeProjection<T> closest_edges(const Vec<T> &a0, const Vec<T> &a1, const Vec<T> &b0, const Vec<T> &b1) {
    const auto u = subtract(a1, a0), v = subtract(b1, b0), r = subtract(a0, b0);
    T a = dot(u, u), e = dot(v, v), b = dot(u, v), c = dot(u, r), f = dot(v, r);
    for (T x : {a, e, b, c, f})
        finite(x);
    if (a <= 0 || e <= 0)
        throw std::invalid_argument("zero length contact edge");
    T s = 0, t = 0, den = a * e - b * b;
    finite(den);
    if (den > T(128) * std::numeric_limits<T>::epsilon() * a * e)
        s = std::clamp((b * f - c * e) / den, T(0), T(1));
    t = (b * s + f) / e;
    if (t < 0) {
        t = 0;
        s = std::clamp(-c / a, T(0), T(1));
    } else if (t > 1) {
        t = 1;
        s = std::clamp((b - c) / a, T(0), T(1));
    }
    Vec<T> d;
    for (unsigned i = 0; i < 3; ++i)
        d[i] = r[i] + s * u[i] - t * v[i];
    T distance = std::sqrt(dot(d, d));
    finite(distance);
    EdgeProjection<T> out{s, t, distance, {}};
    if (distance > 0)
        for (unsigned i = 0; i < 3; ++i)
            out.normal[i] = d[i] / distance;
    return out;
}
// Exact segment-plane crossing against a STATIC triangle, with barycentric inside test.
// General deforming-triangle/edge CCD is a different capability and is not approximated here.
template <class T>
std::optional<T> sweep_static_triangle(const Vec<T> &start, const Vec<T> &end,
                                       const std::array<Vec<T>, 4> &triangle) {
    for (const auto &v : triangle)
        for (T x : v)
            finite(x);
    for (const auto &v : {start, end})
        for (T x : v)
            finite(x);
    const auto a = project(start, triangle, 3), b = project(end, triangle, 3);
    if (a.gap <= 0)
        throw std::invalid_argument("CCD requires initially separated positive side");
    if (b.gap > 0)
        return std::nullopt;
    const T time = a.gap / (a.gap - b.gap);
    Vec<T> x;
    for (unsigned c = 0; c < 3; ++c)
        x[c] = start[c] + time * (end[c] - start[c]);
    auto hit = project(x, triangle, 3);
    return hit.inside ? std::optional<T>(time) : std::nullopt;
}
} // namespace sdb::general_contact
