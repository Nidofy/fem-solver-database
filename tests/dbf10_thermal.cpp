#include <sdb/input/thermal_contract.hpp>
#include <sdb/io/model_store.hpp>
#include <iostream>

using namespace sdb;
using namespace sdb::lifecycle;
static void check(bool v,const char *s) { if(!v) throw std::runtime_error(s); }
template<class F> static void rejects(F f) {
    try { f(); } catch(const std::exception &) { return; }
    throw std::runtime_error("invalid thermal contract accepted");
}
static MaterialTable table(std::string name,std::vector<TableAxis> axes,std::vector<double> values) {
    MaterialTable t; t.name=std::move(name); t.axes=std::move(axes); t.values=std::move(values); return t;
}
static IRThermalFoundation fixture(const std::string &surface) {
    IRThermalFoundation t;
    TableAxis temp{"temperature","K",{273,373}},time{"time","s",{0,1}};
    t.tables={table("k",{temp},{14,17}),table("cp",{temp},{400,420}),table("H",{temp},{100,150}),
              table("film",{temp},{3,4}),table("sink",{time},{300,301}),
              table("gap",{{"pressure","Pa",{0,10}},{"clearance","m",{0,0.1}},temp},{1,2,3,4,5,6,7,8})};
    IRThermalRegion face{surface},body{"100",ThermalRegionKind::ElementSet};
    StepId step(0);
    t.films={{"film",step,face,{ThermalValueSource::Table,0,"sink"},{ThermalValueSource::Table,0,"film"}},
             {"constant",step,body,{ThermalValueSource::Constant,290,{}},{ThermalValueSource::Constant,3,{}}},
             {"curve",step,face,{ThermalValueSource::Amplitude,0,"TENV"},{ThermalValueSource::Constant,3,{}}}};
    t.radiation={{"radiation",step,face,0.75,25,-273.15,5.670374419e-8}};
    t.fluxes={{"body",step,body,HeatFluxKind::BodyGeneration,127},{"surface",step,face,HeatFluxKind::SurfaceFlux,-2}};
    t.contacts={{"gap","gap",true,0.4,-273.15,5.67e-8,0.9,0.35}};
    t.materials={{"STEEL","k","cp","H",true,300,340,42}};
    t.coupled_steps={{step,CouplingScheme::PicardStaggered,1e-5,2e-6,0.8}};
    return t;
}
int main(int argc,char **argv) { try {
    if(argc!=4) return 2;
    std::ifstream inp(argv[2]); auto ir=AbaqusInpReader{}.read(inp,argv[2]).model;
    auto base=ModelCompiler{}.compile<double>(ir);
    auto temp=fixture(base.foundation.surface_ids.begin()->first);
    IRAmplitude amp; amp.name="TENV"; amp.points={{0,300},{1,305}}; ir.amplitudes.push_back(amp);
    admit_thermal_contract(ir,temp);
    auto model=ModelCompiler{}.compile<double>(ir);
    auto altered=ir;
    std::ranges::reverse(altered.foundation.thermal.tables); std::ranges::reverse(altered.foundation.thermal.films);
    check(canonical_semantic_hash(altered)==model.semantic_identity(),"order-dependent thermal identity");
    altered.foundation.thermal.contacts[0].f_slave=0.36;
    check(canonical_semantic_hash(altered)!=model.semantic_identity(),"thermal identity omitted contact property");
    auto direct=ir.foundation; direct.thermal.contacts[0].f_slave=0.36;
    check(semantic_hash(direct)!=semantic_hash(ir.foundation),"standalone foundation omitted thermal section");
    check(!model.report.executable_cpu && descriptor("DC3D8",{}).points==8,"thermal storage admission");
    if(std::string(argv[1])=="read") {
        auto saved=ModelStore::load<double>(argv[3]);
        check(native::pack(saved.definition.foundation.thermal)==native::pack(temp) &&
              saved.semantic_identity()==model.semantic_identity() && saved.analysis_identity()==model.analysis_identity(),"thermal round trip");
        std::cout<<"thermal_semantic="<<saved.semantic_identity()<<" analysis="<<saved.analysis_identity()<<"\n"; return 0;
    }
    auto invalid=[&](auto mutate) { auto t=temp; mutate(t); rejects([&]{admit_thermal_contract(ir,t);}); };
    invalid([](auto &t){t.radiation[0].emissivity=1.1;}); invalid([](auto &t){t.radiation[0].absolute_zero=26;});
    invalid([](auto &t){t.radiation[0].stefan_boltzmann=0;}); invalid([](auto &t){t.contacts[0].eta_fric=-0.1;});
    invalid([](auto &t){t.contacts[0].f_slave=1.1;}); invalid([](auto &t){t.contacts[0].gap_conductance_table="cp";});
    invalid([](auto &t){t.materials[0].liquidus_temp=t.materials[0].solidus_temp;});
    invalid([](auto &t){t.materials[0].latent_heat=-1;}); invalid([](auto &t){t.coupled_steps[0].beta_tq=2;});
    invalid([](auto &t){t.coupled_steps[0].thermal_tolerance=0;}); invalid([](auto &t){t.fluxes[0].kind=HeatFluxKind::SurfaceFlux;});
    invalid([](auto &t){t.tables[0].values[0]=-1;}); invalid([](auto &t){t.films.back().sink_temperature.reference="missing";});
    invalid([](auto &t){t.coupled_steps[0].step=StepId(9);}); invalid([](auto &t){t.tables.back().values.pop_back();});
    for(auto scheme:{CouplingScheme::SequentialThermalToMechanical,CouplingScheme::Monolithic}) {
        auto t=temp; t.coupled_steps[0].scheme=scheme; validate_thermal_contract(ir,t);
    }
    altered=ir; altered.foundation.thermal.films[0].region.name="UNKNOWN";
    rejects([&]{ ModelCompiler{}.compile<double>(altered); });
    altered=ir; altered.schema=10; rejects([&]{native::pack(altered);});
    auto wire=native::pack(ir); auto tail=native::pack(temp); wire[wire.size()-tail.size()]^=128;
    rejects([&]{native::unpack<ModelIR>(wire);});
    ModelStore::save(argv[3],model);
    return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
