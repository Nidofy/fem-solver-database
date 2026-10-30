#pragma once
#include "abaqus.hpp"
#include "../analysis_contract.hpp"

namespace sdb::lifecycle {
// Storage-only fragment importer. Labels -> thermal slots are supplied explicitly.
// Supports only constant nodal TEMPERATURE/CFLUX and initial TEMPERATURE.
// General sets, amplitudes, OP=NEW, steps and the rest of INP remain rejected here.
inline AnalysisContract read_thermal_assignments(std::istream &in,const DofSpace &space,
    const std::map<Id,std::uint64_t> &node_slots,const Unit &temperature,const Unit &heat_rate,
    double begin,double end,std::size_t max_bytes=1024*1024) {
    temperature.validate(); heat_rate.validate();
    if(temperature.powers!=std::array<std::int32_t,7>{0,0,0,0,1,0,0} ||
       heat_rate.powers!=std::array<std::int32_t,7>{2,1,-3,0,0,0,0} || heat_rate.offset!=0)
        throw std::invalid_argument("thermal input units");
    std::set<std::uint64_t> unique;
    for(const auto &[label_value,slot]:node_slots) {
        if(!label_value || slot>=space.slots() || !unique.insert(slot).second) throw std::invalid_argument("thermal source map");
        const auto &key=space.data().bindings[slot].key;
        auto it=std::find_if(space.data().variables.begin(),space.data().variables.end(),[&](const auto &v){return v.id==key.variable;});
        if(key.owner!=DofOwner::Node || it==space.data().variables.end() || it->quantity!=DofQuantity::Temperature)
            throw std::invalid_argument("thermal source slot quantity");
    }
    AnalysisContract out;out.equations=space.identity();out.consumer_schema="sdb.thermal-constant-input/v1";out.begin=begin;out.end=end;
    std::string line,keyword;std::size_t used=0,line_number=0;
    auto consume=[&] {
        ++line_number;line=trim(line);Source at{"thermal-fragment",line_number,1};
        if(line.empty() || line.starts_with("**")) return;
        if(line.starts_with('*')) {
            auto fields=csv(line.substr(1));keyword=upper(fields[0]);
            if(keyword=="INITIAL CONDITIONS") {
                if(fields.size()!=2 || upper(fields[1])!="TYPE=TEMPERATURE") fail(at,"only initial TEMPERATURE supported");
            } else if(keyword=="TEMPERATURE" || keyword=="CFLUX") {
                if(fields.size()>2 || (fields.size()==2 && upper(fields[1])!="OP=MOD")) fail(at,"only OP=MOD supported");
            } else fail(at,"unsupported thermal fragment keyword: "+keyword);
            return;
        }
        if(keyword.empty()) fail(at,"thermal data before keyword");
        auto fields=csv(line);const bool flux=keyword=="CFLUX";
        if(fields.size()!=(flux?3u:2u) || (flux && fields[1]!="11")) fail(at,"thermal nodal row requires explicit label and thermal DOF 11");
        auto source=label(fields[0],at);auto it=node_slots.find(source);if(it==node_slots.end()) fail(at,"unknown thermal node label");
        SlotAssignment a;a.slot=it->second;a.unit=flux?heat_rate:temperature;
        a.role=flux?AssignmentRole::Load:(keyword=="TEMPERATURE"?AssignmentRole::Prescribed:AssignmentRole::Initial);
        auto value=number(fields.back(),at);a.times={begin};a.values={value};
        if(a.role!=AssignmentRole::Initial){a.times.push_back(end);a.values.push_back(value);}
        out.assignments.push_back(std::move(a));
    };
    // Read with a hard byte bound, including a malicious single very long line.
    char ch;
    while(in.get(ch)) {
        if(used==max_bytes) throw std::length_error("thermal input byte budget");
        ++used;
        if(ch=='\n'){consume();line.clear();}else line.push_back(ch);
    }
    if(in.bad()) throw std::runtime_error("thermal input read");
    if(!line.empty())consume();
    out.validate(space);return out;
}
} // namespace sdb::lifecycle
