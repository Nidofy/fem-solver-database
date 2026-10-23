#pragma once
#include "state.hpp"
#include <chrono>

namespace sdb {
enum class CpuPolicy { Serial, OpenMP };
enum class Assembly { Deterministic, Atomic };
template <class F> void parallel_for(std::size_t count, CpuPolicy policy, F &&fn) {
#ifndef _OPENMP
    if (policy == CpuPolicy::OpenMP)
        throw std::runtime_error("OpenMP backend not compiled");
#else
    if (policy == CpuPolicy::OpenMP) {
#pragma omp parallel for schedule(static)
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(count); ++i)
            fn(static_cast<std::size_t>(i));
        return;
    }
#endif
    for (std::size_t i = 0; i < count; ++i)
        fn(i);
}
template <class T> struct TetViews {
    View<const Index, 4> nodes;
    View<const T, 12> gradients;
    View<const T> volume, lambda, mu;
};
// Tensor order xx,yy,zz,xy,yz,zx. Strain stores tensor shear, not engineering shear.
template <class T>
SDB_HD void tet_force(std::size_t e, TetViews<T> t, View<const T, 3> u, View<T, 12> local, View<T, 6> stress,
                      View<T, 6> strain) {
    T du[3][3]{};
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = 0; j < 3; ++j)
                du[i][j] += u(t.nodes(e, a), i) * t.gradients(e, a * 3 + j);
    T eps[6] = {du[0][0],
                du[1][1],
                du[2][2],
                T(.5) * (du[0][1] + du[1][0]),
                T(.5) * (du[1][2] + du[2][1]),
                T(.5) * (du[2][0] + du[0][2])};
    T s[6];
    const T trace = eps[0] + eps[1] + eps[2];
    for (unsigned c = 0; c < 6; ++c) {
        s[c] = 2 * t.mu(e) * eps[c] + (c < 3 ? t.lambda(e) * trace : T(0));
        stress(e, c) = s[c];
        strain(e, c) = eps[c];
    }
    for (unsigned a = 0; a < 4; ++a) {
        const T gx = t.gradients(e, 3 * a), gy = t.gradients(e, 3 * a + 1), gz = t.gradients(e, 3 * a + 2),
                v = t.volume(e);
        local(e, 3 * a) = v * (s[0] * gx + s[3] * gy + s[5] * gz);
        local(e, 3 * a + 1) = v * (s[3] * gx + s[1] * gy + s[4] * gz);
        local(e, 3 * a + 2) = v * (s[5] * gx + s[4] * gy + s[2] * gz);
    }
}
template <class T> class TetPlan {
  public:
    Field<Index, 4> nodes;
    Field<T, 12> gradients;
    Field<T> volume, lambda, mu;
    Field<T, 12> local_force;
    Field<std::uint8_t, 3> fixed;
    AlignedVector<std::size_t> adjacency_offsets;
    AlignedVector<std::uint64_t> adjacency; // packed 4*element+local-node
    T stable_step = std::numeric_limits<T>::max();
    std::uint64_t model_id;
    explicit TetPlan(const MeshDB<T> &mesh, StateDB<T> &state)
        : nodes(mesh.elements()), gradients(mesh.elements()), volume(mesh.elements()),
          lambda(mesh.elements()), mu(mesh.elements()), local_force(mesh.elements()), fixed(mesh.nodes()),
          model_id(mesh.fingerprint()) {
        if (state.model_id() != model_id)
            throw std::invalid_argument("state model mismatch");
        for (const auto &c : mesh.constraints()) {
            if (c.component >= 3 || c.value != 0)
                throw std::invalid_argument("TET example supports homogeneous translation constraints only");
            fixed.write()(c.node, c.component) = 1;
        }
        state.validate_shape();
        auto &s = state.data();
        Field<T> computed_mass(mesh.nodes()), computed_dt(mesh.elements());
        auto mass = computed_mass.write();
        auto conn = nodes.write();
        auto grad = gradients.write();
        auto vol = volume.write();
        auto la = lambda.write();
        auto shear = mu.write();
        auto dt = computed_dt.write();
        adjacency_offsets.resize(mesh.nodes() + 1, 0);
        const auto x = mesh.reference();
        for (Index e = 0; e < mesh.elements(); ++e) {
            const auto &el = mesh.element(e);
            const auto &mat = mesh.materials()[el.material];
            if (el.topology != Topology::Tet4 || el.integration_points != 1 || mat.model != 1)
                throw std::invalid_argument("TET4 linear-elastic one-IP kernel required");
            auto ns = mesh.connectivity(e);
            T a[3], b[3], c[3], cross_bc[3], cross_ca[3], cross_ab[3];
            for (int j = 0; j < 3; ++j) {
                a[j] = x(ns[1], j) - x(ns[0], j);
                b[j] = x(ns[2], j) - x(ns[0], j);
                c[j] = x(ns[3], j) - x(ns[0], j);
            }
            for (int j = 0; j < 3; ++j) {
                int k = (j + 1) % 3, l = (j + 2) % 3;
                cross_bc[j] = b[k] * c[l] - b[l] * c[k];
                cross_ca[j] = c[k] * a[l] - c[l] * a[k];
                cross_ab[j] = a[k] * b[l] - a[l] * b[k];
            }
            T det = 0;
            for (int j = 0; j < 3; ++j)
                det += a[j] * cross_bc[j];
            if (!(det > 0) || !std::isfinite(det))
                throw std::invalid_argument("invalid TET Jacobian");
            vol(e) = det / 6;
            const auto rho = mat.parameters[0], E = mat.parameters[1], nu = mat.parameters[2];
            la(e) = E * nu / ((1 + nu) * (1 - 2 * nu));
            shear(e) = E / (2 * (1 + nu));
            T maxnorm = 0;
            for (int j = 0; j < 3; ++j) {
                grad(e, 3 + j) = cross_bc[j] / det;
                grad(e, 6 + j) = cross_ca[j] / det;
                grad(e, 9 + j) = cross_ab[j] / det;
                grad(e, j) = -grad(e, 3 + j) - grad(e, 6 + j) - grad(e, 9 + j);
            }
            for (unsigned k = 0; k < 4; ++k) {
                T norm = 0;
                for (int j = 0; j < 3; ++j)
                    norm += grad(e, 3 * k + j) * grad(e, 3 * k + j);
                maxnorm = std::max(maxnorm, norm);
                conn(e, k) = ns[k];
                mass(ns[k]) += rho * vol(e) / 4;
                ++adjacency_offsets[ns[k] + 1];
            }
            // Conservative bound from ||B|| and lumped mass; caller still chooses dt below bound.
            dt(e) = T(.25) / (std::sqrt(maxnorm) * std::sqrt((la(e) + 2 * shear(e)) / rho));
            stable_step = std::min(stable_step, dt(e));
        }
        if (s.dt == 0 && s.step == 0) {
            s.mass = std::move(computed_mass);
            s.stable_dt = std::move(computed_dt);
            s.inverse_mass.fill(0);
            mass = s.mass.write();
            auto inv = s.inverse_mass.write();
            for (Index n = 0; n < mesh.nodes(); ++n) {
                if (mass(n) > 0)
                    inv(n) = 1 / mass(n);
                else if (s.node_active.read()(n))
                    throw std::invalid_argument("active node has no mass");
            }
        }
        for (std::size_t n = 1; n < adjacency_offsets.size(); ++n)
            adjacency_offsets[n] += adjacency_offsets[n - 1];
        adjacency.resize(adjacency_offsets.back());
        auto cursor = adjacency_offsets;
        for (Index e = 0; e < mesh.elements(); ++e)
            for (unsigned a = 0; a < 4; ++a)
                adjacency[cursor[conn(e, a)]++] = std::uint64_t(e) * 4 + a;
    }
    TetViews<T> views() const {
        return {nodes.read(), gradients.read(), volume.read(), lambda.read(), mu.read()};
    }
};
template <class T> class CpuExplicit {
    TetPlan<T> plan_;
    CpuPolicy policy_;
    Assembly assembly_;
    std::uint64_t force_evaluations_ = 0;

  public:
    CpuExplicit(const MeshDB<T> &mesh, StateDB<T> &state, CpuPolicy policy = CpuPolicy::Serial,
                Assembly assembly = Assembly::Deterministic)
        : plan_(mesh, state), policy_(policy), assembly_(assembly) {
#ifndef _OPENMP
        if (policy == CpuPolicy::OpenMP)
            throw std::runtime_error("OpenMP not compiled");
#endif
    }
    const TetPlan<T> &plan() const { return plan_; }
    // Completed element+assembly passes; observable for output-scheduling contracts.
    std::uint64_t force_evaluations() const noexcept { return force_evaluations_; }
    // Step-level homogeneous translation mask. No parser or result dependency.
    void set_constraints(std::span<const Constraint> constraints, StateDB<T> &state) {
        if (state.model_id() != plan_.model_id)
            throw std::invalid_argument("constraint model mismatch");
        for (auto c : constraints) {
            if (c.node >= plan_.fixed.size() || c.component >= 3 || c.value != 0)
                throw std::invalid_argument("only homogeneous translation constraints supported");
            if (state.data().displacement.read()(c.node, c.component) != T(0))
                throw std::invalid_argument("activating zero boundary at displaced node is unsupported");
        }
        plan_.fixed.fill(0);
        for (auto c : constraints) {
            plan_.fixed.write()(c.node, c.component) = 1;
            state.data().velocity.write()(c.node, c.component) = 0;
            state.data().acceleration.write()(c.node, c.component) = 0;
        }
    }
    void forces(StateDB<T> &state) {
        if (state.model_id() != plan_.model_id)
            throw std::invalid_argument("state model mismatch");
        state.validate_shape();
        if (state.data().displacement.size() != plan_.fixed.size())
            throw std::logic_error("node field shape changed");
        auto &s = state.data();
        auto u = s.displacement.read();
        auto local = plan_.local_force.write();
        auto stress = s.stress.write(), strain = s.strain.write();
        auto active = s.element_active.read();
        auto tet = plan_.views();
        parallel_for(tet.nodes.count, policy_, [=](std::size_t e) {
            if (active(e))
                tet_force(e, tet, u, local, stress, strain);
            else {
                for (unsigned c = 0; c < 12; ++c)
                    local(e, c) = 0;
                for (unsigned c = 0; c < 6; ++c) {
                    stress(e, c) = 0;
                    strain(e, c) = 0;
                }
            }
        });
        auto force = s.internal_force.write();
        if (assembly_ == Assembly::Deterministic) {
            parallel_for(force.count, policy_, [&](std::size_t n) {
                for (unsigned c = 0; c < 3; ++c) {
                    T total = 0;
                    for (auto k = plan_.adjacency_offsets[n]; k < plan_.adjacency_offsets[n + 1]; ++k) {
                        auto a = plan_.adjacency[k];
                        total += local(a / 4, (a % 4) * 3 + c);
                    }
                    force(n, c) = total;
                }
            });
        } else {
            s.internal_force.fill(0);
            parallel_for(tet.nodes.count, policy_, [=](std::size_t e) {
                for (unsigned a = 0; a < 4; ++a)
                    for (unsigned c = 0; c < 3; ++c) {
#ifdef _OPENMP
#pragma omp atomic update
#endif
                        force(tet.nodes(e, a), c) += local(e, a * 3 + c);
                    }
            });
        }
        ++force_evaluations_;
    }
    void initialize(StateDB<T> &state, T dt) {
        if (!(dt > 0) || !std::isfinite(dt) || dt > plan_.stable_step)
            throw std::invalid_argument("invalid/unstable time step");
        auto &s = state.data();
        if (s.step || s.time || s.dt)
            throw std::logic_error("state already initialized");
        auto fixed = plan_.fixed.read();
        auto u = s.displacement.write();
        for (std::size_t n = 0; n < u.count; ++n)
            for (unsigned c = 0; c < 3; ++c)
                if (fixed(n, c))
                    u(n, c) = 0;
        forces(state);
        auto v = s.velocity.write(), a = s.acceleration.write();
        auto inv = s.inverse_mass.read();
        auto f = s.internal_force.read(), ext = s.external_force.read(), contact = s.contact_force.read();
        auto active = s.node_active.read();
        parallel_for(v.count, policy_, [=](std::size_t n) {
            for (unsigned c = 0; c < 3; ++c) {
                if (fixed(n, c) || !active(n)) {
                    v(n, c) = a(n, c) = 0;
                    if (fixed(n, c))
                        u(n, c) = 0;
                } else {
                    a(n, c) = (ext(n, c) + contact(n, c) - f(n, c)) * inv(n);
                    v(n, c) -= T(.5) * dt * a(n, c);
                }
            }
        });
        s.dt = dt;
    }
    void step(StateDB<T> &state) {
        auto &s = state.data();
        const T dt = static_cast<T>(s.dt);
        if (!(dt > 0) || !std::isfinite(dt) || dt > plan_.stable_step)
            throw std::logic_error("initialize or restore valid dt before step");
        forces(state);
        auto u = s.displacement.write(), v = s.velocity.write(), a = s.acceleration.write();
        auto inv = s.inverse_mass.read();
        auto f = s.internal_force.read(), ext = s.external_force.read(), contact = s.contact_force.read();
        auto fixed = plan_.fixed.read();
        auto active = s.node_active.read();
        parallel_for(u.count, policy_, [=](std::size_t n) {
            for (unsigned c = 0; c < 3; ++c) {
                if (fixed(n, c) || !active(n)) {
                    v(n, c) = a(n, c) = 0;
                    continue;
                }
                a(n, c) = (ext(n, c) + contact(n, c) - f(n, c)) * inv(n);
                v(n, c) += dt * a(n, c);
                u(n, c) += dt * v(n, c);
            }
        });
        ++s.step;
        s.time += s.dt;
    }
};

// Two positive-volume tetrahedra share face {10,20,30}; only node 40 x is free.
// With nu=0 this is a scalar oscillator: mass=rho/24, stiffness=E/6, omega=2 sqrt(E/rho).
template <class T> MeshDB<T> oscillator_model() {
    ModelBuilder<T> b;
    b.add_material({7, 1, {T(1), T(100), T(0)}, 2});
    b.add_part(9);
    b.add_node(10, {0, 0, 0});
    b.add_node(20, {0, 1, 0});
    b.add_node(30, {0, 0, 1});
    b.add_node(40, {1, 0, 0});
    b.add_node(50, {-1, 0, 0});
    b.add_element(100, Topology::Tet4, {10, 40, 20, 30}, 7, 9);
    b.add_element(200, Topology::Tet4, {10, 20, 50, 30}, 7, 9);
    for (Id id : {10, 20, 30, 50})
        for (unsigned c = 0; c < 3; ++c)
            b.constrain(id, static_cast<std::uint8_t>(c));
    b.constrain(40, 1);
    b.constrain(40, 2);
    b.add_surface(1, {{100, 0, 1}, {200, 0, 1}});
    return b.finalize();
}
} // namespace sdb
