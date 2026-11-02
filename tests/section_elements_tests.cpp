#include <sdb/physics/section_run.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;using namespace sdb::physics;
template<class T>void test(const std::filesystem::path& dir,const std::filesystem::path& scratch,const char*file){
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(dir/file).model);
    SectionRun<T> run(model);
    // Infinitesimal rigid rotation about arbitrary axis plus translation: all six forces vanish.
    auto& el=run.element(0);auto x=model.mesh.reference();double q[24]{};
    for(unsigned a=0;a<el.count;++a) {
        auto n=el.nodes[a]; q[6*a]=2+2*x(n,2)-3*x(n,1);q[6*a+1]=3+3*x(n,0)-x(n,2);
        q[6*a+2]=4+x(n,1)-2*x(n,0);q[6*a+3]=1;q[6*a+4]=2;q[6*a+5]=3;
    }
    for(unsigned i=0;i<6*el.count;++i){double f=0;for(unsigned j=0;j<6*el.count;++j)f+=el.K[i][j]*q[j];
        if(std::abs(f)>1e-9)throw std::runtime_error("rigid section force");}
    for(unsigned i=0;i<330;++i)run.advance();run.save(scratch/"section.rst");ModelStore::save(scratch/"section.model",model);
    auto m=ModelStore::load<T>(scratch/"section.model");SectionRun<T> split(m);split.restore(scratch/"section.rst");
    while(run.advance())if(!split.advance())throw std::runtime_error("split cursor");
    double u=.001*(1-std::cos(std::sqrt(200.)*.1));
    if(std::abs(run.state().data().displacement.read()(1,0)-u)>3e-8)throw std::runtime_error("section axial oscillator");
    if(native::pack(run.snapshot())!=native::pack(split.snapshot()))throw std::runtime_error("section restart mismatch");
}
int main(int argc,char**argv){if(argc!=3)return 2;std::filesystem::create_directories(argv[2]);
    for(auto* file:{"section_beam.inp","section_shell.inp"}){test<double>(argv[1],argv[2],file);test<float>(argv[1],argv[2],file);}
    std::cout<<"section beam/shell FP32/64 rigid modes, analytic oscillator, exact restart PASS\\n";}
