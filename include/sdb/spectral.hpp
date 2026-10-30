#pragma once
#include "state_layout.hpp"
#include "io/native.hpp"
#include "units.hpp"

namespace sdb {
enum class SpectralAxis : std::uint8_t { FrequencyHz, ModeNumber };
enum class PhaseConvention : std::uint8_t { ExpPositiveIwt, ExpNegativeIwt };
enum class ModeNormalization : std::uint8_t { None, UnitMass, UnitMaximum, ConsumerDefined };
// Separate versioned storage-only contract. Never reinterprets time-result files.
struct SpectralSeries {
    std::uint32_t version=1;
    std::uint64_t model=0,equations=0,basis=0;
    SpectralAxis axis=SpectralAxis::FrequencyHz;
    PhaseConvention phase=PhaseConvention::ExpPositiveIwt;
    ModeNormalization normalization=ModeNormalization::None;
    std::string normalization_schema;
    FieldDescriptor field;
    Unit unit;
    StateLayout layout;
    std::vector<double> coordinates,real,imaginary;
    template<class F> void visit(F f) { f(version,model,equations,basis,axis,phase,normalization,normalization_schema,
        field,unit,layout,coordinates,real,imaginary); }
    void validate() const {
        field.validate(); unit.validate(); layout.validate(field);
        if(version!=1 || !model || !equations || field.scalar!=ScalarType::Float64 || field.units!=unit.symbol ||
           field.missing!=MissingPolicy::Forbidden || !enum_at_most(axis,SpectralAxis::ModeNumber) ||
           !enum_at_most(phase,PhaseConvention::ExpNegativeIwt) || !enum_at_most(normalization,ModeNormalization::ConsumerDefined) ||
           (axis==SpectralAxis::FrequencyHz && normalization!=ModeNormalization::None) ||
           (axis==SpectralAxis::ModeNumber && normalization==ModeNormalization::None) ||
           (normalization==ModeNormalization::ConsumerDefined && normalization_schema.empty()))
            throw std::invalid_argument("spectral schema");
        auto stride=product(layout.addresses.size(),field.component_count());
        if(real.size()!=product(coordinates.size(),stride) || imaginary.size()!=real.size())
            throw std::invalid_argument("spectral shape");
        for(std::size_t i=0;i<coordinates.size();++i) {
            auto x=coordinates[i];
            if(!std::isfinite(x) || x<0 || (i && x<=coordinates[i-1]) ||
               (axis==SpectralAxis::ModeNumber && (x<1 || x>9007199254740991.0 || std::floor(x)!=x)))
                throw std::invalid_argument("spectral coordinate");
        }
        for(auto x:real) if(!std::isfinite(x)) throw std::invalid_argument("spectral real");
        for(auto x:imaginary) if(!std::isfinite(x)) throw std::invalid_argument("spectral imaginary");
    }
    std::pair<std::size_t,std::size_t> interval(double first,double last) const {
        validate();
        if(!std::isfinite(first) || !std::isfinite(last) || first>last) throw std::invalid_argument("spectral interval");
        return {std::lower_bound(coordinates.begin(),coordinates.end(),first)-coordinates.begin(),
                std::upper_bound(coordinates.begin(),coordinates.end(),last)-coordinates.begin()};
    }
    void save(const std::filesystem::path &path,std::uint64_t budget=lifecycle::native::default_limit) const {
        validate(); auto bytes=lifecycle::native::pack(*this,budget);
        lifecycle::native::atomic_stream(path,[&](auto &out) {
            lifecycle::native::prefix(out,0x3154434550534453ull); lifecycle::native::record(out,1,bytes);
        });
    }
    static SpectralSeries load(const std::filesystem::path &path,std::uint64_t budget=lifecycle::native::default_limit) {
        namespace n=lifecycle::native;
        std::ifstream in(path,std::ios::binary); n::prefix(in,0x3154434550534453ull);
        auto out=n::unpack<SpectralSeries>(n::record(in,1,budget),budget); n::end(in); out.validate(); return out;
    }
};
} // namespace sdb
