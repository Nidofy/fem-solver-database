#pragma once
#include "../checkpoint.hpp"
#include <cstring>

namespace sdb::lifecycle::native {
using Bytes = checkpoint_detail::Bytes;
using Writer = checkpoint_detail::Writer;
using Reader = checkpoint_detail::Reader;
inline constexpr std::uint64_t default_limit = 256ull * 1024 * 1024;
template <class X> void encode(Writer &w, const X &value) {
    if constexpr (std::is_enum_v<X>)
        w.integer<std::uint32_t>(static_cast<std::uint32_t>(value));
    else if constexpr (std::is_same_v<X, bool>)
        w.integer<std::uint8_t>(value ? 1 : 0);
    else if constexpr (std::is_arithmetic_v<X>)
        w.scalar(value);
    else if constexpr (std::is_same_v<X, std::string>) {
        w.integer<std::uint64_t>(value.size());
        w.bytes.insert(w.bytes.end(), value.begin(), value.end());
    } else if constexpr (requires {
                             value.first;
                             value.second;
                         }) {
        encode(w, value.first);
        encode(w, value.second);
    } else if constexpr (requires { const_cast<X &>(value).visit([](auto & ...) {}); })
        const_cast<X &>(value).visit([&](auto &...v) { (encode(w, v), ...); });
    else {
        if constexpr (requires { value.resize(0); })
            w.integer<std::uint64_t>(value.size());
        else if constexpr (requires(X x) { x.resize(0); })
            w.integer<std::uint64_t>(value.size());
        for (auto &v : value)
            encode(w, v);
    }
}
template <class X> void decode(Reader &r, X &value, std::uint64_t &budget) {
    if constexpr (std::is_enum_v<X>)
        value = static_cast<X>(r.integer<std::uint32_t>());
    else if constexpr (std::is_same_v<X, bool>) {
        auto v = r.integer<std::uint8_t>();
        if (v > 1)
            throw std::runtime_error("invalid bool");
        value = v != 0;
    } else if constexpr (std::is_arithmetic_v<X>) {
        value = r.scalar<X>();
        if constexpr (std::is_floating_point_v<X>)
            if (!std::isfinite(value))
                throw std::runtime_error("nonfinite native scalar");
    } else if constexpr (std::is_same_v<X, std::string>) {
        auto n = r.integer<std::uint64_t>();
        if (n > r.bytes.size() - r.pos || n > budget)
            throw std::length_error("native string budget");
        budget -= n;
        value.assign(reinterpret_cast<const char *>(r.bytes.data() + r.pos), static_cast<std::size_t>(n));
        r.pos += static_cast<std::size_t>(n);
    } else if constexpr (requires {
                             value.first;
                             value.second;
                         }) {
        decode(r, value.first, budget);
        decode(r, value.second, budget);
    } else if constexpr (requires { value.visit([](auto & ...) {}); })
        value.visit([&](auto &...v) { (decode(r, v, budget), ...); });
    else {
        if constexpr (requires { value.resize(0); }) {
            auto n = r.integer<std::uint64_t>();
            if (n > r.bytes.size() - r.pos || n > budget / sizeof(typename X::value_type))
                throw std::length_error("native container budget");
            budget -= n * sizeof(typename X::value_type);
            value.resize(static_cast<std::size_t>(n));
        }
        for (auto &v : value)
            decode(r, v, budget);
    }
}
template <class X> std::uint64_t encoded_size(const X &x, std::uint64_t limit) {
    std::uint64_t size = 0;
    auto add = [&](std::uint64_t n) {
        if (n > limit - size)
            throw std::length_error("native payload budget");
        size += n;
    };
    if constexpr (std::is_enum_v<X>)
        add(4);
    else if constexpr (std::is_arithmetic_v<X>)
        add(sizeof(X));
    else if constexpr (std::is_same_v<X, std::string>) {
        add(8);
        add(x.size());
    } else if constexpr (requires {
                             x.first;
                             x.second;
                         }) {
        add(encoded_size(x.first, limit - size));
        add(encoded_size(x.second, limit - size));
    } else if constexpr (requires { const_cast<X &>(x).visit([](auto & ...) {}); })
        const_cast<X &>(x).visit([&](auto &...v) { (add(encoded_size(v, limit - size)), ...); });
    else {
        if constexpr (requires(X v) { v.resize(0); })
            add(8);
        for (auto &v : x)
            add(encoded_size(v, limit - size));
    }
    return size;
}
template <class X> Bytes pack(const X &x, std::uint64_t limit = default_limit) {
    Writer w;
    auto size = encoded_size(x, limit);
    w.bytes.reserve(static_cast<std::size_t>(size));
    encode(w, x);
    return std::move(w.bytes);
}
template <class X> X unpack(std::span<const std::uint8_t> bytes, std::uint64_t limit = default_limit) {
    Reader r{bytes};
    X value;
    decode(r, value, limit);
    r.end();
    return value;
}
inline std::uint64_t hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
inline void write(std::ostream &out, std::span<const std::uint8_t> b) {
    if (!out.write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size())))
        throw std::runtime_error("native I/O write failed");
}
inline void read(std::istream &in, std::span<std::uint8_t> b) {
    if (!in.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(b.size())))
        throw std::runtime_error("native I/O truncated/read failure");
}
inline void prefix(std::ostream &out, std::uint64_t magic) {
    Writer w;
    w.integer(magic);
    w.integer<std::uint32_t>(1);
    write(out, w.bytes);
}
inline void prefix(std::istream &in, std::uint64_t magic) {
    std::array<std::uint8_t, 12> b;
    read(in, b);
    Reader r{b};
    if (r.integer<std::uint64_t>() != magic || r.integer<std::uint32_t>() != 1)
        throw std::runtime_error("native magic/version mismatch");
}
inline void record(std::ostream &out, std::uint32_t kind, std::span<const std::uint8_t> bytes) {
    Writer h;
    h.integer(kind);
    h.integer<std::uint64_t>(bytes.size());
    h.integer(checkpoint_detail::crc(bytes));
    write(out, h.bytes);
    write(out, bytes);
}
struct Record {
    std::uint32_t kind = 0;
    std::uint64_t offset = 0, size = 0;
    std::uint32_t crc = 0;
};
inline Record header(std::istream &in, std::uint64_t limit = default_limit) {
    std::array<std::uint8_t, 16> b;
    read(in, b);
    Reader r{b};
    Record rec;
    rec.kind = r.integer<std::uint32_t>();
    rec.size = r.integer<std::uint64_t>();
    rec.crc = r.integer<std::uint32_t>();
    rec.offset = static_cast<std::uint64_t>(in.tellg());
    if (rec.size > limit)
        throw std::length_error("native record exceeds budget");
    return rec;
}
inline Bytes body(std::istream &in, const Record &rec) {
    Bytes bytes(static_cast<std::size_t>(rec.size));
    read(in, bytes);
    if (checkpoint_detail::crc(bytes) != rec.crc)
        throw std::runtime_error("native CRC mismatch");
    return bytes;
}
inline Bytes record(std::istream &in, std::uint32_t kind, std::uint64_t limit = default_limit) {
    auto rec = header(in, limit);
    if (rec.kind != kind)
        throw std::runtime_error("native record order/type mismatch");
    return body(in, rec);
}
inline void end(std::istream &in) {
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("native trailing data");
    if (in.bad())
        throw std::runtime_error("native read failure");
}
// Cooperative single-writer lease. Atomic directory creation works across processes.
// A crashed writer leaves a stale lease: never remove it automatically or steal it.
// Callers recover explicitly after verifying no owner is alive.
class FileLease {
    std::filesystem::path path_;
  public:
    explicit FileLease(const std::filesystem::path &file) : path_(file.string() + ".writer-lock") {
        if (!std::filesystem::create_directory(path_))
            throw std::runtime_error("result file occupied or stale writer lease: " + file.string());
    }
    FileLease(const FileLease &) = delete;
    FileLease &operator=(const FileLease &) = delete;
    ~FileLease() { std::error_code ec; std::filesystem::remove(path_, ec); }
};
template <class F> void atomic_stream(const std::filesystem::path &path, F fn) {
    checkpoint_detail::atomic_stream_write(path, std::move(fn));
}
} // namespace sdb::lifecycle::native
