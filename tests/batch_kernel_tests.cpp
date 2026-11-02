#include <sdb/dynamics.hpp>
#include <iostream>
using namespace sdb;

template<class T> MeshDB<T> make_mesh(CpuPolicy policy) {
    constexpr Index cells=513;
    ModelBuilder<T> b;
    b.add_material({1,1,{T(2),T(123),T(.27)},0});
    b.add_part(1);
    for(Index i=0;i<cells;++i) {
        T x=T(2*i);
        b.add_node(4*i,{x,0,0});
        b.add_node(4*i+1,{x+1,T(.125),0});
        b.add_node(4*i+2,{x,T(.875),T(.125)});
        b.add_node(4*i+3,{x,0,1});
    }
    for(Index i=0;i<cells;++i) {
        Id j=4*((i*197)%cells);
        b.add_element(1+i,Topology::Tet4,{j,j+1,j+2,j+3},1,1);
    }
    return b.finalize(policy);
}
template<class T> void compare(CpuPolicy policy,TetKernel kernel) {
    auto mesh=make_mesh<T>(policy), serial_mesh=make_mesh<T>(CpuPolicy::Serial);
    if(mesh.fingerprint()!=serial_mesh.fingerprint()) throw std::runtime_error("setup fingerprint");
    StateDB<T> base(mesh),test(mesh);
    CpuExplicit<T> scalar(mesh,base), batched(mesh,test,policy,Assembly::Deterministic,{},kernel);
    for(Index e=0;e<mesh.elements();++e)
        base.data().element_active.write()(e)=test.data().element_active.write()(e)=e%5!=0;
    for(Index n=0;n<mesh.nodes();++n) for(unsigned c=0;c<3;++c)
        base.data().displacement.write()(n,c)=test.data().displacement.write()(n,c)=T(.001*std::sin(3*n+c));
    auto equal=[&](const auto& a,const auto& b){
        constexpr unsigned components=std::decay_t<decltype(a)>::components;
        for(std::size_t n=0;n<a.size();++n) for(unsigned c=0;c<components;++c) {
            double x=a.read()(n,c), y=b.read()(n,c);
            if(!(std::abs(x-y) <= (kernel==TetKernel::Mixed8 ? 2e-7 : 1e-13)))
                throw std::runtime_error("batch/scalar discrepancy");
        }
    };
    scalar.initialize(base,T(.0002)); batched.initialize(test,T(.0002));
    for(unsigned k=0;k<36;++k) { scalar.step(base); batched.step(test); }
    equal(base.data().internal_force,test.data().internal_force);
    equal(base.data().stress,test.data().stress); equal(base.data().strain,test.data().strain);
    equal(base.data().displacement,test.data().displacement);
    if(kernel==TetKernel::Mixed8 && !(batched.mixed_peak_error()>0 && batched.mixed_peak_error()<1.e-4))
        throw std::runtime_error("mixed precision audit");
}
int main() {
    try {
        for(auto policy:{CpuPolicy::Serial
#ifdef _OPENMP
            ,CpuPolicy::OpenMP
#endif
        }) {
            for(auto mode:{TetKernel::Batch8,TetKernel::Tiled8}) {compare<float>(policy,mode);compare<double>(policy,mode);}
            compare<double>(policy,TetKernel::Mixed8);
        }
        std::cout<<"Batch force, stress, strain, displacement, fingerprint and mixed drift PASS\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
