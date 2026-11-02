#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
using physics::SectionRun;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class T> void simulate(const std::filesystem::path& fixtures,const std::filesystem::path& dir,bool beam) {
    auto ir=ReaderRegistry{}.read(fixtures/(beam?"section_beam_dload.inp":"section_shell_dload.inp")).model;
    auto model=ModelCompiler{}.compile<T>(ir);
    ModelStore::save(dir/"section.sdbmodel",model);
    auto loaded=ModelStore::load<T>(dir/"section.sdbmodel");SectionRun<T> run(loaded);
    auto totals=[&](std::array<double,3> reference) {
        std::array<double,3> force{}, moment{};
        for(Index n=0;n<loaded.mesh.nodes();++n) {
            std::array<double,3>x{},f{};
            for(unsigned i=0;i<3;++i){x[i]=loaded.mesh.reference()(n,i);f[i]=run.state().data().external_force.read()(n,i);force[i]+=f[i];}
            auto contribution=distributed::cross(x,f);for(unsigned i=0;i<3;++i)moment[i]+=contribution[i];
        }
        auto exact=distributed::cross({.5,beam?0:.5,0},reference);
        for(unsigned i=0;i<3;++i)check(std::abs(reference[i]-force[i])+std::abs(exact[i]-moment[i])<2e-6,"section load resultant/moment");
    };
    run.initialize();totals(beam?std::array<double,3>{}:std::array<double,3>{1,.5,1});
    for(int i=0;i<10;++i)check(run.advance(),"step 1 count");
    run.save(dir/"section.rst");totals(beam?std::array<double,3>{1.2,-3,2.1}:std::array<double,3>{1,.5,1});
    SectionRun<T> split(loaded);split.restore(dir/"section.rst");
    while(run.advance())check(split.advance(),"split cursor");
    auto resumed=split.snapshot(), baseline=run.snapshot();
    check(resumed.fields.size()==baseline.fields.size(),"section restart field count");
    for(std::size_t i=0;i<baseline.fields.size();++i) {
        const auto& a=baseline.fields[i];const auto& b=resumed.fields[i];
        check(a.descriptor.name==b.descriptor.name && a.typed && b.typed &&
              a.typed->addresses==b.typed->addresses && typed_values<T>(a)==typed_values<T>(b),"section loads restart parity");
    }
    auto frame=run.snapshot();
    check(frame.info.step==1 && frame.info.increment==20 && !frame.fields.empty(),"step-local S/E recovery");
    totals(beam?std::array<double,3>{0,4,0}:std::array<double,3>{0,0,2});
}
int main(int argc,char**argv){
    if(argc!=3)return 2;
    std::filesystem::create_directories(argv[2]);
    for(bool beam:{false,true}){simulate<float>(argv[1],argv[2],beam);simulate<double>(argv[1],argv[2],beam);}
    std::cout<<"six-DOF sectional DLOAD resultant, moment, multi-step and restart PASS\n";
}
