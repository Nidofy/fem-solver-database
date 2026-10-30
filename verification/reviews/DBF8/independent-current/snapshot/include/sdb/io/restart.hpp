#pragma once
#include "model_store.hpp"

namespace sdb::lifecycle {
inline constexpr std::uint64_t restart_magic = 0x33545352424453ull;
struct RestartInfo {
    std::uint32_t state_schema = 3, model_schema = 1, solver_version = 1;
    std::uint8_t precision = 8;
    std::uint64_t mesh = 0, definition = 0, configuration = 0, step = 0, increment = 0, total_increment = 0;
    double time = 0, dt = 0;
    template <class F> void visit(F f) {
        f(state_schema, model_schema, solver_version, precision, mesh, definition, configuration, step,
          increment, total_increment, time, dt);
    }
};
class RestartDB {
  public:
    // 64 KiB scalar blocks, transactional state replacement, no whole-state file image in RAM.
    template <class T>
    static void save(const std::filesystem::path &path, const CompiledModel<T> &model,
                     const StateDB<T> &state, RestartInfo info) {
        if (state.in_trial())
            throw std::logic_error("lifecycle restart requires committed state");
        state.validate_shape();
        if (state.model_id() != model.mesh.fingerprint())
            throw std::invalid_argument("restart model mismatch");
        // Schema 3 remains byte-compatible for translation-only states. Schema 4
        // requires all six rotation fields after the existing fourteen fields.
        const bool extensions = state.registered_fields() && !state.registered_fields()->empty();
        info.state_schema = extensions ? 5 : (state.data().rotation ? 4 : 3);
        info.precision = sizeof(T);
        info.model_schema = model.definition.schema;
        info.mesh = model.mesh.fingerprint();
        info.definition = ModelStore::identity(model);
        info.total_increment = state.data().step;
        info.time = state.data().time;
        info.dt = state.data().dt;
        native::atomic_stream(path, [&](auto &out) {
            native::prefix(out, restart_magic);
            native::record(out, 0, native::pack(info));
            if (extensions) {
                native::Writer binding;
                binding.integer(state.registered_fields()->fingerprint());
                binding.integer<std::uint8_t>(state.data().rotation.has_value());
                native::record(out,199,binding.bytes);
            }
            std::uint32_t id = 1;
            state.data().fields([&](const auto &field) {
                using F = std::decay_t<decltype(field)>;
                using V = typename F::value_type;
                auto view = field.read();
                auto total = product(field.size(), F::components);
                std::size_t offset = 0;
                do {
                    auto count = std::min<std::size_t>(8192, total - offset);
                    native::Writer w;
                    w.integer<std::uint64_t>(field.size());
                    w.integer<std::uint32_t>(F::components);
                    w.integer<std::uint8_t>(sizeof(V));
                    w.integer<std::uint64_t>(offset);
                    w.integer<std::uint64_t>(count);
                    for (std::size_t k = 0; k < count; ++k) {
                        auto index = offset + k;
                        w.scalar(view(index % field.size(), static_cast<unsigned>(index / field.size())));
                    }
                    native::record(out, id, w.bytes);
                    offset += count;
                } while (offset < total);
                ++id;
            });
            if (extensions) checkpoint_detail::write_registered(*state.registered_fields(),
                [&](std::uint32_t kind, const auto &bytes) { native::record(out,kind,bytes); });
            native::record(out, 0xffffffffu, {});
        });
    }
    template <class T>
    static RestartInfo load(const std::filesystem::path &path, const CompiledModel<T> &model,
                            StateDB<T> &state, std::uint64_t configuration,
                            std::uint64_t max_file_bytes = 64ull * 1024 * 1024 * 1024) {
        if (state.in_trial() || state.model_id() != model.mesh.fingerprint())
            throw std::logic_error("restart requires committed matching model");
        if (std::filesystem::file_size(path) > max_file_bytes)
            throw std::length_error("restart file budget");
        std::ifstream in(path, std::ios::binary);
        native::prefix(in, restart_magic);
        auto info = native::unpack<RestartInfo>(native::record(in, 0, 4096));
        const bool extensions = state.registered_fields() && !state.registered_fields()->empty();
        if (info.state_schema != (extensions ? 5u : (state.data().rotation ? 4u : 3u)) || info.model_schema != model.definition.schema || info.solver_version != 1 ||
            info.precision != sizeof(T) || info.mesh != model.mesh.fingerprint() ||
            info.definition != ModelStore::identity(model) || info.configuration != configuration ||
            !std::isfinite(info.time) || !std::isfinite(info.dt) || info.time < 0 || info.dt <= 0)
            throw std::runtime_error("restart schema/model/solver/precision/configuration mismatch");
        if (extensions) {
            auto bytes = native::record(in,199,9); native::Reader binding{bytes};
            if (binding.integer<std::uint64_t>() != state.registered_fields()->fingerprint() ||
                binding.integer<std::uint8_t>() != std::uint8_t(state.data().rotation.has_value()))
                throw std::runtime_error("restart registered state identity/rotation mismatch");
            binding.end();
        }
        StateDB<T> next(model.mesh, state.data().rotation.has_value());
        next.copy_registered_layout(state);
        auto &data = next.data();
        data.step = info.total_increment;
        data.time = info.time;
        data.dt = info.dt;
        std::uint32_t id = 1;
        data.fields([&](auto &field) {
            using F = std::decay_t<decltype(field)>;
            using V = typename F::value_type;
            auto view = field.write();
            auto total = product(field.size(), F::components);
            std::size_t offset = 0;
            do {
                auto bytes = native::record(in, id, 65536 + 64);
                native::Reader r{bytes};
                if (r.integer<std::uint64_t>() != field.size() ||
                    r.integer<std::uint32_t>() != F::components || r.integer<std::uint8_t>() != sizeof(V) ||
                    r.integer<std::uint64_t>() != offset)
                    throw std::runtime_error("restart field shape/offset mismatch");
                auto count = r.integer<std::uint64_t>();
                if (count > 8192 || count > total - offset || (total > offset && count == 0))
                    throw std::runtime_error("restart scalar block length");
                for (std::uint64_t k = 0; k < count; ++k) {
                    auto value = r.scalar<V>();
                    if (!std::isfinite(value))
                        throw std::runtime_error("nonfinite restart state");
                    if constexpr (std::is_same_v<V, std::uint8_t>)
                        if (value > 1)
                            throw std::runtime_error("invalid restart activation");
                    auto index = offset + static_cast<std::size_t>(k);
                    view(index % field.size(), static_cast<unsigned>(index / field.size())) = value;
                }
                r.end();
                offset += static_cast<std::size_t>(count);
            } while (offset < total);
            ++id;
        });
        if (extensions) checkpoint_detail::read_registered(*data.registered,
            [&](std::uint32_t kind, std::size_t limit) { return native::record(in,kind,limit); });
        auto end = native::record(in, 0xffffffffu, 0);
        (void)end;
        native::end(in);
        for (Index n = 0; n < model.mesh.nodes(); ++n) {
            auto mass = data.mass.read()(n), inverse = data.inverse_mass.read()(n);
            if (mass < 0 || inverse < 0 ||
                (mass == 0 ? inverse != 0
                           : std::abs(mass * inverse - 1) > 64 * std::numeric_limits<T>::epsilon()))
                throw std::runtime_error("restart invalid mass/inverse");
            if (data.rotation)
                for (unsigned c = 0; c < 3; ++c) {
                    auto inertia = data.rotation->inertia.read()(n,c);
                    auto inverse_inertia = data.rotation->inverse_inertia.read()(n,c);
                    if (inertia < 0 || inverse_inertia < 0 ||
                        (inertia == 0 ? inverse_inertia != 0 :
                         std::abs(inertia * inverse_inertia - 1) > 64 * std::numeric_limits<T>::epsilon()))
                        throw std::runtime_error("restart invalid inertia/inverse");
                }
        }
        state.data() = std::move(data);
        return info;
    }
};
} // namespace sdb::lifecycle
