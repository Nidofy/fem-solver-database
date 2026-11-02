#pragma once
#include "../model_compiler/compiler.hpp"
namespace sdb::physics {
using Vec3=std::array<double,3>;
inline Vec3 difference(Vec3 a,Vec3 b){for(unsigned i=0;i<3;++i)a[i]-=b[i];return a;}
inline double magnitude(Vec3 a){return std::hypot(a[0],a[1],a[2]);}
inline Vec3 unit(Vec3 a){double l=magnitude(a);if(!(l>0)||!std::isfinite(l))throw std::invalid_argument("degenerate section basis");for(auto& v:a)v/=l;return a;}

// Small-displacement 6-DOF stiffness, assembled from work-conjugate strain rows.
class SectionElement {
    using Row=std::array<double,24>;
    std::array<Vec3,3> axes_;
    void mode(double stiffness,const Row& local){
        Row g{};
        for(unsigned a=0;a<count;++a)for(unsigned kind=0;kind<2;++kind)
            for(unsigned d=0;d<3;++d)for(unsigned j=0;j<3;++j)
                g[6*a+3*kind+j]+=local[6*a+3*kind+d]*axes_[d][j];
        for(unsigned i=0;i<6*count;++i) for(unsigned j=0;j<6*count;++j) K[i][j]+=stiffness*g[i]*g[j];
    }
  public:
    unsigned count=0;
    std::array<Index,4> nodes{};
    std::array<Row,24> K{};
    double nodal_mass=0, rotary_mass=0;
    double squared_frequency_bound() const {
        double bound=0;for(unsigned i=0;i<count*6;++i) {
            double r=0;for(unsigned j=0;j<count*6;++j)r+=std::abs(K[i][j]);
            if(!std::isfinite(r)) throw std::overflow_error("section stiffness overflow");
            bound=std::max(bound,r/(i%6<3?nodal_mass:rotary_mass));
        }
        if(!(bound>0)||!std::isfinite(bound))throw std::invalid_argument("invalid section stability bound");
        return bound;
    }
    template<class T> SectionElement(const lifecycle::CompiledModel<T>& m,Index e,const lifecycle::SectionRecord& section) {
        using lifecycle::distributed::cross; using lifecycle::distributed::dot;
        auto ns=m.mesh.connectivity(e);const auto& descriptor=m.descriptors.at(e);
        auto& mat=m.mesh.materials()[m.mesh.element(e).material];const auto& p=section.properties;
        if(mat.model!=1 || !mat.history_schema.empty())throw std::invalid_argument("sections require isotropic elasticity");
        double rho=mat.parameters[0],E=mat.parameters[1],nu=mat.parameters[2],G=E/(2*(1+nu));
        count=unsigned(ns.size());if(count!=2 && count!=4)throw std::invalid_argument("invalid section topology");
        std::array<Vec3,4> x{};auto ref=m.mesh.reference();for(unsigned a=0;a<count;++a){
            nodes[a]=ns[a];for(unsigned d=0;d<3;++d)x[a][d]=ref(ns[a],d);
        }
        auto edge=difference(x[1],x[0]);double L=magnitude(edge); axes_[0]=unit(edge);
        if(descriptor.token=="B31" && count==2 && section.kind=="BEAM SECTION/RECT") {
            if(p.size()!=5 || !(p[0]>0 && p[1]>0))throw std::invalid_argument("B31 RECT needs two dimensions and an orientation vector");
            axes_[2]=unit(cross(axes_[0],{p[2],p[3],p[4]}));axes_[1]=cross(axes_[2],axes_[0]);
            double A=p[0]*p[1],Iy=A*p[1]*p[1]/12,Iz=A*p[0]*p[0]/12;
            double w=std::max(p[0],p[1]),h=std::min(p[0],p[1]);
            double J=w*h*h*h*(1./3-.21*h/w*(1-std::pow(h/w,4)/12)); // rectangular torsion approximation
            nodal_mass=rho*A*L/2;rotary_mass=rho*(Iy+Iz)*L/2;
            for(unsigned d=0;d<6;++d) {
                Row r{};r[d]=-1/L;r[6+d]=1/L;
                if(d==1)r[5]=r[11]=-.5;
                if(d==2)r[4]=r[10]=.5;
                const double moduli[]={E*A,5.*G*A/6,5.*G*A/6,G*J,E*Iy,E*Iz};
                mode(L*moduli[d],r);
            }
        } else if(descriptor.token=="S4R" && count==4 && section.kind=="SHELL SECTION") {
            if(p.empty() || !(p[0]>0))throw std::invalid_argument("invalid shell thickness");
            auto side=difference(x[3],x[0]);double H=magnitude(side);axes_[2]=unit(cross(edge,side));axes_[1]=cross(axes_[2],axes_[0]);
            auto diagonal=difference(difference(x[2],x[1]),side);
            double tol=64*std::numeric_limits<T>::epsilon();
            if(std::abs(dot(axes_[0],unit(side)))>tol || magnitude(diagonal)>tol*std::max(L,H))
                throw std::invalid_argument("S4R reference kernel requires a planar rectangle");
            double t=p[0],area=L*H;nodal_mass=rho*t*area/4;rotary_mass=nodal_mass*t*t/12;
            constexpr int sx[]={-1,1,1,-1},sy[]={-1,-1,1,1};
            Row ex{},ey{},xy{},kx{},ky{},kxy{},xz{},yz{},drill{};
            for(unsigned a=0;a<4;++a) {
                unsigned i=6*a;double dx=sx[a]/(2*L),dy=sy[a]/(2*H);
                ex[i]=dx;ey[i+1]=dy;xy[i]=dy;xy[i+1]=dx;
                kx[i+4]=dx;ky[i+3]=-dy;kxy[i+4]=dy;kxy[i+3]=-dx;
                xz[i+2]=dx;xz[i+4]=.25;yz[i+2]=dy;yz[i+3]=-.25;
                drill[i+5]=.25;drill[i]=.5*dy;drill[i+1]=-.5*dx;
            }
            auto plane=[&](Row a,Row b,Row shear,double v){
                Row sum{},diff{};for(unsigned i=0;i<24;++i){sum[i]=a[i]+b[i];diff[i]=a[i]-b[i];}
                mode(v*E/(2*(1-nu)),sum);mode(v*G,diff);mode(v*G,shear);
            };
            plane(ex,ey,xy,area*t);plane(kx,ky,kxy,area*t*t*t/12);
            mode(area*t*G*5/6,xz);mode(area*t*G*5/6,yz); mode(.01*area*t*G,drill);
            // Affine-orthogonal checkerboard modes. Drilling rotation differences
            // also constrain the three zero-energy variations of nodal drill.
            for(unsigned d=0;d<5;++d){
                Row r{};for(unsigned a=0;a<4;++a) r[6*a+d]=sx[a]*sy[a]/4.;
                mode(.1*G*t*(d<3?1:t*t/12),r);
            }
            for(unsigned a=1;a<4;++a){Row r{};r[5]=-1;r[6*a+5]=1;mode(.001*area*t*G,r);}
        } else throw std::invalid_argument("section backend supports S4R or B31 RECT only");
        if(!(nodal_mass>0 && rotary_mass>0) || !std::isfinite(nodal_mass) || !std::isfinite(rotary_mass))
            throw std::invalid_argument("invalid lumped sectional inertia");
    }
};
}
            // Opposite-edge midpoint transverse shears define the two linear
            // variations omitted by the center point. Their squared integral is A/3.
            Row xz_variation{}, yz_variation{};
            for(unsigned a=0;a<4;++a){
                xz_variation[6*a+2]=sx[a]*sy[a]/(2*L);
                xz_variation[6*a+4]=sy[a]/4.;
                yz_variation[6*a+2]=sx[a]*sy[a]/(2*H);
                yz_variation[6*a+3]=-sx[a]/4.;
            }
            mode(area*t*G*5/18,xz_variation);
            mode(area*t*G*5/18,yz_variation);
