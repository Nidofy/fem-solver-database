#pragma once
#include "dof_space.hpp"
#include "units.hpp"

namespace sdb {
enum class AssignmentRole : std::uint8_t { Initial, Prescribed, Load };
struct SlotAssignment {
    std::uint64_t slot=0;
    AssignmentRole role=AssignmentRole::Initial;
    Unit unit;
    std::uint64_t basis=0;
    std::vector<double> times,values; // time is SI seconds; linear interpolation is explicit
    template<class F> void visit(F f) { f(slot,role,unit,basis,times,values); }
    void validate(const DofSpace &space,double begin,double end) const {
        unit.validate();
        if(slot>=space.data().bindings.size() || !enum_at_most(role,AssignmentRole::Load) ||
           times.empty() || times.size()!=values.size()) throw std::invalid_argument("analysis assignment");
        for(std::size_t i=0;i<times.size();++i)
            if(!std::isfinite(times[i]) || !std::isfinite(values[i]) || times[i]<begin || times[i]>end ||
               (i && times[i]<=times[i-1])) throw std::invalid_argument("assignment time/value");
        if(role==AssignmentRole::Initial && (times.size()!=1 || times.front()!=begin))
            throw std::invalid_argument("initial condition must occur at step start");
        if(role!=AssignmentRole::Initial && (times.front()!=begin || times.back()!=end))
            throw std::invalid_argument("assignment must cover entire step; no implicit extrapolation");
        if(role==AssignmentRole::Prescribed && space.data().bindings[slot].status!=DofStatus::Prescribed)
            throw std::invalid_argument("prescribed assignment needs a prescribed slot");
        if(role!=AssignmentRole::Load) {
            auto variable=space.data().bindings[slot].key.variable;
            const auto &variables=space.data().variables;
            auto it=std::find_if(variables.begin(),variables.end(),[&](const auto &v){return v.id==variable;});
            if(it==variables.end() || it->units!=unit.symbol) throw std::invalid_argument("assignment/DOF unit mismatch");
            std::array<std::int32_t,7> expected{};
            if(it->quantity==DofQuantity::Translation) expected[0]=1;
            else if(it->quantity==DofQuantity::Temperature) expected[4]=1;
            else if(it->quantity==DofQuantity::Pressure) expected={-1,1,-2,0,0,0,0};
            if(it->quantity!=DofQuantity::Multiplier && it->quantity!=DofQuantity::User && unit.powers!=expected)
                throw std::invalid_argument("assignment/DOF dimensions");
        }
    }
    double evaluate(double time) const {
        if(!std::isfinite(time) || times.empty() || times.size()!=values.size() || time<times.front() || time>times.back())
            throw std::out_of_range("assignment time");
        for(std::size_t k=0;k<times.size();++k)
            if(!std::isfinite(times[k]) || !std::isfinite(values[k]) || (k && times[k]<=times[k-1]))
                throw std::invalid_argument("invalid interpolation table");
        auto it=std::lower_bound(times.begin(),times.end(),time); auto i=static_cast<std::size_t>(it-times.begin());
        if(i==0 || *it==time) return values[i];
        auto fraction=(time-times[i-1])/(times[i]-times[i-1]);
        auto out=std::lerp(values[i-1],values[i],fraction);
        if(!std::isfinite(out)) throw std::overflow_error("assignment interpolation");
        return out;
    }
};
struct AnalysisContract {
    std::uint32_t version=1;
    std::uint64_t equations=0,step=1;
    std::string consumer_schema;
    double begin=0,end=1;
    std::vector<SlotAssignment> assignments;
    std::vector<double> output_times;
    template<class F> void visit(F f) { f(version,equations,step,consumer_schema,begin,end,assignments,output_times); }
    void validate(const DofSpace &space) const {
        if(version!=1 || equations!=space.identity() || !step || consumer_schema.empty() ||
           !std::isfinite(begin) || !std::isfinite(end) || begin<0 || end<=begin) throw std::invalid_argument("analysis context");
        std::set<std::pair<std::uint64_t,AssignmentRole>> keys;
        for(const auto &a:assignments) {
            a.validate(space,begin,end);
            if(!keys.emplace(a.slot,a.role).second) throw std::invalid_argument("duplicate analysis assignment");
        }
        for(std::size_t i=0;i<output_times.size();++i)
            if(!std::isfinite(output_times[i]) || output_times[i]<begin || output_times[i]>end ||
               (i && output_times[i]<=output_times[i-1])) throw std::invalid_argument("analysis output times");
    }
    auto encode(const DofSpace &space,std::uint64_t budget=lifecycle::native::default_limit) const {
        validate(space); return lifecycle::native::pack(*this,budget);
    }
    static AnalysisContract decode(std::span<const std::uint8_t> bytes,const DofSpace &space,
                                   std::uint64_t budget=lifecycle::native::default_limit) {
        auto out=lifecycle::native::unpack<AnalysisContract>(bytes,budget); out.validate(space); return out;
    }
};
} // namespace sdb
