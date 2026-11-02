#pragma once
#include "../model.hpp"

namespace sdb::physics {
// Compressible neo-Hookean energy per reference volume, updated spatial gradients.
// Output E is spatial Euler-Almansi strain and S is Cauchy stress.
struct FiniteTetPoint {
    std::array<double,12> force{};
    std::array<double,6> stress{},strain{};
    double energy=0, max_frequency_squared=0;
};
template<class T,class U> FiniteTetPoint finite_tet(std::size_t e, View<const T,12> g,
        double volume,double lambda,double mu,double lumped_mass,U u) {
    double F[3][3]={{1,0,0},{0,1,0},{0,0,1}},invT[3][3];
    for(unsigned a=0;a<4;++a) for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
        F[i][j]+=(double(u(a,i))-u(0,i))*g(e,3*a+j);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
        invT[i][j]=F[(i+1)%3][(j+1)%3]*F[(i+2)%3][(j+2)%3]-F[(i+1)%3][(j+2)%3]*F[(i+2)%3][(j+1)%3];
    double J=0;for(unsigned j=0;j<3;++j) J+=F[0][j]*invT[0][j];
    if(!(J>0) || !std::isfinite(J)) throw std::domain_error("inverted/nonfinite current TET4 Jacobian");
    for(auto &row:invT)for(auto &x:row)x/=J;
    const double logJ=std::log(J),q=lambda*logJ-mu;
    FiniteTetPoint out;double spatial[4][3]{}, sigma[3][3]{};
    for(unsigned a=0;a<4;++a) for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
        spatial[a][i]+=invT[i][j]*g(e,3*a+j);
    constexpr unsigned ij[6][2]={{0,0},{1,1},{2,2},{0,1},{1,2},{2,0}};
    double trB=0;
    for(unsigned k=0;k<6;++k) {
        auto i=ij[k][0],j=ij[k][1]; double b=0,b_inv=0;
        for(unsigned l=0;l<3;++l){b+=F[i][l]*F[j][l];b_inv+=invT[i][l]*invT[j][l];}
        if(i==j)trB+=b;
        out.stress[k]=sigma[i][j]=sigma[j][i]=(mu*b+(i==j?q:0))/J;
        out.strain[k]=.5*((i==j?1.:0.)-b_inv);
    }
    out.energy=volume*(.5*mu*(trB-3)-mu*logJ+.5*lambda*logJ*logJ);
    for(unsigned a=0;a<4;++a) for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j) out.force[3*a+i]+=J*volume*sigma[i][j]*spatial[a][j];
        double row=0;
        for(unsigned b=0;b<4;++b) {
            double dot=0;for(unsigned k=0;k<3;++k)dot+=double(g(e,3*a+k))*g(e,3*b+k);
            for(unsigned j=0;j<3;++j)
                row+=std::abs(volume*((i==j?mu*dot:0)+lambda*spatial[a][i]*spatial[b][j]-q*spatial[a][j]*spatial[b][i]));
        }
        out.max_frequency_squared=std::max(out.max_frequency_squared,row/lumped_mass);
    }
    if(!std::isfinite(out.energy) || !std::isfinite(out.max_frequency_squared))
        throw std::overflow_error("nonfinite finite TET4 state");
    return out;
}
}
