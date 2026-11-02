#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;using namespace sdb::lifecycle;
void check(bool ok){if(!ok)throw std::runtime_error("finite tetrahedron contract");}
template<class T> void test(const std::filesystem::path& inp,const std::filesystem::path& dir){
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(inp).model);
    auto x=model.mesh.reference();StateDB<T> st(model.mesh);
    CpuExplicit<T> cpu(model.mesh,st,CpuPolicy::Serial,Assembly::Deterministic,{},TetKernel::Scalar,true);
    // Proper 90-degree rotation plus translation. Linearized strain is not zero here.
    auto u=st.data().displacement.write();
    for(Index i=0;i<model.mesh.nodes();++i){u(i,0)=-x(i,1)-x(i,0)+2;u(i,1)=x(i,0)-x(i,1);u(i,2)=3;}
    cpu.forces(st);
    for(std::size_t i=0;i<st.data().stress.size();++i)for(unsigned c=0;c<6;++c)
        check(st.data().stress.read()(i,c)==0 && st.data().strain.read()(i,c)==0);
    for(Index i=0;i<model.mesh.nodes();++i){u(i,0)=T(.2)*x(i,0);u(i,1)=u(i,2)=0;}
    cpu.forces(st);
    double sigma=50*(1.2*1.2-1)/1.2; // lambda=0, mu=50.
    check(std::abs(st.data().stress.read()(0,0)-sigma)<1e-4);
    auto mesh=ModelStore::identity(model); (void)mesh;
    CpuRun<T> run(model);run.initialize();for(unsigned i=0;i<173;++i)run.advance();
    run.save(dir/"finite.rst"); ModelStore::save(dir/"finite.model",model);
    auto copy=ModelStore::load<T>(dir/"finite.model"); CpuRun<T> split(copy);split.restore(dir/"finite.rst");
    while(run.advance())check(split.advance());check(native::pack(run.snapshot())==native::pack(split.snapshot()));
    // Face 1 on the first TET is at z=0. Stretching x doubles its current area.
    CpuRun<T> pressure(model);auto w=pressure.state().data().displacement.write();
    w(model.sources.node_id({"","",40}).value(),0)=1;
    pressure.initialize();double f=0;for(Index n=0;n<model.mesh.nodes();++n) f+=pressure.state().data().external_force.read()(n,2);
    check(std::abs(f-1)<1e-6);
}
int main(int argc,char**argv){if(argc!=3)return 2;std::filesystem::create_directories(argv[2]);
    test<float>(argv[1],argv[2]);test<double>(argv[1],argv[2]);std::cout<<"finite TET4 PASS\\n";}
