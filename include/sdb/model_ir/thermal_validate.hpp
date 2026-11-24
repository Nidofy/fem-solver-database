#pragma once
#include "model_ir.hpp"

namespace sdb::lifecycle {
inline void validate_thermal_contract(const ModelIR &ir,const IRThermalFoundation &t) {
    auto require=[](bool ok,const char *reason) { if(!ok) throw std::invalid_argument(reason); };
    auto finite=[](double v) { return std::isfinite(v); };
    auto fraction=[&](double v) { return finite(v) && v>=0 && v<=1; };
    auto step=[&](StepId id) { require(id.valid() && id.value()<ir.steps.size(),"unknown thermal step"); };
    require(t.tag==0x314d4854 && t.version==1,"thermal extension tag/version");
    std::map<std::string,const MaterialTable*> tables;
    for(const auto &v:t.tables) {
        require(!v.name.empty() && tables.emplace(v.name,&v).second && !v.units.empty() &&
                v.components==1 && !v.axes.empty(),"invalid or duplicate thermal scalar table");
        require(v.interpolation<=InterpolationPolicy::Constant && v.extrapolation<=ExtrapolationPolicy::Linear &&
                !(v.interpolation==InterpolationPolicy::Constant && v.extrapolation==ExtrapolationPolicy::Linear),
                "thermal interpolation/extrapolation");
        std::size_t n=1;
        std::set<std::string> axes;
        for(const auto &a:v.axes) {
            require(!a.name.empty() && !a.units.empty() && axes.insert(a.name).second && !a.points.empty() &&
                    (v.extrapolation!=ExtrapolationPolicy::Linear || a.points.size()>1),"thermal table axis");
            n=product(n,a.points.size());
            for(std::size_t i=0;i<a.points.size();++i)
                require(finite(a.points[i]) && (!i || a.points[i]>a.points[i-1]),"nonmonotone thermal table axis");
        }
        require(n==v.values.size() && std::ranges::all_of(v.values,finite),"thermal Cartesian table shape/value");
    }
    auto table=[&](const std::string &ref,std::initializer_list<std::string_view> axes,bool nonnegative) -> const MaterialTable & {
        const auto it=tables.find(ref);
        require(it!=tables.end(),"unknown thermal table");
        const auto &v=*it->second;
        require(v.axes.size()==axes.size() && std::equal(axes.begin(),axes.end(),v.axes.begin(),
                [](auto name,const TableAxis &a){return name==a.name;}),"thermal table axis contract");
        if(nonnegative) require(std::ranges::all_of(v.values,[](double x){return x>=0;}),"negative thermal property");
        return v;
    };
    auto value=[&](const IRThermalValue &v,bool coefficient) {
        require(finite(v.constant) && v.source<=ThermalValueSource::Amplitude,"invalid thermal driver");
        if(v.source==ThermalValueSource::Constant)
            require(v.reference.empty() && (!coefficient || v.constant>=0),"thermal constant");
        else {
            require(v.constant==0 && !v.reference.empty(),"ambiguous thermal driver");
            if(v.source==ThermalValueSource::Table)
                table(v.reference,{coefficient?"temperature":"time"},coefficient);
            else require(!coefficient && std::ranges::any_of(ir.amplitudes,[&](const auto &a){return a.name==v.reference;}),
                         "unknown thermal temperature curve");
        }
    };
    std::set<std::string> conditions;
    auto condition=[&](const auto &v) {
        step(v.step);
        require(!v.name.empty() && conditions.insert(v.name).second && !v.region.name.empty() &&
                v.region.kind<=ThermalRegionKind::ElementSet,"invalid thermal condition name or region");
    };
    for(const auto &v:t.films) { condition(v); value(v.sink_temperature,false); value(v.film_coefficient,true); }
    for(const auto &v:t.radiation) {
        condition(v);
        require(fraction(v.emissivity) && finite(v.absolute_zero) && finite(v.ambient_temperature) &&
                v.ambient_temperature>=v.absolute_zero && finite(v.stefan_boltzmann) && v.stefan_boltzmann>0,
                "radiation absolute temperature, emissivity or Stefan-Boltzmann constant");
    }
    for(const auto &v:t.fluxes) {
        condition(v);
        require(finite(v.rate) && v.kind<=HeatFluxKind::BodyGeneration &&
                (v.kind==HeatFluxKind::BodyGeneration)==(v.region.kind==ThermalRegionKind::ElementSet),
                "heat flux measure/region mismatch");
    }
    std::set<std::string> contacts,materials;
    for(const auto &v:t.contacts) {
        require(!v.name.empty() && contacts.insert(v.name).second && fraction(v.eta_fric) && fraction(v.f_slave),
                "thermal contact name or heat partition");
        table(v.gap_conductance_table,{"pressure","clearance","temperature"},true);
        require(fraction(v.gap_emissivity) && finite(v.absolute_zero) && finite(v.stefan_boltzmann) &&
                (v.radiative_gap ? v.stefan_boltzmann>0 : v.gap_emissivity==0 && v.absolute_zero==0 && v.stefan_boltzmann==0),
                "invalid gap radiation contract");
    }
    for(const auto &v:t.materials) {
        require(materials.insert(v.name).second && std::ranges::any_of(ir.materials,[&](const auto &m){return m.name==v.name;}),
                "unknown or duplicate thermal material");
        table(v.conductivity,{"temperature"},true);
        table(v.specific_heat,{"temperature"},true);
        if(!v.enthalpy.empty()) {
            const auto &h=table(v.enthalpy,{"temperature"},false);
            require(std::ranges::is_sorted(h.values),"decreasing material enthalpy");
        }
        require(finite(v.solidus_temp) && finite(v.liquidus_temp) && finite(v.latent_heat) &&
                (v.phase_change ? !v.enthalpy.empty() && v.solidus_temp<v.liquidus_temp && v.latent_heat>=0 :
                 v.solidus_temp==0 && v.liquidus_temp==0 && v.latent_heat==0),"invalid latent heat interval");
    }
    std::set<StepId> steps;
    for(const auto &v:t.coupled_steps) {
        step(v.step);
        require(steps.insert(v.step).second && v.scheme<=CouplingScheme::Monolithic &&
                finite(v.thermal_tolerance) && v.thermal_tolerance>0 && finite(v.mechanical_tolerance) &&
                v.mechanical_tolerance>0 && fraction(v.beta_tq),"invalid coupled step");
    }
}
} // namespace sdb::lifecycle
