#include <sdb/lifecycle.hpp>
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
    if(plastic ? !(ie>se) : (ie!=se || std::abs(ie+ke+viscous-wk)>(damping?2e-4:2e-6)))
        throw std::runtime_error("internal energy reference");
}
int main(int argc,char **argv) {
    try {
        if(argc!=3) throw std::invalid_argument("fixtures scratch");
        std::filesystem::create_directories(argv[2]);
        for(auto file:{"forced_tet.inp","j2_tet.inp"}) {
            auto path=std::filesystem::path(argv[1])/file;
            run_test<float>(path,argv[2]);run_test<double>(path,argv[2]);run_test<double>(path,argv[2],true);
        }
        std::cout<<"work, internal energy, balance and exact restart PASS\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<std::endl;return 1;}
}
