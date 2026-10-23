#pragma once
#include "sdb/dynamics.hpp"

// Equivalent arithmetic/order to tet_force and fixed-order node gather, with direct
// contiguous pointer indexing. No Field/View dereference occurs in this baseline.
template <class T>
void raw_tet_forces(std::size_t ne, std::size_t nn, const sdb::Index *nodes, const T *gradients,
                    const T *volume, const T *lambda, const T *mu, const T *u, T *local, T *stress, T *strain,
                    const std::size_t *offsets, const std::uint64_t *adjacency, T *force,
                    sdb::CpuPolicy policy, const std::uint8_t *active) {
    sdb::parallel_for(ne, policy, [=](std::size_t e) {
        if (!active[e]) {
            for (unsigned c = 0; c < 12; ++c)
                local[c * ne + e] = 0;
            for (unsigned c = 0; c < 6; ++c) {
                stress[c * ne + e] = 0;
                strain[c * ne + e] = 0;
            }
            return;
        }
        T du[3][3]{};
        for (unsigned a = 0; a < 4; ++a)
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < 3; ++j)
                    du[i][j] += u[i * nn + nodes[a * ne + e]] * gradients[(a * 3 + j) * ne + e];
        T eps[6] = {du[0][0],
                    du[1][1],
                    du[2][2],
                    T(.5) * (du[0][1] + du[1][0]),
                    T(.5) * (du[1][2] + du[2][1]),
                    T(.5) * (du[2][0] + du[0][2])};
        T s[6];
        T trace = eps[0] + eps[1] + eps[2];
        for (unsigned c = 0; c < 6; ++c) {
            s[c] = 2 * mu[e] * eps[c] + (c < 3 ? lambda[e] * trace : T(0));
            stress[c * ne + e] = s[c];
            strain[c * ne + e] = eps[c];
        }
        for (unsigned a = 0; a < 4; ++a) {
            T gx = gradients[3 * a * ne + e], gy = gradients[(3 * a + 1) * ne + e],
              gz = gradients[(3 * a + 2) * ne + e], v = volume[e];
            local[3 * a * ne + e] = v * (s[0] * gx + s[3] * gy + s[5] * gz);
            local[(3 * a + 1) * ne + e] = v * (s[3] * gx + s[1] * gy + s[4] * gz);
            local[(3 * a + 2) * ne + e] = v * (s[5] * gx + s[4] * gy + s[2] * gz);
        }
    });
    sdb::parallel_for(nn, policy, [=](std::size_t n) {
        for (unsigned c = 0; c < 3; ++c) {
            T f = 0;
            for (auto j = offsets[n]; j < offsets[n + 1]; ++j) {
                auto a = adjacency[j];
                f += local[((a % 4) * 3 + c) * ne + a / 4];
            }
            force[c * nn + n] = f;
        }
    });
}
