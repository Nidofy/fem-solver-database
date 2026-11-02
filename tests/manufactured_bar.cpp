#include <sdb/dynamics.hpp>
#include <array>
#include <iostream>
using namespace sdb;

// u=x(1-x)t^2, E=100, rho=1, nu=0. Dirichlet ends and zero transverse motion.
// rho*u_tt - E*u_xx = 2*x*(1-x) + 200*t^2, using lumped body-load quadrature.
std::array<double,2> error(unsigned cells,unsigned ips) {
    ModelBuilder<double> b;
    b.add_material({1,1,{1,100,0},0}); b.add_part(1);
    for(unsigned i=0;i<=cells;++i) for(unsigned q=0;q<4;++q) {
        Id n=4*i+q+1;
        b.add_node(n,{double(i)/cells,double(q%2),double(q/2)});
        b.constrain(n,1); b.constrain(n,2);
        if(i==0 || i==cells) b.constrain(n,0);
    }
    for(unsigned i=0;i<cells;++i) {
        Id l=4*i+1,r=l+4;
        b.add_element(i+1,Topology::Hex8,{l,r,r+1,l+1,l+2,r+2,r+3,l+3},1,1,ips);
    }
    auto mesh=b.finalize(); StateDB<double> s(mesh); CpuExplicit<double> cpu(mesh,s);
    auto load=[&](double t) {
        for(Index n=0;n<mesh.nodes();++n) {
            auto x=mesh.reference()(n,0);
            s.data().external_force.write()(n,0)=s.data().mass.read()(n)*(2*x*(1-x)+200*t*t);
        }
    };
    constexpr double dt=.0001,t=.05;
    load(0);cpu.initialize(s,dt);
    for(unsigned i=0;i<500;++i) {load(i*dt);cpu.step(s);}
    cpu.forces(s);
    std::array<double,2> squared{};
    // Integrate squared displacement and strain error with three-point Gauss.
    for(unsigned i=0;i<cells;++i) {
        const auto left=s.data().displacement.read()(4*i,0),right=s.data().displacement.read()(4*i+4,0);
        for(int q=-1;q<=1;++q) {
            double r=(1+q*std::sqrt(.6))/2,x=(i+r)/cells,w=(q==0?4./9:5./18)/cells;
            double u=(1-r)*left+r*right, ex=s.data().strain.read()(i*ips,0);
            squared[0]+=w*std::pow(u-t*t*x*(1-x),2);
            squared[1]+=w*std::pow(ex-t*t*(1-2*x),2);
        }
    }
    return {std::sqrt(squared[0]),std::sqrt(squared[1])};
}
int main() {
    for(unsigned ips:{1u,8u}) {
        auto previous=error(2,ips);
        for(unsigned n:{4u,8u,16u}) {
            auto current=error(n,ips);
            for(unsigned c=0;c<2;++c) {
                double order=std::log2(previous[c]/current[c]);
                std::cout<<"ips="<<ips<<" cells="<<n<<" component="<<c<<" error="<<current[c]<<" order="<<order<<'\n';
                if(!(std::abs(order-(2-c))<.05)) return 1;
            }
            previous=current;
        }
    }
}
