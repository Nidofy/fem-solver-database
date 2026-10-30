#pragma once
#include "database.hpp"
#include "../io/native.hpp"

namespace sdb::general_contact {
// Contact extension restart. mechanical_state must identify the EXACT paired mechanical artifact.
// This is not a substitute for an atomic whole-analysis checkpoint manifest.
template <class T> struct Restart {
    std::uint32_t scalar_bytes = sizeof(T), law_schema = 1;
    Binding binding;
    T friction_dissipation = 0;
    std::vector<Record<T>> records;
    template <class F> void visit(F f) {
        f(scalar_bytes, law_schema, binding, friction_dissipation, records);
    }
};
template <class T> std::uint64_t property_fingerprint(std::vector<Property<T>> properties) {
    std::sort(properties.begin(), properties.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    for (std::size_t i = 0; i < properties.size(); ++i) {
        properties[i].validate();
        if (i && properties[i - 1].id == properties[i].id)
            throw std::invalid_argument("duplicate property identity");
    }
    return lifecycle::native::hash(lifecycle::native::pack(properties));
}
inline constexpr std::uint64_t restart_magic = 0x3154434342445355ull;
template <class T>
void save(const std::filesystem::path &path, const Database<T> &db,
          std::uint64_t budget = lifecycle::native::default_limit) {
    if (db.in_trial())
        throw std::logic_error("cannot persist an uncommitted contact trial");
    Restart<T> restart;
    restart.binding = db.binding();
    restart.friction_dissipation = db.friction_dissipation();
    restart.records = db.committed().records();
    auto bytes = lifecycle::native::pack(restart, budget);
    lifecycle::native::atomic_stream(path, [&](std::ostream &out) {
        lifecycle::native::prefix(out, restart_magic);
        lifecycle::native::record(out, 1, bytes);
    });
}
template <class T>
void load(const std::filesystem::path &path, Database<T> &db,
          std::uint64_t budget = lifecycle::native::default_limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("contact restart open failed");
    lifecycle::native::prefix(in, restart_magic);
    auto bytes = lifecycle::native::record(in, 1, budget);
    lifecycle::native::end(in);
    auto restart = lifecycle::native::unpack<Restart<T>>(bytes, budget);
    if (restart.scalar_bytes != sizeof(T) || restart.law_schema != 1)
        throw std::runtime_error("contact restart scalar/law schema mismatch");
    restart.binding.validate();
    db.restore(restart.binding, std::move(restart.records),
               restart.friction_dissipation); // validate all before publishing
}
} // namespace sdb::general_contact
