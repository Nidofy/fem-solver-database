#include "sdb/io/compression.hpp"
#include <iostream>
#include <random>

namespace c = sdb::compression;
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F &&function, const char *message) {
    bool rejected = false;
    try { function(); } catch (const std::exception &) { rejected = true; }
    require(rejected, message);
}
void run() {
    using Bytes = std::vector<std::uint8_t>;
    Bytes small{1, 2, 3, 4};
    auto none = c::encode(small);
    require(none.codec == c::Codec::None && none.payload == small, "none encoder");
    require(c::decode(none.codec, none.payload, none.raw_bytes, none.raw_crc) == small, "none decoder");
    require(c::crc32(Bytes{'1','2','3','4','5','6','7','8','9'}) == 0xcbf43926u, "standard CRC32");
    require(c::workspace_bound({}, small.size()) == 0 && c::decode_workspace_bound(c::Codec::None) == 0,
            "none workspace");
    rejects([&] { c::decode(c::Codec::None, small, 3, c::crc32(small)); }, "raw size mismatch");
    rejects([&] { c::decode(c::Codec::None, small, 4, 0); }, "raw CRC corruption");
    rejects([&] { c::decode(static_cast<c::Codec>(254), small, 4, 0); }, "unknown codec");
    rejects([&] { c::validate({static_cast<c::Codec>(254)}); }, "unknown option codec");
    rejects([&] { c::decode(c::Codec::None, {}, c::hard_raw_limit + 1, 0); }, "decoded allocation bomb");
    rejects([&] { c::decode(c::Codec::None, small, 4, c::crc32(small), {3, 5, 0}); }, "raw caller limit");
    rejects([&] { c::decode(c::Codec::None, small, 4, c::crc32(small), {5, 3, 0}); }, "stored caller limit");
    rejects([&] { c::encode(small, {}, {3, 5, 0}); }, "encoder raw caller limit");
    Bytes oversized(c::hard_raw_limit + 1);
    rejects([&] { c::encode(oversized); }, "encoder hard limit");
    c::Options zstd{c::Codec::Zstd, 3, 256, true};
    if (!c::available(c::Codec::Zstd)) {
        rejects([&] { c::validate(zstd); }, "unavailable option");
        rejects([&] { c::encode(small, zstd); }, "unavailable codec even below threshold");
        Bytes frame{0x28,0xb5,0x2f,0xfd,0x24,0,1,0,0,0};
        rejects([&] { c::decode(c::Codec::Zstd, frame, 0, 0); }, "unavailable compressed input");
        rejects([&] { c::decode_workspace_bound(c::Codec::Zstd); }, "unavailable decode workspace");
        return;
    }
#if defined(SDB_WITH_ZSTD) && SDB_WITH_ZSTD
    std::cout << "actual zstd runtime " << ZSTD_versionString() << '\n';
    {
        c::detail::Arena arena(sizeof(c::detail::Arena::Header) + 8);
        auto *allocation = c::detail::Arena::allocate(&arena, 8);
        require(allocation && reinterpret_cast<std::uintptr_t>(allocation) % alignof(std::max_align_t) == 0,
                "allocator alignment");
        require(arena.used == sizeof(c::detail::Arena::Header) + 8, "allocator includes headers");
        require(!c::detail::Arena::allocate(&arena, 1), "allocator aggregate cap");
        c::detail::Arena::release(&arena, allocation);
        require(arena.used == 0, "allocator release accounting");
        require(!c::detail::Arena::allocate(&arena, (std::numeric_limits<std::size_t>::max)()),
                "allocator overflow rejection");
    }
    require(c::workspace_bound(zstd, 4096) == c::hard_workspace_limit, "zstd admission workspace");
    require(c::workspace_bound(zstd, 4) == 0, "threshold workspace");
    auto threshold = c::encode(small, zstd);
    require(threshold.codec == c::Codec::None, "threshold bypass");
    rejects([&] { c::validate({c::Codec::Zstd, 0}); }, "low level");
    rejects([&] { c::validate({c::Codec::Zstd, 10}); }, "high level");
    Bytes repeated(1024 * 1024, 42);
    auto encoded = c::encode(repeated, zstd);
    require(encoded.codec == c::Codec::Zstd && encoded.payload.size() < repeated.size(), "real compression");
    require((encoded.payload[4] & 4u) != 0, "frame checksum emitted");
    require(c::decode(encoded.codec, encoded.payload, encoded.raw_bytes, encoded.raw_crc) == repeated, "zstd round trip");
    for (int level = 1; level <= 9; ++level) {
        auto at_level = c::encode(repeated, {c::Codec::Zstd, level});
        require(c::decode(at_level.codec, at_level.payload, at_level.raw_bytes, at_level.raw_crc) == repeated, "level round trip");
    }
    Bytes random(8192);
    std::mt19937 generator(0xDBF5);
    for (auto &value : random) value = static_cast<std::uint8_t>(generator());
    auto incompressible = c::encode(random, zstd);
    require(incompressible.codec == c::Codec::None, "only-if-smaller bypass");
    auto forced = c::encode(random, {c::Codec::Zstd, 3, 0, false});
    require(forced.codec == c::Codec::Zstd, "forced compression");
    require(c::decode(forced.codec, forced.payload, forced.raw_bytes, forced.raw_crc) == random, "incompressible round trip");
    auto empty = c::encode({}, {c::Codec::Zstd, 3, 0, false});
    require(c::decode(empty.codec, empty.payload, empty.raw_bytes, empty.raw_crc).empty(), "empty compressed frame");
    rejects([&] { c::decode(c::Codec::Zstd, encoded.payload, c::hard_raw_limit + 1, 0); }, "compressed bomb");
    rejects([&] { c::decode(c::Codec::Zstd, encoded.payload, repeated.size() - 1, encoded.raw_crc); }, "frame/header decoded size mismatch");
    rejects([&] { c::decode(c::Codec::Zstd, encoded.payload, repeated.size(), 0); }, "compressed raw CRC");
    auto trailing = encoded.payload;
    trailing.push_back(0);
    rejects([&] { c::decode(c::Codec::Zstd, trailing, encoded.raw_bytes, encoded.raw_crc); }, "trailing frame bytes");
    auto concatenated = encoded.payload;
    concatenated.insert(concatenated.end(), encoded.payload.begin(), encoded.payload.end());
    rejects([&] { c::decode(c::Codec::Zstd, concatenated, encoded.raw_bytes, encoded.raw_crc); }, "concatenated frames");
    auto truncated = encoded.payload;
    truncated.pop_back();
    rejects([&] { c::decode(c::Codec::Zstd, truncated, encoded.raw_bytes, encoded.raw_crc); }, "truncated frame");
    auto corrupt = encoded.payload;
    corrupt.back() ^= 1;
    rejects([&] { c::decode(c::Codec::Zstd, corrupt, encoded.raw_bytes, encoded.raw_crc); }, "zstd checksum corruption");
    auto no_checksum = encoded.payload;
    no_checksum[4] &= static_cast<std::uint8_t>(~4u);
    rejects([&] { c::decode(c::Codec::Zstd, no_checksum, encoded.raw_bytes, encoded.raw_crc); }, "missing required checksum");
    {
        c::detail::Arena arena(c::hard_workspace_limit);
        std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx_advanced(arena.memory()), ZSTD_freeCCtx);
        require(bool(context), "unknown-size test context");
        c::detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_contentSizeFlag, 0), arena);
        c::detail::check_zstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 1), arena);
        Bytes unknown_size(ZSTD_compressBound(repeated.size()));
        auto count = ZSTD_compress2(context.get(), unknown_size.data(), unknown_size.size(), repeated.data(), repeated.size());
        c::detail::check_zstd(count, arena);
        unknown_size.resize(count);
        require(ZSTD_getFrameContentSize(unknown_size.data(), unknown_size.size()) == ZSTD_CONTENTSIZE_UNKNOWN,
                "unknown-size fixture");
        rejects([&] { c::decode(c::Codec::Zstd, unknown_size, repeated.size(), c::crc32(repeated)); }, "missing frame content size");
    }
    Bytes malformed{0x28,0xb5,0x2f,0xfd,0xff,0xff,0xff,0xff};
    rejects([&] { c::decode(c::Codec::Zstd, malformed, 20, 0); }, "malformed frame");
    Bytes skip{0x50,0x2a,0x4d,0x18,0,0,0,0};
    rejects([&] { c::decode(c::Codec::Zstd, skip, 0, 0); }, "skippable frame");
    rejects([&] { c::encode(repeated, zstd, {c::hard_raw_limit, 5ull * 1024 * 1024, 0}); }, "encoder workspace limit");
    rejects([&] { c::decode(c::Codec::Zstd, encoded.payload, encoded.raw_bytes, encoded.raw_crc,
                         {c::hard_raw_limit, 5ull * 1024 * 1024, 0}); }, "decoder workspace limit");
    rejects([&] { c::encode(repeated, zstd, {c::hard_raw_limit, repeated.size(), c::hard_workspace_limit}); }, "compressBound charged before output allocation");
    require(c::decode(c::Codec::Zstd, encoded.payload, encoded.raw_bytes, encoded.raw_crc,
                     {c::hard_raw_limit, 5ull * 1024 * 1024, 1024 * 1024}) == repeated, "lower actual decoder workspace permitted");
    Bytes largest(c::hard_raw_limit, 7);
    auto max_block = c::encode(largest, {c::Codec::Zstd, 9});
    require(c::decode(max_block.codec, max_block.payload, max_block.raw_bytes, max_block.raw_crc) == largest,
            "largest block highest supported level");
#endif
}
} // namespace
int main() {
    try { run(); std::cout << "DBF5 codec contracts passed\n"; return 0; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
