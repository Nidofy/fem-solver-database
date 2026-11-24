#pragma once
#include "memory.hpp"
#include <cmath>
#include <set>
#include <string>

namespace sdb {
// Cold semantic metadata. Wire enums are explicitly encoded by native::encode,
// never by dumping these C++ objects. Unknown values fail validation.
enum class ScalarType : std::uint8_t { Float32, Float64, Int32, UInt32, Int64, UInt64, UInt8 };
enum class Association : std::uint8_t { Node, Element, Face, Contact, Global };
enum class FieldLocation : std::uint8_t { Entity, ElementNode, IntegrationPoint, SectionPoint };
enum class ShearConvention : std::uint8_t { None, Tensor, Engineering };
enum class Configuration : std::uint8_t { Unspecified, Reference, Current };
enum class PhysicalTime : std::uint8_t { Unspecified, Current, HalfStep, Previous };
enum class Persistence : std::uint8_t { Persistent, Transient, Derived };
enum class RestartPolicy : std::uint8_t { Required, Reconstruct, Omit };
enum class MissingPolicy : std::uint8_t { Forbidden, ValidityMask };
enum class StorageKind : std::uint8_t { Dense, Ragged, Sparse };

// Version 1 retains its original three wire members. Version 2 adds explicit
// complex channels so a legacy time reader can reject spectral payloads.
enum class SampleAxisKind : std::uint8_t { Time, FrequencyHz, ModeNumber, BucklingEigenvalue, LoadFactor };
enum class ComplexChannels : std::uint8_t { None, RealImag, MagnitudePhase };
struct SampleAxisDescriptor {
    std::uint32_t version = 1;
    SampleAxisKind kind = SampleAxisKind::Time;
    std::string units = "UNSPECIFIED";
    ComplexChannels channels = ComplexChannels::None;
    void validate() const {
        if (units.empty() || (version != 1 && version != 2) || kind > SampleAxisKind::LoadFactor ||
            (version == 1 && (kind != SampleAxisKind::Time || channels != ComplexChannels::None)) ||
            (version == 2 && channels != ComplexChannels::RealImag && channels != ComplexChannels::MagnitudePhase))
            throw std::invalid_argument("unsupported sample axis/version/channels/units");
    }
    void validate_coordinate(double x) const {
        validate();
        if (!std::isfinite(x) || ((kind == SampleAxisKind::Time || kind == SampleAxisKind::FrequencyHz) && x < 0) ||
            (kind == SampleAxisKind::ModeNumber && (x < 1 || x > 9007199254740991.0 || std::floor(x) != x)))
            throw std::invalid_argument("invalid sample axis coordinate");
    }
    template<class F> void visit(F f) {
        f(version, kind, units);
        if (version == 2) f(channels);
    }
};
struct SpectralSampleMetadata {
    double coordinate = 0, eigenvalue = 0, natural_frequency_hz = 0, generalized_mass = 0, damping_ratio = 0;
    std::vector<double> participation_factors;
    template<class F> void visit(F f) {
        f(coordinate, eigenvalue, natural_frequency_hz, generalized_mass, damping_ratio, participation_factors);
    }
    void validate(const SampleAxisDescriptor &axis) const {
        axis.validate_coordinate(coordinate);
        if (!std::isfinite(eigenvalue) || !std::isfinite(natural_frequency_hz) || natural_frequency_hz < 0 ||
            !std::isfinite(generalized_mass) || generalized_mass < 0 || !std::isfinite(damping_ratio))
            throw std::invalid_argument("invalid spectral sample metadata");
        for (double value : participation_factors)
            if (!std::isfinite(value)) throw std::invalid_argument("nonfinite participation factor");
    }
};

template <class T> constexpr ScalarType scalar_type() {
    if constexpr (std::is_same_v<T, float>) return ScalarType::Float32;
    else if constexpr (std::is_same_v<T, double>) return ScalarType::Float64;
    else if constexpr (std::is_same_v<T, std::int32_t>) return ScalarType::Int32;
    else if constexpr (std::is_same_v<T, std::uint32_t>) return ScalarType::UInt32;
    else if constexpr (std::is_same_v<T, std::int64_t>) return ScalarType::Int64;
    else if constexpr (std::is_same_v<T, std::uint64_t>) return ScalarType::UInt64;
    else {
        static_assert(std::is_same_v<T, std::uint8_t>, "unsupported field scalar type");
        return ScalarType::UInt8;
    }
}
template <class E> inline bool enum_at_most(E value, E last) {
    return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(last);
}
struct FieldDescriptor {
    std::uint32_t version = 1;
    std::string name;
    ScalarType scalar = ScalarType::Float64;
    std::vector<std::uint32_t> shape{1};
    std::vector<std::string> components{"VALUE"}; // ordered; never infer tensor order
    Association association = Association::Element;
    FieldLocation location = FieldLocation::IntegrationPoint;
    ShearConvention shear = ShearConvention::None;
    std::string units = "UNSPECIFIED", basis = "UNSPECIFIED";
    Configuration configuration = Configuration::Unspecified;
    PhysicalTime time = PhysicalTime::Current;
    Persistence persistence = Persistence::Persistent;
    RestartPolicy restart = RestartPolicy::Required;
    MissingPolicy missing = MissingPolicy::Forbidden;
    StorageKind storage = StorageKind::Dense;
    std::size_t component_count() const {
        std::size_t n = 1;
        if (shape.empty() || shape.size() > 4)
            throw std::invalid_argument("field rank must be 1..4");
        for (auto extent : shape) {
            if (!extent) throw std::invalid_argument("zero field extent");
            n = product(n, extent);
        }
        return n;
    }
    void validate() const {
        if (version != 1 || name.empty() || units.empty() || basis.empty() ||
            !enum_at_most(scalar, ScalarType::UInt8) ||
            !enum_at_most(association, Association::Global) ||
            !enum_at_most(location, FieldLocation::SectionPoint) ||
            !enum_at_most(shear, ShearConvention::Engineering) ||
            !enum_at_most(configuration, Configuration::Current) ||
            !enum_at_most(time, PhysicalTime::Previous) ||
            !enum_at_most(persistence, Persistence::Derived) ||
            !enum_at_most(restart, RestartPolicy::Omit) ||
            !enum_at_most(missing, MissingPolicy::ValidityMask) ||
            !enum_at_most(storage, StorageKind::Sparse))
            throw std::invalid_argument("invalid field descriptor/version");
        if (component_count() != components.size())
            throw std::invalid_argument("field shape/component mismatch");
        std::set<std::string> names;
        for (const auto &c : components)
            if (c.empty() || !names.insert(c).second)
                throw std::invalid_argument("empty/duplicate field component");
        if (location != FieldLocation::Entity && association != Association::Element)
            throw std::invalid_argument("local point requires element association");
        if (persistence != Persistence::Persistent && restart == RestartPolicy::Required)
            throw std::invalid_argument("nonpersistent field cannot require restart");
        if (shear != ShearConvention::None &&
            (components.size() != 3 && components.size() != 6 && components.size() != 9))
            throw std::invalid_argument("shear convention requires tensor components");
    }
    template <class F> void visit(F f) {
        f(version, name, scalar, shape, components, association, location, shear, units, basis,
          configuration, time, persistence, restart, missing, storage);
    }
};
struct HistoryVariable {
    FieldDescriptor field;
    std::vector<double> initial{0};
    template <class F> void visit(F f) { f(field, initial); }
};
struct HistorySchema {
    std::string id;
    std::uint32_t version = 1;
    std::vector<HistoryVariable> variables;
    bool empty() const noexcept { return id.empty() && variables.empty() && version == 1; }
    // The existing packed StateDB history uses the model's scalar type and
    // committed/trial rollback semantics. Reject unsupported policies explicitly.
    template <class T> std::uint32_t validate() const {
        if (version == 0 || id.empty() || variables.empty())
            throw std::invalid_argument("invalid history schema identity/version");
        std::size_t count = 0;
        std::set<std::string> names;
        for (const auto &v : variables) {
            const auto &d = v.field;
            d.validate();
            if (!names.insert(d.name).second || d.scalar != scalar_type<T>() ||
                d.association != Association::Element || d.location != FieldLocation::IntegrationPoint ||
                d.persistence != Persistence::Persistent || d.restart != RestartPolicy::Required ||
                d.storage != StorageKind::Dense || d.missing != MissingPolicy::Forbidden ||
                d.time != PhysicalTime::Current)
                throw std::invalid_argument("unsupported/duplicate packed history descriptor");
            if (v.initial.size() != d.component_count())
                throw std::invalid_argument("history initialization shape mismatch");
            for (double x : v.initial)
                if (!std::isfinite(x) || std::abs(x) > std::numeric_limits<T>::max())
                    throw std::invalid_argument("history initialization scalar range");
            count = sum(count, d.component_count());
        }
        if (count >= std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("history component count exhausted");
        return static_cast<std::uint32_t>(count);
    }
    std::size_t offset(const std::string &name) const {
        std::size_t n = 0;
        for (const auto &v : variables) {
            if (v.field.name == name) return n;
            n = sum(n, v.field.component_count());
        }
        throw std::out_of_range("unknown history variable");
    }
    template <class F> void visit(F f) { f(id, version, variables); }
};
} // namespace sdb
