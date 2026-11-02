#include <sdb/lifecycle.hpp>
#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
double scalar(const ResultFrame& f,const std::string &name) {
    for(const auto &field:f.fields) if(field.descriptor.name==name && !field.descriptor.history)
        return field.values.at(0);
    throw std::runtime_error("missing energy field "+name);
}
template<class T> void run_test(const std::filesystem::path &fixture,const std::filesystem::path &scratch, bool damping=false) {
    auto ir=ReaderRegistry{}.read(fixture).model;
    for(auto &step:ir.steps) for(auto &request:step.outputs)
        if(std::find(request.variables.begin(),request.variables.end(),"ALLSE")!=request.variables.end()) {
            request.variables.insert(request.variables.end(),{"ALLIE","ALLWK","ALLVD","ETOTAL"});
        }
    auto m=ModelCompiler{}.compile<T>(ir);
    ExplicitControls options; if(damping) options.bulk_linear=.06;
    CpuRun<T> full(m,options),split(m,options);
    for(int i=0;i<713;++i) full.advance();
    full.save(scratch/"work.sdbrst"); split.restore(scratch/"work.sdbrst");
    while(full.advance()) if(!split.advance()) throw std::runtime_error("restart schedule");
    auto f=full.snapshot();
    if(native::pack(f)!=native::pack(split.snapshot())) throw std::runtime_error("energy restart");
    const double viscous=scalar(f,"ALLVD");
    if(damping ? !(viscous>0) : viscous!=0) throw std::runtime_error("viscous dissipation");
    const auto wk=scalar(f,"ALLWK"),ie=scalar(f,"ALLIE"),se=scalar(f,"ALLSE"),ke=scalar(f,"ALLKE");
    auto u=full.state().data().displacement.read()(m.mesh.node_index(4),0);
    if(std::abs(wk-u)>2e-6 || std::abs(scalar(f,"ETOTAL")-(ie+ke+viscous-wk))>1e-11)
        throw std::runtime_error("external work or total definition");
    bool plastic=!ir.plastic.empty();
    full.state().data().element_active.fill(0);
    if(scalar(full.snapshot(),"ALLIE")!=0) throw std::runtime_error("inactive internal energy");
    if(plastic ? !(ie>se) : (ie!=se || std::abs(ie+ke+viscous-wk)>(damping?2e-4:2e-6)))
        throw std::runtime_error("internal energy reference");
}
void boundary_cases(const std::filesystem::path& dir) {
    for (bool finite:{false,true}) {
        auto ir=ReaderRegistry{}.read(dir/(finite?"finite_tet.inp":"forced_tet.inp")).model;
        if(finite) {
            for(auto& block:ir.initial_boundaries) for(auto& a:block.actions)
                if(a.target=="FREE") a.last=a.first; // release the pressure-normal Z DOF
        } else {
            IREquation eq; eq.id=ConstraintId(1);
            eq.terms={{{"","",10},Dof::X,1},{{"","",40},Dof::X,-1}};
            ir.schema=6;ir.penalty_stiffness=100;ir.foundation.equations={eq};
        }
        for(auto& r:ir.steps[0].outputs)
            if(std::find(r.variables.begin(),r.variables.end(),"ALLSE")!=r.variables.end())
                r.variables={"ALLKE","ALLSE","ALLIE","ALLWK","ETOTAL"};
        auto model=ModelCompiler{}.compile<double>(ir);
        CpuRun<double> run(model);run.initialize();
        auto node=model.sources.node_id({"","",40}).value();
        double reference_work=0;
        while(!run.finished()) {
            auto u0=run.state().data().displacement;
            auto applied=run.state().data().external_force;
            run.advance();
            auto u=run.state().data().displacement.read();
            if(finite) {
                for(std::size_t n=0;n<u.count;++n) for(unsigned c=0;c<3;++c)
                    reference_work+=applied.read()(n,c)*(u(n,c)-u0.read()(n,c));
            } else reference_work+=u(node,0)-u0.read()(node,0); // F=1; excludes penalty reaction
        }
        auto f=run.snapshot();const double wk=scalar(f,"ALLWK");
        auto x=run.state().data().displacement.read()(node,0);
        auto potential=finite?0.:.5*100*x*x;
        if(!(reference_work>1e-8) || std::abs(wk-reference_work)>1e-12 ||
           std::abs(scalar(f,"ALLIE")-scalar(f,"ALLSE")-potential)>1e-12 ||
           std::abs(scalar(f,"ETOTAL"))>1e-5)
            throw std::runtime_error("constraint/follower work or energy balance");
    }
    auto ir=ReaderRegistry{}.read(dir/"section_beam.inp").model;
    ir.steps[0].outputs.push_back({false,1,0,"",{"ALLWK"},{}});
    auto beam=ModelCompiler{}.compile<double>(ir);
    bool rejected=false;
    try {physics::SectionRun<double> section(beam);}
    catch(const std::invalid_argument&) {rejected=true;}
    if(!rejected) throw std::runtime_error("unavailable section recovery accepted");
}
int main(int argc,char **argv) {
    try {
        if(argc!=3) throw std::invalid_argument("fixtures scratch");
        std::filesystem::create_directories(argv[2]);
        for(auto file:{"forced_tet.inp","j2_tet.inp"}) {
            auto path=std::filesystem::path(argv[1])/file;
            run_test<float>(path,argv[2]);run_test<double>(path,argv[2]);run_test<double>(path,argv[2],true);
        }
        boundary_cases(argv[1]);
        std::cout<<"work, internal energy, balance and exact restart PASS\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 1;}
}
