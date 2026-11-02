#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
void expect(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
template<class T> void run(const std::filesystem::path& inp,const std::filesystem::path& tmp){
    auto m=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(inp).model);
    expect(m.report.executable_cpu && m.definition.schema==9,"load capability");
    ModelStore::save(tmp/"inertial.model",m); auto loaded=ModelStore::load<T>(tmp/"inertial.model");
    CpuRun<T> cpu(loaded);
    for(Index n=0;n<m.mesh.nodes();++n)cpu.state().data().velocity.write()(n,0)=1;
    cpu.initialize();
    auto resultant=[&] {std::array<double,3> f{};for(Index n=0;n<m.mesh.nodes();++n)
        for(unsigned d=0;d<3;++d)f[d]+=cpu.state().data().external_force.read()(n,d);return f;};
    auto f=resultant();
    // Centrifugal (2,2,0), rotary (3,-3,0), Coriolis (0,-4,0), bottom pressure (0,0,2).
    expect(std::abs(f[0]-5)<1e-5 && std::abs(f[1]+5)<1e-5 && std::abs(f[2]-2)<1e-5,"rotation and HP resultant");
    cpu.advance();cpu.save(tmp/"inertial.rst");
    CpuRun<T> split(loaded);split.restore(tmp/"inertial.rst");
    while(cpu.advance())expect(split.advance(),"split cursor");
    f=resultant();expect(std::abs(f[0])+std::abs(f[1])<1e-8 && std::abs(f[2]-3)<1e-6,"OP=NEW removes all prior DLOAD");
    expect(native::pack(cpu.snapshot())==native::pack(split.snapshot()),"exact dynamic load restart");
    // Face 3 is the y=0 side with inward normal +Y: partial immersion of 0.25 m.
    // Pressure grows from 0 at z=.25 to 8 at z=0; exact net force is +1 Y.
    LoadContribution head{}; head.scalar=8; head.levels={.25,0};
    std::array<double,3> wet{};
    distributed::hydrostatic(m.mesh,{1,2,1},head,[&](Index,auto area,double w){
        for(unsigned d=0;d<3;++d)wet[d]+=area[d]*w;
    });
    expect(std::abs(wet[0])+std::abs(wet[2])+std::abs(wet[1]-1)<1e-12,"partial hydrostatic immersion");
    auto gyro=m.definition;
    gyro.steps.resize(1);gyro.steps[0].period=.02;
    gyro.foundation.face_loads.clear();
    std::erase_if(gyro.foundation.body_loads,[](const auto& l){return l.kind!=BodyLoadKind::Coriolis || l.definition.step.value()!=0;});
    auto g=ModelCompiler{}.compile<T>(gyro);CpuRun<T> free(g);
    for(Index n=0;n<g.mesh.nodes();++n)free.state().data().velocity.write()(n,0)=1;
    free.initialize();const auto initial=free.state().data().velocity.read();
    double speed=std::hypot(initial(0,0),initial(0,1));
    while(free.advance()){}
    auto v=free.state().data().velocity.read();
    expect(std::abs(std::hypot(v(0,0),v(0,1))-speed)<(sizeof(T)==4?3e-6:1e-12),"Coriolis kinetic energy");
}
int main(int argc,char**argv){
    if(argc!=3)return 2;
    std::filesystem::create_directories(argv[2]);run<float>(argv[1],argv[2]);run<double>(argv[1],argv[2]);
    std::cout<<"solid rotational loads, HP, OP=NEW, energy and restart PASS\n";
}
