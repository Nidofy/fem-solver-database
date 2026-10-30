#pragma once
#include "../io/compression.hpp"

namespace sdb::lifecycle {
// Cold field-name policy. All regions/bases of the same named field use its rule.
// The policy is persisted and must match on append; old formats never carry it.
struct CompressionSettings {
    compression::Options defaults;
    std::map<std::string, compression::Options> fields;
    const compression::Options &for_field(const std::string &name) const {
        const auto it = fields.find(name);
        return it == fields.end() ? defaults : it->second;
    }
    void validate() const {
        compression::validate(defaults);
        if (fields.size() > 64) throw std::length_error("too many field compression policies");
        for (const auto &[name, options] : fields) {
            if (name.empty() || name.size() > 1024) throw std::invalid_argument("invalid compression field name");
            compression::validate(options);
        }
    }
    bool enabled() const { return defaults.codec != compression::Codec::None || !fields.empty(); }
};
namespace compressed_detail {
inline constexpr std::uint64_t magic = 0x34534552424453ull;
inline constexpr std::uint64_t envelope_bytes = 24;
inline constexpr std::uint64_t max_stored = 5ull * 1024 * 1024;
inline constexpr std::uint64_t admission_reserve = compression::hard_workspace_limit + 2 * max_stored + 1024;
inline void pack_options(native::Writer &w, const compression::Options &v) {
    w.integer<std::uint8_t>(static_cast<std::uint8_t>(v.codec));
    w.scalar<std::int32_t>(v.level);
    w.integer(v.min_bytes);
    w.integer<std::uint8_t>(v.only_if_smaller ? 1 : 0);
}
inline compression::Options unpack_options(native::Reader &r) {
    compression::Options v;
    v.codec = static_cast<compression::Codec>(r.integer<std::uint8_t>());
    v.level = r.scalar<std::int32_t>();
    v.min_bytes = r.integer<std::uint64_t>();
    const auto flag = r.integer<std::uint8_t>();
    if (flag > 1) throw std::runtime_error("invalid compression policy flag");
    v.only_if_smaller = flag != 0;
    compression::validate(v);
    return v;
}
inline native::Bytes pack_settings(const CompressionSettings &settings) {
    settings.validate();
    native::Writer w;
    w.integer<std::uint32_t>(1);
    pack_options(w, settings.defaults);
    w.integer<std::uint32_t>(static_cast<std::uint32_t>(settings.fields.size()));
    for (const auto &[name, options] : settings.fields) { native::encode(w, name); pack_options(w, options); }
    return std::move(w.bytes);
}
inline CompressionSettings unpack_settings(std::span<const std::uint8_t> bytes) {
    native::Reader r{bytes};
    if (r.integer<std::uint32_t>() != 1) throw std::runtime_error("unknown compression policy version");
    CompressionSettings settings;
    settings.defaults = unpack_options(r);
    const auto count = r.integer<std::uint32_t>();
    if (count > 64) throw std::length_error("too many field compression policies");
    std::uint64_t budget = 65536;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string name; native::decode(r, name, budget);
        auto options = unpack_options(r);
        if (!settings.fields.emplace(std::move(name), options).second)
            throw std::runtime_error("duplicate compression field policy");
    }
    r.end(); settings.validate(); return settings;
}
inline std::uint64_t identity_hash(std::span<const std::uint8_t> model, std::span<const std::uint8_t> policy) {
    auto hash = native::hash(model);
    for (auto byte : policy) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
inline native::Bytes encode(std::span<const std::uint8_t> raw, const compression::Options &options) {
    auto block = compression::encode(raw, options);
    native::Writer w;
    w.bytes.reserve(static_cast<std::size_t>(envelope_bytes + block.payload.size()));
    w.integer<std::uint8_t>(1);
    w.integer<std::uint8_t>(static_cast<std::uint8_t>(block.codec));
    w.integer<std::uint16_t>(0);
    w.integer(block.raw_bytes); w.integer<std::uint64_t>(block.payload.size()); w.integer(block.raw_crc);
    w.bytes.insert(w.bytes.end(), block.payload.begin(), block.payload.end());
    return std::move(w.bytes);
}
struct Envelope { compression::Codec codec; std::uint64_t raw, stored; std::uint32_t crc; };
inline Envelope inspect(std::span<const std::uint8_t> bytes) {
    native::Reader r{bytes};
    if (r.integer<std::uint8_t>() != 1) throw std::runtime_error("unknown compression envelope version");
    const auto codec = static_cast<compression::Codec>(r.integer<std::uint8_t>());
    if (codec != compression::Codec::None && codec != compression::Codec::Zstd)
        throw std::runtime_error("unknown block codec");
    if (r.integer<std::uint16_t>() != 0) throw std::runtime_error("unknown compression feature flags");
    Envelope h{codec, r.integer<std::uint64_t>(), r.integer<std::uint64_t>(), r.integer<std::uint32_t>()};
    if (h.raw > compression::hard_raw_limit || h.stored > max_stored || h.stored != bytes.size() - r.pos)
        throw std::length_error("compressed block declared size outside limits");
    return h;
}
} // namespace compressed_detail
} // namespace sdb::lifecycle
