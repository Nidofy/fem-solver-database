#pragma once
#include "bridge.hpp"
#include "io.hpp"

namespace sdb::general_contact {
template <class T> struct BundleHeader {
    std::uint32_t schema = 1, precision = sizeof(T);
    Binding binding;
    double dt = 0;
    bool rotations = false;
    std::uint64_t contacts = 0;
    T friction_dissipation = 0;
    template <class F> void visit(F f) {
        f(schema, precision, binding, dt, rotations, contacts, friction_dissipation);
    }
};
inline constexpr std::uint64_t bundle_magic = 0x3242434342445355ull;
template <class T> void validate_mass(const StateDB<T> &state) {
    const auto &d = state.data();
    auto pair = [](T mass, T inverse) {
        finite(mass);
        finite(inverse);
        if (mass < 0 || inverse < 0 ||
            (mass == 0 ? inverse != 0
                       : std::abs(mass * inverse - 1) > T(128) * std::numeric_limits<T>::epsilon()))
            throw std::invalid_argument("checkpoint invalid mass/inverse");
    };
    for (std::size_t n = 0; n < d.mass.size(); ++n) {
        pair(d.mass.read()(n), d.inverse_mass.read()(n));
        if (d.rotation)
            for (unsigned c = 0; c < 3; ++c)
                pair(d.rotation->inertia.read()(n, c), d.rotation->inverse_inertia.read()(n, c));
    }
}
// One atomic file contains both accepted mechanical state and contact history. No cross-file
// rename window, no whole-file image; fixed-size field and contact record chunks carry CRCs.
template <class T>
void save_bundle(const std::filesystem::path &path, const StateDB<T> &state, const Database<T> &contact) {
    if (state.in_trial() || contact.in_trial())
        throw std::logic_error("bundle requires committed phases");
    if (bind(state, contact.binding().configuration) != contact.binding())
        throw std::invalid_argument("bundle mechanical/contact state mismatch");
    validate_mass(state);
    BundleHeader<T> header;
    header.binding = contact.binding();
    header.dt = state.data().dt;
    header.rotations = state.data().rotation.has_value();
    header.contacts = contact.committed().size();
    header.friction_dissipation = contact.friction_dissipation();
    lifecycle::native::atomic_stream(path, [&](std::ostream &out) {
        namespace native = lifecycle::native;
        native::prefix(out, bundle_magic);
        native::record(out, 0, native::pack(header));
        std::uint32_t field_id = 1;
        state.data().fields([&](const auto &field) {
            using F = std::decay_t<decltype(field)>;
            using V = typename F::value_type;
            const auto total = product(field.size(), F::components);
            auto view = field.read();
            std::size_t offset = 0;
            do {
                const auto count = std::min<std::size_t>(8192, total - offset);
                native::Writer w;
                w.integer<std::uint64_t>(field.size());
                w.integer<std::uint32_t>(F::components);
                w.integer<std::uint8_t>(sizeof(V));
                w.integer<std::uint64_t>(offset);
                w.integer<std::uint64_t>(count);
                for (std::size_t k = 0; k < count; ++k) {
                    auto index = offset + k;
                    const auto value =
                        view(index % field.size(), static_cast<unsigned>(index / field.size()));
                    if constexpr (std::is_same_v<V, std::uint8_t>)
                        if (value > 1)
                            throw std::invalid_argument("invalid activation flag");
                    w.scalar(value);
                }
                native::record(out, field_id, w.bytes);
                offset += count;
            } while (offset < total);
            ++field_id;
        });
        for (std::size_t offset = 0; offset < header.contacts;) {
            std::vector<Record<T>> records;
            const auto end = std::min<std::size_t>(header.contacts, offset + 1024);
            records.reserve(end - offset);
            for (auto i = offset; i < end; ++i)
                records.push_back({contact.committed().keys()[i], contact.committed().at(i)});
            native::record(out, 100, native::pack(records));
            offset = end;
        }
        native::record(out, 0xffffffffu, {});
    });
}
template <class T>
void load_bundle(const std::filesystem::path &path, const MeshDB<T> &mesh, StateDB<T> &state,
                 Database<T> &contact, std::uint64_t max_bytes = 4ull * 1024 * 1024 * 1024,
                 std::uint64_t max_contacts = 10000000) {
    namespace native = lifecycle::native;
    if (state.in_trial() || contact.in_trial() || state.model_id() != mesh.fingerprint() ||
        contact.binding().mesh != mesh.fingerprint())
        throw std::logic_error("bundle load requires matching committed owners");
    const auto file_bytes = std::filesystem::file_size(path);
    if (file_bytes > max_bytes)
        throw std::length_error("bundle file budget");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("bundle open failed");
    native::prefix(in, bundle_magic);
    auto header = native::unpack<BundleHeader<T>>(native::record(in, 0, 4096));
    header.binding.validate();
    if (header.schema != 1 || header.precision != sizeof(T) || header.binding.mesh != mesh.fingerprint() ||
        header.binding.configuration != contact.binding().configuration || header.dt < 0 ||
        (header.binding.step && header.dt == 0) || header.contacts > max_contacts ||
        header.contacts > file_bytes / 32)
        throw std::invalid_argument("bundle schema/model/configuration/size mismatch");
    StateDB<T> next(mesh, header.rotations);
    auto &data = next.data();
    data.step = header.binding.step;
    data.time = header.binding.time;
    data.dt = header.dt;
    std::uint32_t id = 1;
    data.fields([&](auto &field) {
        using F = std::decay_t<decltype(field)>;
        using V = typename F::value_type;
        auto view = field.write();
        const auto total = product(field.size(), F::components);
        std::size_t offset = 0;
        do {
            auto bytes = native::record(in, id, 65536 + 64);
            native::Reader r{bytes};
            if (r.integer<std::uint64_t>() != field.size() || r.integer<std::uint32_t>() != F::components ||
                r.integer<std::uint8_t>() != sizeof(V) || r.integer<std::uint64_t>() != offset)
                throw std::runtime_error("bundle field descriptor mismatch");
            const auto count = r.integer<std::uint64_t>();
            if (count > 8192 || count > total - offset || (total > offset && count == 0))
                throw std::length_error("bundle scalar chunk");
            for (std::size_t k = 0; k < count; ++k) {
                const V value = r.scalar<V>();
                finite(value);
                if constexpr (std::is_same_v<V, std::uint8_t>)
                    if (value > 1)
                        throw std::invalid_argument("bundle activation flag");
                const auto index = offset + k;
                view(index % field.size(), static_cast<unsigned>(index / field.size())) = value;
            }
            r.end();
            offset += static_cast<std::size_t>(count);
        } while (offset < total);
        ++id;
    });
    std::vector<Record<T>> records;
    records.reserve(static_cast<std::size_t>(header.contacts));
    while (records.size() < header.contacts) {
        auto chunk =
            native::unpack<std::vector<Record<T>>>(native::record(in, 100, 1024 * 1024), 1024 * 1024);
        if (chunk.empty() || chunk.size() > 1024 || chunk.size() > header.contacts - records.size())
            throw std::length_error("bundle contact chunk");
        for (const auto &r : chunk) {
            if (!records.empty() && !(records.back().key < r.key))
                throw std::invalid_argument("bundle contact order");
            records.push_back(r);
        }
    }
    (void)native::record(in, 0xffffffffu, 0);
    native::end(in);
    validate_mass(next);
    if (bind(next, header.binding.configuration) != header.binding)
        throw std::runtime_error("bundle full mechanical digest mismatch");
    Database<T> next_contact(header.binding);
    next_contact.restore(header.binding, std::move(records), header.friction_dissipation);
    static_assert(std::is_nothrow_move_assignable_v<StateData<T>>);
    contact.adopt(std::move(next_contact)); // all failure points precede publication
    state.data() = std::move(data);
}
} // namespace sdb::general_contact
