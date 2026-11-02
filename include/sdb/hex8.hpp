#pragma once
#include "state.hpp"
#include "explicit_controls.hpp"
namespace sdb {
template <class T> struct HexViews {
    View<const Index, 8> nodes;
    View<const T, 24> gradients;
    View<const T> weights, lambda, mu;
    View<const std::size_t> ip_offsets;
    View<const T, 32> hourglass_modes{};
    View<const T> hourglass_stiffness{};
};
template <class T>
T hourglass_amplitude(std::size_t h, unsigned mode, unsigned component, View<const Index, 8> nodes,
                      View<const T, 32> modes, View<const T, 3> u) {
    T amplitude = 0;
    for (unsigned a = 0; a < 8; ++a)
        amplitude += modes(h, 8 * mode + a) * u(nodes(h, a), component);
    return amplitude;
}
template <class T>
void hex_force(std::size_t h, HexViews<T> t, View<const T, 3> u, View<T, 24> local,
               View<T, 6> stress, View<T, 6> strain, bool active) {
    auto conn = t.nodes;
    auto g = t.gradients;
    auto w = t.weights;
    for (unsigned c = 0; c < 24; ++c)
        local(h, c) = 0;
    // Positive weights identify full Gauss slots. Reduced plans reserve slots
    // 1..7 with zero weight so older six-field views cannot masquerade as full.
    const bool reduced = w(8 * h + 1) == 0;
    if (reduced && (h >= t.hourglass_modes.count || h >= t.hourglass_stiffness.count ||
                    !(t.hourglass_stiffness(h) > 0)))
        throw std::invalid_argument("reduced HEX8 view requires hourglass data");
    if (active && reduced) {
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned i = 0; i < 3; ++i) {
                T amplitude = hourglass_amplitude(h, mode, i, conn, t.hourglass_modes, u);
                for (unsigned a = 0; a < 8; ++a)
                    local(h, 3 * a + i) += t.hourglass_stiffness(h) * amplitude *
                                            t.hourglass_modes(h, 8 * mode + a);
            }
    }
    for (unsigned q = 0; q < (reduced ? 1u : 8u); ++q) {
        auto p = 8 * h + q, ip = t.ip_offsets(h) + q;
        if (!active) {
            for (unsigned c = 0; c < 6; ++c) {
                stress(ip, c) = 0;
                strain(ip, c) = 0;
            }
            continue;
        }
        T du[3][3]{};
        for (unsigned a = 0; a < 8; ++a)
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < 3; ++j)
                    du[i][j] += u(conn(h, a), i) * g(p, 3 * a + j);
        T eps[6] = {du[0][0],
                    du[1][1],
                    du[2][2],
                    T(.5) * (du[0][1] + du[1][0]),
                    T(.5) * (du[1][2] + du[2][1]),
                    T(.5) * (du[2][0] + du[0][2])};
        T s[6], trace = eps[0] + eps[1] + eps[2];
        for (unsigned c = 0; c < 6; ++c) {
            s[c] = 2 * t.mu(h) * eps[c] + (c < 3 ? t.lambda(h) * trace : T(0));
            stress(ip, c) = s[c];
            strain(ip, c) = eps[c];
        }
        for (unsigned a = 0; a < 8; ++a) {
            auto gx = g(p, 3 * a), gy = g(p, 3 * a + 1), gz = g(p, 3 * a + 2);
            local(h, 3 * a) += w(p) * (s[0] * gx + s[3] * gy + s[5] * gz);
            local(h, 3 * a + 1) += w(p) * (s[3] * gx + s[1] * gy + s[4] * gz);
            local(h, 3 * a + 2) += w(p) * (s[5] * gx + s[4] * gy + s[2] * gz);
        }
    }
}
// CPU full-integration and uniform-strain small-strain bricks. See the formulation contract in docs.
template <class T> class HexPlan {
  public:
    std::vector<Index> elements;
    std::vector<std::size_t> ip_offsets, adjacency_offsets;
    std::vector<std::uint64_t> adjacency;
    Field<Index, 8> nodes;
    Field<T, 24> gradients, local_force;
    Field<T> weights, lambda, mu;
    Field<T, 32> hourglass_modes;
    Field<T> hourglass_stiffness;
    T stable_step = std::numeric_limits<T>::max();
    HexPlan(const MeshDB<T> &mesh, const StateDB<T> &state, Field<T> &mass_field, Field<T> &dt_field,
            Field<T> &volumes, Field<T> &point_weights, ExplicitControlPlan* controls = nullptr) {
        if (mesh.requires_storage_only())
            throw std::invalid_argument("model contracts require storage-only execution");
        for (Index e = 0; e < mesh.elements(); ++e)
            if (mesh.element(e).topology == Topology::Hex8)
                elements.push_back(e);
        if (std::any_of(elements.begin(), elements.end(), [&](Index e) {
                return mesh.element(e).integration_points == 1;
            })) {
            hourglass_modes.resize(elements.size());
            hourglass_stiffness.resize(elements.size());
        }
        nodes.resize(elements.size());
        gradients.resize(product(elements.size(), 8));
        weights.resize(product(elements.size(), 8));
        local_force.resize(elements.size());
        lambda.resize(elements.size());
        mu.resize(elements.size());
        adjacency_offsets.resize(mesh.nodes() + 1, 0);
        constexpr int signs[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                     {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
        auto x = mesh.reference();
        auto conn = nodes.write();
        auto g = gradients.write();
        auto weight = weights.write();
        auto mass = mass_field.write();
        auto dt = dt_field.write();
        auto volume = volumes.write();
        auto global_weight = point_weights.write();
        for (std::size_t h = 0; h < elements.size(); ++h) {
            auto e = elements[h];
            auto &el = mesh.element(e);
            auto &mat = mesh.materials()[el.material];
            const bool reduced = el.integration_points == 1;
            if ((!reduced && el.integration_points != 8) || mat.model != 1 || !mat.history_schema.empty())
                throw std::invalid_argument("HEX8 requires one or eight IPs and linear elasticity");
            auto ns = mesh.connectivity(e);
            ip_offsets.push_back(state.ip_offsets()[e]);
            for (unsigned a = 0; a < 8; ++a) {
                conn(h, a) = ns[a];
                ++adjacency_offsets[ns[a] + 1];
            }
            auto rho = mat.parameters[0], E = mat.parameters[1], nu = mat.parameters[2];
            T la = E * nu / ((1 + nu) * (1 - 2 * nu)), shear = E / (2 * (1 + nu));
            lambda.write()(h) = la;
            mu.write()(h) = shear;
            T lumped[8]{}, K[24][24]{};
            volume(e) = 0;
            for (unsigned q = 0; q < 8; ++q) {
                T r[3];
                for (unsigned j = 0; j < 3; ++j)
                    r[j] = ((q & (1u << j)) ? T(1) : T(-1)) / std::sqrt(T(3));
                T N[8], dr[8][3], J[3][3]{}, C[3][3];
                for (unsigned a = 0; a < 8; ++a) {
                    N[a] = T(.125);
                    for (unsigned j = 0; j < 3; ++j)
                        N[a] *= 1 + signs[a][j] * r[j];
                    for (unsigned j = 0; j < 3; ++j) {
                        dr[a][j] = T(.125) * signs[a][j];
                        for (unsigned k = 0; k < 3; ++k)
                            if (k != j)
                                dr[a][j] *= 1 + signs[a][k] * r[k];
                        for (unsigned i = 0; i < 3; ++i)
                            J[i][j] += (x(ns[a], i) - x(ns[0], i)) * dr[a][j];
                    }
                }
                T scale = 0;
                for (unsigned i = 0; i < 3; ++i)
                    for (unsigned j = 0; j < 3; ++j) {
                        C[i][j] = J[(i + 1) % 3][(j + 1) % 3] * J[(i + 2) % 3][(j + 2) % 3] -
                                  J[(i + 1) % 3][(j + 2) % 3] * J[(i + 2) % 3][(j + 1) % 3];
                        scale = std::max(scale, std::abs(J[i][j]));
                    }
                T det = 0;
                for (unsigned j = 0; j < 3; ++j)
                    det += J[0][j] * C[0][j];
                if (!std::isfinite(det) ||
                    !(det > 64 * std::numeric_limits<T>::epsilon() * scale * scale * scale))
                    throw std::invalid_argument("invalid HEX8 Jacobian");
                auto p = 8 * h + q;
                weight(p) = det;
                if (!reduced)
                    global_weight(ip_offsets.back() + q) = det;
                volume(e) += det;
                for (unsigned a = 0; a < 8; ++a) {
                    lumped[a] += rho * N[a] * det;
                    for (unsigned i = 0; i < 3; ++i) {
                        T value = 0;
                        for (unsigned j = 0; j < 3; ++j)
                            value += C[i][j] * dr[a][j] / det;
                        if (!std::isfinite(value))
                            throw std::invalid_argument("invalid HEX8 gradient");
                        g(p, 3 * a + i) = value;
                    }
                }
            }
            if (reduced) {
                // Uniform-strain brick: integrate geometry once, evaluate material at one IP.
                const auto first = 8 * h;
                for (unsigned c = 0; c < 24; ++c) {
                    T integral = 0;
                    for (unsigned q = 0; q < 8; ++q)
                        integral += weight(first + q) * g(first + q, c);
                    g(first, c) = integral / volume(e);
                }
                for (unsigned q = 1; q < 8; ++q)
                    weight(first + q) = 0;
                weight(first) = global_weight(ip_offsets.back()) = volume(e);
                const T k = shear * std::cbrt(volume(e)) / T(80);
                if (!(k > 0) || !std::isfinite(k))
                    throw std::invalid_argument("invalid HEX8 hourglass stiffness");
                hourglass_stiffness.write()(h) = k;
                auto modes = hourglass_modes.write();
                for (unsigned m = 0; m < 4; ++m) {
                    T moment[3]{};
                    for (unsigned a = 0; a < 8; ++a) {
                        const auto &v = signs[a];
                        int sign = m == 3 ? v[0] * v[1] * v[2] : v[m] * v[(m + 1) % 3];
                        modes(h, 8 * m + a) = T(sign);
                        for (unsigned i = 0; i < 3; ++i)
                            moment[i] += T(sign) * (x(ns[a], i) - x(ns[0], i));
                    }
                    for (unsigned a = 0; a < 8; ++a)
                        for (unsigned i = 0; i < 3; ++i)
                            modes(h, 8 * m + a) -= g(first, 3 * a + i) * moment[i];
                    for (unsigned a = 0; a < 8; ++a)
                        for (unsigned b = 0; b < 8; ++b)
                            for (unsigned i = 0; i < 3; ++i)
                                K[3 * a + i][3 * b + i] += k * modes(h, 8 * m + a) * modes(h, 8 * m + b);
                }
            }
            for (unsigned q = 0; q < el.integration_points; ++q) {
                auto p = 8 * h + q;
                auto det = weight(p);
                for (unsigned a = 0; a < 8; ++a)
                    for (unsigned b = 0; b < 8; ++b) {
                        T dot = 0;
                        for (unsigned k = 0; k < 3; ++k)
                            dot += g(p, 3 * a + k) * g(p, 3 * b + k);
                        for (unsigned i = 0; i < 3; ++i)
                            for (unsigned j = 0; j < 3; ++j)
                                K[3 * a + i][3 * b + j] += det * (la * g(p, 3 * a + i) * g(p, 3 * b + j) +
                                                                  shear * g(p, 3 * a + j) * g(p, 3 * b + i) +
                                                                  (i == j ? shear * dot : T(0)));
                    }
            }
            T bound = 0;
            for (unsigned a = 0; a < 8; ++a) {
                if (!(lumped[a] > 0) || !std::isfinite(lumped[a]))
                    throw std::invalid_argument("invalid HEX8 mass");

                for (unsigned i = 0; i < 3; ++i) {
                    T row = 0;
                    for (unsigned j = 0; j < 24; ++j)
                        row += std::abs(K[3 * a + i][j]);
                    bound = std::max(bound, row / lumped[a]);
                }
            }
            if (!(bound > 0) || !std::isfinite(bound))
                throw std::invalid_argument("invalid HEX8 stiffness bound");
            dt(e) = T(1.9) / std::sqrt(bound);
            T scale = controls ? controls->configure(e, volume(e), rho, la, shear, dt(e)) : T(1);
            for (unsigned a=0; a<8; ++a)
                mass(ns[a]) += lumped[a]*scale;
            stable_step = std::min(stable_step, dt(e));
        }
        for (std::size_t n = 1; n < adjacency_offsets.size(); ++n)
            adjacency_offsets[n] += adjacency_offsets[n - 1];
        adjacency.resize(adjacency_offsets.back());
        auto cursor = adjacency_offsets;
        for (std::size_t h = 0; h < elements.size(); ++h)
            for (unsigned a = 0; a < 8; ++a)
                adjacency[cursor[conn(h, a)]++] = 8 * h + a;
    }
    double artificial_energy(View<const T, 3> u, View<const std::uint8_t> active) const {
        double energy = 0;
        const auto k = hourglass_stiffness.read();
        for (std::size_t h = 0; h < k.count; ++h)
            if (k(h) > 0 && active(elements[h]))
                for (unsigned m = 0; m < 4; ++m)
                    for (unsigned i = 0; i < 3; ++i) {
                        double q = hourglass_amplitude(h, m, i, nodes.read(), hourglass_modes.read(), u);
                        energy += .5 * k(h) * q * q;
                    }
        return energy;
    }
    HexViews<T> views() const {
        return {nodes.read(), gradients.read(), weights.read(), lambda.read(), mu.read(),
                {ip_offsets.data(), ip_offsets.size()}, hourglass_modes.read(), hourglass_stiffness.read()};
    }
    void force(std::size_t h, View<const T, 3> u, View<T, 24> local, View<T, 6> stress, View<T, 6> strain,
               bool active) const {
        hex_force(h, views(), u, local, stress, strain, active);
    }
};
} // namespace sdb
