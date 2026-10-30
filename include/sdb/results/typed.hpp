#pragma once
#include "../schema.hpp"
#include <compare>
#include <variant>
#include <optional>

namespace sdb::lifecycle {
enum class ResultFormat { Auto, Native1, Indexed2, TypedIndexed3, CompressedIndexed4 };
struct ResultEndpoint {
    // kind: 0=node, 1=element-node, 2=element-face. Entity and local are
    // one-based (node local is zero); face side is +/-1, other sides zero.
    // FaceId's zero-based local index is converted by the publishing adapter.
    std::uint32_t kind = 0;
    std::uint64_t entity = 0;
    std::uint32_t local = 0;
    std::int32_t side = 0;
    auto operator<=>(const ResultEndpoint &) const = default;
    template<class F> void visit(F f) { f(kind, entity, local, side); }
};
struct ResultAddress {
    std::uint64_t entity = 0;
    std::uint32_t integration_point = 0, layer = 0, section_point = 0, element_node = 0, face = 0;
    std::int32_t side = 0;
    std::uint64_t contact_generation = 0, interaction = 0;
    ResultEndpoint endpoint_a, endpoint_b;
    std::uint32_t contact_point = 0, domain = 0;
    auto operator<=>(const ResultAddress &) const = default;
    template<class F> void visit(F f) {
        f(entity, integration_point, layer, section_point, element_node, face, side,
          contact_generation, interaction, endpoint_a, endpoint_b, contact_point, domain);
    }
};
using TypedValues = std::variant<std::vector<float>, std::vector<double>, std::vector<std::int32_t>,
    std::vector<std::uint32_t>, std::vector<std::int64_t>, std::vector<std::uint64_t>,
    std::vector<std::uint8_t>>;
inline TypedValues empty_typed_values(ScalarType t) {
    switch(t) {
    case ScalarType::Float32: return std::vector<float>{};
    case ScalarType::Float64: return std::vector<double>{};
    case ScalarType::Int32: return std::vector<std::int32_t>{};
    case ScalarType::UInt32: return std::vector<std::uint32_t>{};
    case ScalarType::Int64: return std::vector<std::int64_t>{};
    case ScalarType::UInt64: return std::vector<std::uint64_t>{};
    case ScalarType::UInt8: return std::vector<std::uint8_t>{};
    }
    throw std::invalid_argument("unknown typed result scalar");
}
struct TypedFieldPayload {
    FieldDescriptor schema;
    std::vector<ResultAddress> addresses;
    TypedValues values = std::vector<double>{};
    std::vector<std::uint8_t> validity;
    std::string schema_id;
    std::uint32_t schema_version = 1;
    std::uint64_t basis_id = 0;
    template<class F> void visit(F f) const {
        if (values.index() != static_cast<std::size_t>(schema.scalar))
            throw std::invalid_argument("typed result scalar/variant mismatch");
        f(schema, addresses, validity, schema_id, schema_version, basis_id);
        std::visit([&](const auto &v) { f(v); }, values);
    }
    template<class F> void visit(F f) {
        f(schema, addresses, validity, schema_id, schema_version, basis_id);
        if (values.index() != static_cast<std::size_t>(schema.scalar))
            values = empty_typed_values(schema.scalar);
        std::visit([&](auto &v) { f(v); }, values);
    }
};
struct ResultBinding {
    std::uint64_t mesh = 0, definition = 0, configuration = 0, step = 0, increment = 0;
    double time = 0;
    bool operator==(const ResultBinding &) const = default;
    template<class F> void visit(F f) { f(mesh, definition, configuration, step, increment, time); }
};
struct ResultBasis {
    std::uint64_t id = 0;
    std::array<double,9> rotation{1,0,0,0,1,0,0,0,1};
    template<class F> void visit(F f) { f(id,rotation); }
};
struct ResultSample {
    std::uint32_t integration_point = 0, layer = 0, section_point = 0;
    Index material = 0;
    auto operator<=>(const ResultSample &) const = default;
    template<class F> void visit(F f) { f(integration_point, layer, section_point, material); }
};
inline void validate_typed_payload(const TypedFieldPayload &p) {
    p.schema.validate();
    if (!p.schema_version || p.values.index() != static_cast<std::size_t>(p.schema.scalar))
        throw std::invalid_argument("typed result scalar/schema version mismatch");
    auto count = product(p.addresses.size(), p.schema.component_count());
    if ((!p.validity.empty() && p.validity.size() != count) ||
        (p.schema.missing == MissingPolicy::Forbidden && !p.validity.empty()))
        throw std::invalid_argument("typed result validity shape/policy");
    std::visit([&](const auto &v) {
        if (v.size() != count) throw std::invalid_argument("typed result value shape");
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (!p.validity.empty() && (p.validity[i] > 1 || (!p.validity[i] && v[i] != 0)))
                throw std::invalid_argument("typed result invalid slot must be canonical zero");
            if constexpr (std::is_floating_point_v<typename std::decay_t<decltype(v)>::value_type>)
                if (!std::isfinite(v[i]) || (!p.validity.empty() && !p.validity[i] && std::signbit(v[i])))
                    throw std::invalid_argument("typed result nonfinite/noncanonical scalar");
        }
    }, p.values);
    std::set<ResultAddress> seen;
    for (auto &a : p.addresses)
        if (!seen.insert(a).second) throw std::invalid_argument("duplicate full result address");
}
} // namespace sdb::lifecycle
