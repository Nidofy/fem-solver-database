#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
// The advanced allocator API is exported by the supported shared libraries.
// Consumers must use an SDK/library pair that provides these symbols.
#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h>
#endif

namespace sdb::compression {
enum class Codec : std::uint8_t { None = 0, Zstd = 1 };
struct Options {
    Codec codec = Codec::None;
    int level = 3;
    std::uint64_t min_bytes = 256;
    bool only_if_smaller = true;
};
inline constexpr std::uint64_t hard_raw_limit = 4ull * 1024 * 1024;
inline constexpr std::uint64_t hard_workspace_limit = 32ull * 1024 * 1024;
struct Limits {
    std::uint64_t max_raw_bytes = hard_raw_limit;
    std::uint64_t max_stored_bytes = 5ull * 1024 * 1024;
    std::uint64_t max_workspace_bytes = hard_workspace_limit;
};
struct Encoded {
    Codec codec = Codec::None;
    std::uint64_t raw_bytes = 0;
    std::uint32_t raw_crc = 0;
    std::vector<std::uint8_t> payload;
};
inline bool available(Codec codec) noexcept {
    if (codec == Codec::None) return true;
#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
    return codec == Codec::Zstd;
#else
    return false;
#endif
}
inline void validate(const Options &options) {
    if (options.codec != Codec::None && options.codec != Codec::Zstd)
        throw std::invalid_argument("unknown compression codec");
    if (!available(options.codec)) throw std::runtime_error("zstd codec is unavailable in this build");
    if (options.codec == Codec::Zstd && (options.level < 1 || options.level > 9))
        throw std::invalid_argument("zstd compression level must be in [1,9]");
}
inline std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xffffffffu;
    for (auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
// Admission estimates are conservative and allocate nothing. Both input/output
// buffers must be charged separately by the caller; these cover codec memory.
inline std::uint64_t workspace_bound(const Options &options, std::uint64_t raw_bytes) {
    validate(options);
    if (raw_bytes > hard_raw_limit) throw std::length_error("compression raw block hard limit");
    return options.codec == Codec::Zstd && raw_bytes >= options.min_bytes ? hard_workspace_limit : 0;
}
inline std::uint64_t decode_workspace_bound(Codec codec) {
    if (codec == Codec::None) return 0;
    if (codec != Codec::Zstd) throw std::invalid_argument("unknown compression codec");
    if (!available(codec)) throw std::runtime_error("zstd codec is unavailable in this build");
    return hard_workspace_limit;
}
namespace detail {
inline void check_sizes(std::uint64_t raw, std::uint64_t stored, const Limits &limits) {
    if (raw > hard_raw_limit || raw > limits.max_raw_bytes || stored > limits.max_stored_bytes ||
        raw > (std::numeric_limits<std::size_t>::max)() || stored > (std::numeric_limits<std::size_t>::max)())
        throw std::length_error("compression block size limit");
}
#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
struct Arena {
    struct alignas(std::max_align_t) Header { std::size_t bytes; };
    std::size_t used = 0;
    std::size_t limit;
    bool exhausted = false;
    explicit Arena(std::uint64_t cap)
        : limit(static_cast<std::size_t>((cap < hard_workspace_limit) ? cap : hard_workspace_limit)) {}
    static void *allocate(void *opaque, std::size_t bytes) noexcept {
        auto &arena = *static_cast<Arena *>(opaque);
        if (bytes > arena.limit - arena.used || sizeof(Header) > arena.limit - arena.used - bytes) {
            arena.exhausted = true;
            return nullptr;
        }
        auto *header = static_cast<Header *>(std::malloc(sizeof(Header) + bytes));
        if (!header) { arena.exhausted = true; return nullptr; }
        header->bytes = sizeof(Header) + bytes;
        arena.used += header->bytes;
        return header + 1;
    }
    static void release(void *opaque, void *address) noexcept {
        if (!address) return;
        auto *header = static_cast<Header *>(address) - 1;
        static_cast<Arena *>(opaque)->used -= header->bytes;
        std::free(header);
    }
    ZSTD_customMem memory() noexcept { return {allocate, release, this}; }
};
inline void check_zstd(std::size_t result, const Arena &arena) {
    if (arena.exhausted) throw std::length_error("zstd workspace budget exhausted");
    if (ZSTD_isError(result)) throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(result));
}
#endif
} // namespace detail
inline Encoded encode(std::span<const std::uint8_t> raw, const Options &options = {}, const Limits &limits = {}) {
    validate(options);
    detail::check_sizes(raw.size(), raw.size(), limits);
    Encoded result;
    result.raw_bytes = raw.size();
    result.raw_crc = crc32(raw);
    if (options.codec == Codec::None || raw.size() < options.min_bytes) {
        result.payload.assign(raw.begin(), raw.end());
        return result;
    }
#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
    const auto bound = ZSTD_compressBound(raw.size());
    if (ZSTD_isError(bound) || bound > limits.max_stored_bytes)
        throw std::length_error("zstd stored buffer budget");
    detail::Arena arena(limits.max_workspace_bytes);
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx_advanced(arena.memory()), ZSTD_freeCCtx);
    if (!context) throw std::length_error("zstd context workspace budget");
    detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_compressionLevel, options.level), arena);
    detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 1), arena);
    detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_contentSizeFlag, 1), arena);
    // Synchronous, single-threaded zstd: no worker allocations escape the arena.
    detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_nbWorkers, 0), arena);
    result.payload.resize(bound);
    auto count = ZSTD_compress2(context.get(), result.payload.data(), result.payload.size(), raw.data(), raw.size());
    detail::check_zstd(count, arena);
    if (options.only_if_smaller && count >= raw.size()) {
        result.payload.assign(raw.begin(), raw.end());
    } else {
        result.payload.resize(count);
        result.codec = Codec::Zstd;
    }
    return result;
#else
    throw std::runtime_error("zstd codec is unavailable in this build");
#endif
}
inline std::vector<std::uint8_t> decode(Codec codec, std::span<const std::uint8_t> stored,
                                       std::uint64_t raw_bytes, std::uint32_t raw_crc,
                                       const Limits &limits = {}) {
    detail::check_sizes(raw_bytes, stored.size(), limits);
    if (codec != Codec::None && codec != Codec::Zstd) throw std::invalid_argument("unknown compression codec");
    if (!available(codec)) throw std::runtime_error("zstd codec is unavailable in this build");
    std::vector<std::uint8_t> raw;
    if (codec == Codec::None) {
        if (stored.size() != raw_bytes) throw std::runtime_error("raw compression block size mismatch");
        raw.assign(stored.begin(), stored.end());
    } else {
#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
        // Reject skippable frames, omitted content sizes, concatenated frames,
        // and trailing bytes before allocating the declared decoded payload.
        if (stored.size() < 6 || stored[0] != 0x28 || stored[1] != 0xb5 || stored[2] != 0x2f || stored[3] != 0xfd)
            throw std::runtime_error("invalid zstd frame magic");
        if ((stored[4] & 4u) == 0) throw std::runtime_error("zstd frame checksum is required");
        auto frame_bytes = ZSTD_findFrameCompressedSize(stored.data(), stored.size());
        if (ZSTD_isError(frame_bytes) || frame_bytes != stored.size())
            throw std::runtime_error("zstd frame must consume exactly one block");
        auto content_bytes = ZSTD_getFrameContentSize(stored.data(), stored.size());
        if (content_bytes == ZSTD_CONTENTSIZE_ERROR || content_bytes == ZSTD_CONTENTSIZE_UNKNOWN || content_bytes != raw_bytes)
            throw std::runtime_error("zstd declared content size mismatch");
        detail::Arena arena(limits.max_workspace_bytes);
        std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx_advanced(arena.memory()), ZSTD_freeDCtx);
        if (!context) throw std::length_error("zstd context workspace budget");
        raw.resize(static_cast<std::size_t>(raw_bytes));
        auto decoded = ZSTD_decompressDCtx(context.get(), raw.data(), raw.size(), stored.data(), stored.size());
        detail::check_zstd(decoded, arena);
        if (decoded != raw_bytes) throw std::runtime_error("zstd decoded size mismatch");
#endif
    }
    if (crc32(raw) != raw_crc) throw std::runtime_error("compression block raw CRC mismatch");
    return raw;
}
} // namespace sdb::compression
