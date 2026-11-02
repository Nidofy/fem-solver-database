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
void mixed_energy() {
    auto mesh=oscillator_model<double>();
    StateDB<double> s(mesh);
    CpuExplicit<double> cpu(mesh,s,CpuPolicy::Serial,Assembly::Deterministic,{},TetKernel::Mixed8);
    const auto n=mesh.node_index(40);
    s.data().displacement.write()(n,0)=.01;
    constexpr double h=.0001, stiffness=100./6, mass=1./24;
    const double initial=.5*stiffness*.01*.01;
    cpu.initialize(s,h);
    double peak=0;
    for(unsigned i=0;i<2000;++i) {
        cpu.step(s);
        const auto& derived=cpu.output_forces(s);
        double u=s.data().displacement.read()(n,0);
        double v=s.data().velocity.read()(n,0)-.5*h*derived.internal_force.read()(n,0)/mass;
        peak=std::max(peak,std::abs(.5*(stiffness*u*u+mass*v*v)/initial-1));
    }
    if(!(peak<3e-6)) throw std::runtime_error("mixed oscillator energy drift");
    std::cout<<"mixed peak relative energy drift="<<peak<<std::endl;
}
int main() {
    try {
        mixed_energy();
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
