#include "sdb/cuda_backend.hpp"
#include "sdb/cuda_memory.hpp"

namespace sdb {
template <class T>
__global__ void element_kernel(TetViews<T> tet, View<const T, 3> u, View<const std::uint8_t> active,
                               View<T, 12> local, View<T, 6> stress, View<T, 6> strain) {
    auto e = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (e >= tet.nodes.count)
        return;
    if (active(e))
        tet_force(e, tet, u, local, stress, strain);
    else {
        for (int c = 0; c < 12; ++c)
            local(e, c) = 0;
        for (int c = 0; c < 6; ++c) {
            stress(e, c) = 0;
            strain(e, c) = 0;
        }
    }
}
template <class T>
__global__ void gather_kernel(View<T, 3> force, View<const T, 12> local, const std::size_t *offsets,
                              const std::uint64_t *adjacency) {
    auto n = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (n >= force.count)
        return;
    for (unsigned c = 0; c < 3; ++c) {
        T f = 0;
        for (auto j = offsets[n]; j < offsets[n + 1]; ++j) {
            auto a = adjacency[j];
            f += local(a / 4, (a % 4) * 3 + c);
        }
        force(n, c) = f;
    }
}
template <class T>
__global__ void update_kernel(View<T, 3> u, View<T, 3> v, View<T, 3> a, View<const T> inverse,
                              View<const T, 3> internal, View<const T, 3> external, View<const T, 3> contact,
                              View<const std::uint8_t, 3> fixed, View<const std::uint8_t> active, T dt,
                              bool initialize) {
    auto n = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (n >= u.count)
        return;
    for (unsigned c = 0; c < 3; ++c) {
        if (fixed(n, c) || !active(n)) {
            v(n, c) = a(n, c) = 0;
            if (fixed(n, c))
                u(n, c) = 0;
            continue;
        }
        a(n, c) = (external(n, c) + contact(n, c) - internal(n, c)) * inverse(n);
        if (initialize)
            v(n, c) -= T(.5) * dt * a(n, c);
        else {
            v(n, c) += dt * a(n, c);
            u(n, c) += dt * v(n, c);
        }
    }
}
template <class T> struct CudaExplicit<T>::Impl {
    std::shared_ptr<cuda::Context> ctx;
    TetPlan<T> plan;
    StateData<T> snapshot;
    cuda::Mirror<Index, 4> nodes;
    cuda::Mirror<T, 12> gradients, local;
    cuda::Mirror<T> volume, lambda, mu, inverse;
    cuda::Mirror<std::uint8_t, 3> fixed;
    cuda::Mirror<std::uint8_t> node_active, element_active;
    cuda::Mirror<T, 3> u, v, a, internal, external, contact;
    cuda::Mirror<T, 6> stress, strain;
    cuda::DeviceBuffer<std::size_t> offsets;
    cuda::DeviceBuffer<std::uint64_t> adjacency;
    cuda::Event start, end;
    bool timing = false;
    explicit Impl(const MeshDB<T> &mesh, StateDB<T> &state, int device)
        : ctx(std::make_shared<cuda::Context>(device)), plan(mesh, state), snapshot(state.data()),
          nodes(ctx, mesh.elements()), gradients(ctx, mesh.elements()), local(ctx, mesh.elements()),
          volume(ctx, mesh.elements()), lambda(ctx, mesh.elements()), mu(ctx, mesh.elements()),
          inverse(ctx, mesh.nodes()), fixed(ctx, mesh.nodes()), node_active(ctx, mesh.nodes()),
          element_active(ctx, mesh.elements()), u(ctx, mesh.nodes()), v(ctx, mesh.nodes()),
          a(ctx, mesh.nodes()), internal(ctx, mesh.nodes()), external(ctx, mesh.nodes()),
          contact(ctx, mesh.nodes()), stress(ctx, mesh.elements()), strain(ctx, mesh.elements()),
          offsets(ctx, plan.adjacency_offsets.size()), adjacency(ctx, plan.adjacency.size()),
          start(ctx, true), end(ctx, true) {
        auto upload = [](auto &mirror, const auto &field) {
            mirror.assign(field.read());
            mirror.upload();
        };
        // Constrained initial displacement must be zero before the first force evaluation.
        auto ux = snapshot.displacement.write();
        auto fc = plan.fixed.read();
        for (std::size_t n = 0; n < mesh.nodes(); ++n)
            for (int c = 0; c < 3; ++c)
                if (fc(n, c))
                    ux(n, c) = 0;
        upload(nodes, plan.nodes);
        upload(gradients, plan.gradients);
        upload(local, plan.local_force);
        upload(volume, plan.volume);
        upload(lambda, plan.lambda);
        upload(mu, plan.mu);
        upload(inverse, snapshot.inverse_mass);
        upload(fixed, plan.fixed);
        upload(node_active, snapshot.node_active);
        upload(element_active, snapshot.element_active);
        upload(u, snapshot.displacement);
        upload(v, snapshot.velocity);
        upload(a, snapshot.acceleration);
        upload(internal, snapshot.internal_force);
        upload(external, snapshot.external_force);
        upload(contact, snapshot.contact_force);
        upload(stress, snapshot.stress);
        upload(strain, snapshot.strain);
        // Immutable plan arrays remain owned until stream completion.
        cuda::check(cudaMemcpyAsync(offsets.data(), plan.adjacency_offsets.data(),
                                    offsets.size() * sizeof(std::size_t), cudaMemcpyHostToDevice,
                                    ctx->stream()),
                    "adjacency offsets upload");
        if (adjacency.size())
            cuda::check(cudaMemcpyAsync(adjacency.data(), plan.adjacency.data(),
                                        adjacency.size() * sizeof(std::uint64_t), cudaMemcpyHostToDevice,
                                        ctx->stream()),
                        "adjacency upload");
        nodes.finish();
        gradients.finish();
        local.finish();
        volume.finish();
        lambda.finish();
        mu.finish();
        inverse.finish();
        fixed.finish();
        node_active.finish();
        element_active.finish();
        u.finish();
        v.finish();
        a.finish();
        internal.finish();
        external.finish();
        contact.finish();
        stress.finish();
        strain.finish();
        ctx->wait();
    }
    ~Impl() {
        try {
            ctx->wait();
        } catch (...) {
            ++cuda::cleanup_errors;
        }
    }
    void execute(bool initialize) {
        ctx->activate();
        if (!timing) {
            start.record();
            timing = true;
        }
        auto stream = ctx->stream();
        const auto e = plan.nodes.size(), n = plan.fixed.size();
        TetViews<T> tet{nodes.device_read(), gradients.device_read(), volume.device_read(),
                        lambda.device_read(), mu.device_read()};
        if (e) {
            element_kernel<<<static_cast<unsigned>((e + 255) / 256), 256, 0, stream>>>(
                tet, u.device_read(), element_active.device_read(), local.device_write(),
                stress.device_write(), strain.device_write());
            cuda::check(cudaGetLastError(), "TET kernel launch");
        }
        gather_kernel<<<static_cast<unsigned>((n + 255) / 256), 256, 0, stream>>>(
            internal.device_write(), local.device_read(), offsets.data(), adjacency.data());
        cuda::check(cudaGetLastError(), "gather kernel launch");
        update_kernel<<<static_cast<unsigned>((n + 255) / 256), 256, 0, stream>>>(
            u.device_write(), v.device_write(), a.device_write(), inverse.device_read(),
            internal.device_read(), external.device_read(), contact.device_read(), fixed.device_read(),
            node_active.device_read(), static_cast<T>(snapshot.dt), initialize);
        cuda::check(cudaGetLastError(), "update kernel launch");
        end.record();
    }
};
template <class T>
CudaExplicit<T>::CudaExplicit(const MeshDB<T> &mesh, StateDB<T> &initial, int device)
    : impl_(std::make_unique<Impl>(mesh, initial, device)) {}
template <class T> CudaExplicit<T>::~CudaExplicit() = default;
template <class T> void CudaExplicit<T>::initialize(T dt) {
    auto &p = *impl_;
    if (!(dt > 0) || !std::isfinite(dt) || dt > p.plan.stable_step)
        throw std::invalid_argument("invalid GPU dt");
    if (p.snapshot.dt || p.snapshot.time || p.snapshot.step)
        throw std::logic_error("GPU state already initialized");
    p.snapshot.dt = dt;
    p.execute(true);
}
template <class T> void CudaExplicit<T>::step() {
    auto &p = *impl_;
    if (!(p.snapshot.dt > 0) || !std::isfinite(p.snapshot.dt) || p.snapshot.dt > p.plan.stable_step)
        throw std::logic_error("GPU state not initialized");
    p.execute(false);
    ++p.snapshot.step;
    p.snapshot.time += p.snapshot.dt;
}
template <class T> void CudaExplicit<T>::wait() {
    impl_->ctx->wait();
}
template <class T> void CudaExplicit<T>::download(StateDB<T> &target) {
    auto &p = *impl_;
    if (target.model_id() != p.plan.model_id || target.in_trial())
        throw std::invalid_argument("GPU target model/trial mismatch");
    p.u.download();
    p.v.download();
    p.a.download();
    p.internal.download();
    p.stress.download();
    p.strain.download();
    p.u.finish();
    p.v.finish();
    p.a.finish();
    p.internal.finish();
    p.stress.finish();
    p.strain.finish();
    p.u.copy_to(p.snapshot.displacement);
    p.v.copy_to(p.snapshot.velocity);
    p.a.copy_to(p.snapshot.acceleration);
    p.internal.copy_to(p.snapshot.internal_force);
    p.stress.copy_to(p.snapshot.stress);
    p.strain.copy_to(p.snapshot.strain);
    auto replacement = p.snapshot;
    static_assert(std::is_nothrow_move_assignable_v<StateData<T>>);
    target.data() = std::move(replacement);
}
template <class T> float CudaExplicit<T>::elapsed_kernel_ms() {
    auto &p = *impl_;
    if (!p.timing)
        return 0;
    p.end.wait();
    return p.end.elapsed_since(p.start);
}
template <class T> std::size_t CudaExplicit<T>::device_peak_bytes() const {
    return impl_->ctx->memory.peak.load();
}
template class CudaExplicit<float>;
template class CudaExplicit<double>;
} // namespace sdb
