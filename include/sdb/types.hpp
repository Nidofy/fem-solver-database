#pragma once
#include "memory.hpp"
#include "index.hpp"
#include <compare>
#include <bit>
#include <string>

namespace sdb {
template <class Tag, class U> class StrongId {
    static_assert(std::is_unsigned_v<U>);
    U value_ = std::numeric_limits<U>::max();
  public:
    constexpr StrongId() = default;
    explicit constexpr StrongId(U value) : value_(value) {}
    static StrongId checked(std::uint64_t value) {
        if (value >= std::numeric_limits<U>::max()) throw std::length_error("strong ID exhausted");
        return StrongId(static_cast<U>(value));
    }
    constexpr U value() const noexcept { return value_; }
    constexpr bool valid() const noexcept { return value_ != std::numeric_limits<U>::max(); }
    auto operator<=>(const StrongId &) const = default;
    template <class F> void visit(F f) { f(value_); }
};
#define SDB_ID(name, width) struct name##Tag {}; using name = StrongId<name##Tag, width>
SDB_ID(ExternalLabel, std::uint64_t);
SDB_ID(NodeId, Index);
SDB_ID(ElementId, Index);
SDB_ID(InstanceId, Index);
SDB_ID(MaterialId, Index);
SDB_ID(SectionId, Index);
SDB_ID(SurfaceId, Index);
SDB_ID(FieldId, Index);
SDB_ID(StepId, Index);
SDB_ID(FrameId, std::uint64_t);
SDB_ID(CoordinateId, Index);
SDB_ID(OrientationId, Index);
SDB_ID(ConstraintId, Index);
SDB_ID(LoadId, Index);
#undef SDB_ID
static_assert(sizeof(NodeId) == sizeof(Index) && sizeof(ExternalLabel) == 8);
static_assert(std::is_trivially_copyable_v<NodeId> && !std::is_convertible_v<NodeId, ElementId>);
enum class Dof : std::uint8_t { X, Y, Z, RX, RY, RZ };

// Canonical cold-metadata identity; not authentication. Length-delimit containers
// and strings, encode arithmetic in LE. No object representation/padding hashes.
template <class X> std::uint64_t semantic_hash(const X &object) {
    std::uint64_t h = 14695981039346656037ull;
    auto integer = [&](std::uint64_t v) {
        for (unsigned k = 0; k < 8; ++k) { h ^= (v >> (8 * k)) & 255; h *= 1099511628211ull; }
    };
    auto visit = [&](auto &&self, const auto &x) -> void {
        using V = std::remove_cvref_t<decltype(x)>;
        if constexpr (std::is_enum_v<V> || std::is_integral_v<V>) integer(static_cast<std::uint64_t>(x));
        else if constexpr (std::is_floating_point_v<V>) integer(std::bit_cast<std::uint64_t>(double(x)));
        else if constexpr (std::is_same_v<V, std::string>) {
            integer(x.size()); for (unsigned char c : x) integer(c);
        } else if constexpr (requires { x.first; x.second; }) {
            self(self, x.first); self(self, x.second);
        } else if constexpr (requires { x.visit([](const auto &...) {}); })
            x.visit([&](const auto &...v) { (self(self, v), ...); });
        else if constexpr (requires(V &v) { v.visit([](auto &...) {}); })
            const_cast<V &>(x).visit([&](const auto &...v) { (self(self, v), ...); });
        else { integer(x.size()); for (const auto &v : x) self(self, v); }
    };
    visit(visit, object);
    return h;
}
} // namespace sdb
