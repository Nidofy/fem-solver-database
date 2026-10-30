#pragma once
#include "io/native.hpp"
#include <array>

namespace sdb {
// SI base dimensions: length, mass, time, current, temperature, amount, luminosity.
// Integer powers are deliberately bounded; fractional dimensions are not inferred.
struct Unit {
    std::uint32_t version=1;
    std::string symbol="1";
    std::array<std::int32_t,7> powers{};
    double scale=1, offset=0; // SI = value*scale + offset
    template<class F> void visit(F f) { f(version,symbol,powers,scale,offset); }
    void validate() const {
        if(version!=1 || symbol.empty() || !std::isfinite(scale) || scale<=0 || !std::isfinite(offset))
            throw std::invalid_argument("unit definition");
        for(auto p:powers) if(p < -64 || p > 64) throw std::invalid_argument("unit exponent");
        if(offset!=0 && powers!=std::array<std::int32_t,7>{0,0,0,0,1,0,0})
            throw std::invalid_argument("affine offset requires absolute temperature");
    }
    double convert(double value,const Unit &target) const {
        validate(); target.validate();
        if(powers!=target.powers || !std::isfinite(value)) throw std::invalid_argument("incompatible units/value");
        auto out=(value*scale+offset-target.offset)/target.scale;
        if(!std::isfinite(out)) throw std::overflow_error("unit conversion overflow");
        return out;
    }
    Unit combine(const Unit &other,bool divide=false) const {
        validate(); other.validate();
        if(offset!=0 || other.offset!=0) throw std::invalid_argument("affine unit algebra needs an explicit difference unit");
        Unit out; out.symbol="("+symbol+(divide?"/":"*")+other.symbol+")";
        for(std::size_t i=0;i<7;++i) out.powers[i]=powers[i]+(divide?-other.powers[i]:other.powers[i]);
        out.scale=divide?scale/other.scale:scale*other.scale; out.validate(); return out;
    }
};
} // namespace sdb
