#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
using namespace sdb::physics;
double value(const ResultFrame &f,const std::string& name) {
    for(const auto& field:f.fields) if(field.descriptor.name==name) return field.values.at(0);
    throw std::runtime_error("missing section energy: "+name);
}
template<class T> void torsion(const std::filesystem::path& path,const std::filesystem::path& dir) {
    std::ifstream deck(path);
    std::string text(std::istreambuf_iterator<char>(deck),{});
    text.replace(text.find("2,2,6"),5,"2,1,3\n2,5,6");
    text.replace(text.find("2,1,.002"),8,"2,4,.0002");
    text.insert(text.find("*End Step"),"*Output,field,frequency=50\n*Energy Output\nALLKE,ALLSE,ALLIE,ALLWK,ALLAE,ALLVD,ETOTAL\n");
    auto ir=AbaqusInpReader{}.buffer(text,path).model;
    // Torsional restoring penalty from the free end to the grounded end.
    ir.schema=6;ir.penalty_stiffness=.02;
    IREquation eq;eq.id=ConstraintId(1); eq.terms={{{"","",1},Dof::RX,1},{{"","",2},Dof::RX,-1}};
    ir.foundation.equations={eq};
    auto m=ModelCompiler{}.compile<T>(ir);
    SectionRun<T> run(m),split(m);
    for(int i=0;i<437;++i) run.advance();
    if(!run.snapshot().fields.empty()) throw std::runtime_error("non-due energy frame");
    run.save(dir/"torsion.sdbrst");split.restore(dir/"torsion.sdbrst");
    while(run.advance()) if(!split.advance()) throw std::runtime_error("schedule mismatch");
    auto f=run.snapshot();
    if(native::pack(f)!=native::pack(split.snapshot()))throw std::runtime_error("section energy restart");
    auto n=m.sources.node_id({"","",2}).value();
    double theta=run.state().data().rotation->angle.read()(n,0), wk=value(f,"ALLWK");
    double ke=value(f,"ALLKE"),se=value(f,"ALLSE"),ie=value(f,"ALLIE");
    if(!(ke>0 && se>0 && std::abs(wk) > 1e-10) || std::abs(.0002*theta-wk)>1e-10 ||
       std::abs((ie-se)-.01*theta*theta)>1e-11 || std::abs(value(f,"ETOTAL"))>1e-9 ||
       value(f,"ALLAE")!=0 || value(f,"ALLVD")!=0)
        throw std::runtime_error("torque work, stored penalty or energy budget");
}
void distributed_work(const std::filesystem::path& dir) {
    auto ir=ReaderRegistry{}.read(dir/"section_beam_dload.inp").model;
    for(auto& step:ir.steps) step.outputs={{false,7,0,"",{"ALLWK","ETOTAL"},{}}};
    auto model=ModelCompiler{}.compile<double>(ir);SectionRun<double> run(model);
    run.initialize();double expected=0;
    while(run.state().data().step<run.increments()) {
        const auto old=run.state().data().displacement;
        auto n=run.state().data().step;
        const auto f=n<10 ? std::array<double,3>{1.2*(n*.1),-3*(n*.1),2.1*(n*.1)} :
                            std::array<double,3>{0,4,0};
        run.advance();
        for(unsigned a=0;a<2;++a) for(unsigned d=0;d<3;++d)
            expected+=.5*f[d]*(run.state().data().displacement.read()(a,d)-old.read()(a,d));
    }
    auto f=run.snapshot();
    if(!(expected>0) || std::abs(value(f,"ALLWK")-expected)>1e-12)
        throw std::runtime_error("multi-step distributed force work");
}
void drill_energy(const std::filesystem::path& dir) {
    auto ir=ReaderRegistry{}.read(dir/"section_shell.inp").model;
    auto model=ModelCompiler{}.compile<double>(ir);
    StateDB<double> state(model.mesh,true);
    SectionElement el(model,0,model.sections[0]);
    // Alternating drill angles have zero centre drill, membrane, bending and transverse shear.
    // Two differences against node 0 have magnitude 2a; each spring is 0.001*A*t*G.
    double angle=.02;
    for(unsigned n=0;n<4;++n) state.data().rotation->angle.write()(n,2)=n%2 ? -angle : angle;
    auto [total, artificial]=el.energies(state);
    double exact=.001*.1*50*4*angle*angle;
    if(std::abs(artificial-exact)>1e-14 || std::abs(total-artificial)>1e-14)
        throw std::runtime_error("shell artificial drill energy decomposition");
}
int main(int argc,char**argv) {
    try {
        if(argc!=3)throw std::invalid_argument("fixtures scratch");
        std::filesystem::create_directories(argv[2]);
        auto path=std::filesystem::path(argv[1])/"section_beam.inp";
        drill_energy(argv[1]);distributed_work(argv[1]);
        torsion<float>(path,argv[2]);torsion<double>(path,argv[2]);
        std::cout<<"section torque work, energy balance and restart PASS\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
}
