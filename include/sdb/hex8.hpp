#pragma once
#include "state.hpp"
namespace sdb {
// CPU full-integration small-strain brick. See the formulation contract in docs.
template <class T> class HexPlan {
  public:
    std::vector<Index> elements;
    std::vector<std::size_t> ip_offsets, adjacency_offsets;
    std::vector<std::uint64_t> adjacency;
    Field<Index, 8> nodes;
    Field<T, 24> gradients, local_force;
    Field<T> weights, lambda, mu;
    T stable_step = std::numeric_limits<T>::max();
    HexPlan(const MeshDB<T> &mesh, const StateDB<T> &state, Field<T> &mass_field, Field<T> &dt_field,
            Field<T> &volumes, Field<T> &point_weights) {
        for (Index e = 0; e < mesh.elements(); ++e)
            if (mesh.element(e).topology == Topology::Hex8)
                elements.push_back(e);
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
            if (el.integration_points != 8 || mat.model != 1 || !mat.history_schema.empty())
                throw std::invalid_argument("HEX8 full eight-IP linear kernel required");
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
                mass(ns[a]) += lumped[a];
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
    void force(std::size_t h, View<const T, 3> u, View<T, 24> local, View<T, 6> stress, View<T, 6> strain,
               bool active) const {
        auto conn = nodes.read();
        auto g = gradients.read();
        auto w = weights.read();
        for (unsigned c = 0; c < 24; ++c)
            local(h, c) = 0;
        for (unsigned q = 0; q < 8; ++q) {
            auto p = 8 * h + q, ip = ip_offsets[h] + q;
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
                s[c] = 2 * mu.read()(h) * eps[c] + (c < 3 ? lambda.read()(h) * trace : T(0));
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
};
} // namespace sdb
