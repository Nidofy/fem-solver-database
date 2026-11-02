#pragma once
#include "../model_ir/model_ir.hpp"

namespace sdb::lifecycle::distributed {
using Vec=std::array<double,3>;
inline Vec cross(Vec a,Vec b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline double dot(Vec a,Vec b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
// Trilinear brick, evaluated in the kernel's local node numbering.
inline constexpr int sign[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                                  {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
template<class Mesh,class Add> void integrate_body(const Mesh& mesh, Index e, Vec body, Add add) {
    const auto ns=mesh.connectivity(e); auto x=mesh.reference();
    const auto top=mesh.element(e).topology;
    if(top==Topology::Tet4) {
        Vec dx[3]{};
        for(unsigned a=0;a<3;++a) for(unsigned d=0;d<3;++d) dx[a][d]=double(x(ns[a+1],d))-x(ns[0],d);
        auto weight=dot(dx[0],cross(dx[1],dx[2]))/24;
        if(!(weight>0) || !std::isfinite(weight)) throw std::invalid_argument("invalid DLOAD TET4 volume");
        for(auto n:ns) add(n,body,weight);
    } else if(top==Topology::Hex8) {
        for(auto &q:sign) {
            double N[8]; Vec dx[3]{};
            for(unsigned a=0;a<8;++a) {
                double f[3];for(unsigned i=0;i<3;++i) f[i]=1+sign[a][i]*q[i]/std::sqrt(3.);
                N[a]=f[0]*f[1]*f[2]/8;
                for(unsigned i=0;i<3;++i) for(unsigned d=0;d<3;++d)
                    dx[i][d]+=(double(x(ns[a],d))-x(ns[0],d))*sign[a][i]*f[(i+1)%3]*f[(i+2)%3]/8;
            }
            double det=dot(dx[0],cross(dx[1],dx[2]));
            if(!(det>0) || !std::isfinite(det)) throw std::invalid_argument("invalid DLOAD HEX8 volume");
            for(unsigned a=0;a<8;++a) add(ns[a],body,det*N[a]);
        }
    } else throw std::invalid_argument("body DLOAD requires C3D4/C3D8/C3D8R");
}
template<class Mesh,class Add> void integrate_pressure(const Mesh& mesh,const FaceId& face,double p,Add add) {
    Index e=mesh.element_index(face.element);auto top=mesh.element(e).topology;
    if(top!=Topology::Tet4 && top!=Topology::Hex8) throw std::invalid_argument("pressure requires solid face");
    auto face_nodes=topology_face_nodes(top,face.local); auto ns=mesh.connectivity(e);auto x=mesh.reference();
    auto n=[&](unsigned i){return ns[face_nodes[i]];};
    if(face_nodes.size()==3) {
        Vec a{},b{}; for(unsigned d=0;d<3;++d){a[d]=double(x(n(1),d))-x(n(0),d);b[d]=double(x(n(2),d))-x(n(0),d);}
        auto area=cross(a,b);
        for(unsigned i=0;i<3;++i)add(n(i),area,-p/6);
    } else {
        for(unsigned q=0;q<4;++q) {
            double r=sign[q][0]/std::sqrt(3.),s=sign[q][1]/std::sqrt(3.);
            Vec a{},b{};double N[4];
            for(unsigned i=0;i<4;++i) {
                double u=sign[i][0],v=sign[i][1];N[i]=(1+r*u)*(1+s*v)/4;
                for(unsigned d=0;d<3;++d) {
                    double xx=double(x(n(i),d))-x(n(0),d);
                    a[d]+=xx*u*(1+s*v)/4;b[d]+=xx*v*(1+r*u)/4;
                }
            }
            auto area=cross(a,b);
            for(unsigned i=0;i<4;++i)add(n(i),area,-p*N[i]);
        }
    }
}
// Evaluate the affine rotational acceleration field with consistent shape weights.
template<class Mesh,class Add> void rotation(const Mesh& mesh,Index e,const LoadContribution& l,Add add) {
    const auto ns=mesh.connectivity(e); auto x=mesh.reference(); Vec at[8]{};
    double magnitude=l.scalar;
    if(l.kind==LoadValueKind::Centrifugal || l.kind==LoadValueKind::RotaryAcceleration)
        magnitude*=mesh.materials()[mesh.element(e).material].parameters.at(0);
    for(unsigned a=0;a<ns.size();++a) {
        Vec r;for(unsigned d=0;d<3;++d)r[d]=double(x(ns[a],d))-l.axis_point[d];
        if(l.kind==LoadValueKind::RotaryAcceleration) at[a]=cross(r,l.vector);
        else {
            double axial=dot(r,l.vector);
            for(unsigned d=0;d<3;++d)at[a][d]=r[d]-axial*l.vector[d];
        }
        for(auto& v:at[a])v*=magnitude;
    }
    if(mesh.element(e).topology==Topology::Tet4) {
        Vec total{};for(unsigned a=0;a<4;++a)for(unsigned d=0;d<3;++d)total[d]+=at[a][d];
        unsigned a=0;
        integrate_body(mesh,e,Vec{},[&](Index n,Vec,double w){
            Vec load;for(unsigned d=0;d<3;++d)load[d]=(total[d]+at[a][d])/5;
            add(n,load,w);++a;
        });
    } else if(mesh.element(e).topology==Topology::Hex8) {
        for(auto &q:sign) {
            Vec basis[3]{},load{};double N[8];
            for(unsigned a=0;a<8;++a) {
                Vec f;for(unsigned i=0;i<3;++i)f[i]=1+sign[a][i]*q[i]/std::sqrt(3.);
                N[a]=f[0]*f[1]*f[2]/8;
                for(unsigned d=0;d<3;++d) {
                    load[d]+=N[a]*at[a][d];
                    for(unsigned i=0;i<3;++i)
                        basis[i][d]+=(double(x(ns[a],d))-x(ns[0],d))*sign[a][i]*f[(i+1)%3]*f[(i+2)%3]/8;
                }
            }
            double det=dot(basis[0],cross(basis[1],basis[2]));
            if(!(det>0) || !std::isfinite(det)) throw std::invalid_argument("invalid rotating brick");
            for(unsigned a=0;a<8;++a)add(ns[a],load,N[a]*det);
        }
    } else throw std::invalid_argument("rotational load requires solid");
}
template<class Mesh,class Add> void hydrostatic(const Mesh& mesh,FaceId face,const LoadContribution& l,Add add) {
    Index e=mesh.element_index(face.element);auto ns=mesh.connectivity(e);auto x=mesh.reference();
    auto fn=topology_face_nodes(mesh.element(e).topology,face.local);
    // Parameterize triangle (0<=r<=1,0<=s<=1-r) or bilinear quad ([0,1]^2).
    // At fixed r, elevation is linear in s; clip the actual wet interval before quadrature.
    const bool tri=fn.size()==3;
    if(!tri && fn.size()!=4) throw std::invalid_argument("hydrostatic face topology");
    auto shape=[&](double r,double s) -> std::array<double,4> {
        return tri?std::array<double,4>{1-r-s,r,s,0}:std::array<double,4>{(1-r)*(1-s),r*(1-s),r*s,(1-r)*s};
    };
    auto depth=[&](double r,double s){
        auto N=shape(r,s);double h=0;
        for(unsigned a=0;a<fn.size();++a)h+=N[a]*(l.levels[0]-x(ns[fn[a]],2));
        return h;
    };
    std::vector<double> cuts{0,1};
    for(unsigned side=0;side<2;++side) {
        double d0=depth(0,side),d1=depth(1,tri?0:side);
        if((d0<0 && d1>0)||(d0>0 && d1<0))cuts.push_back(d0/(d0-d1));
    }
    std::sort(cuts.begin(),cuts.end());
    constexpr double g[]={-.8611363115940526,-.3399810435848563,.3399810435848563,.8611363115940526};
    constexpr double w[]={.3478548451374538,.6521451548625461,.6521451548625461,.3478548451374538};
    for(std::size_t k=1;k<cuts.size();++k) for(unsigned i=0;i<4;++i) {
        double dr=(cuts[k]-cuts[k-1])*.5,r=cuts[k-1]+dr*(1+g[i]);
        double lo=0,hi=tri?1-r:1,hl=depth(r,lo),hh=depth(r,hi);
        if(hl<=0 && hh<=0)continue;
        if(hl<0)lo=hi*hl/(hl-hh); else if(hh<0)hi*=hl/(hl-hh);
        for(unsigned j=0;j<2;++j) {
            double ds=(hi-lo)*.5,s=lo+ds*(1+(j?1:-1)/std::sqrt(3.));
            auto N=shape(r,s); auto R=shape(1,s),R0=shape(0,s),S=shape(r,1),S0=shape(r,0);
            Vec a{},b{};
            for(unsigned q=0;q<fn.size();++q)for(unsigned d=0;d<3;++d) {
                double dx=double(x(ns[fn[q]],d))-x(ns[fn[0]],d);
                a[d]+=(R[q]-R0[q])*dx;b[d]+=(S[q]-S0[q])*dx;
            }
            auto area=cross(a,b);
            double pressure=l.scalar*depth(r,s)/(l.levels[0]-l.levels[1]);
            for(unsigned q=0;q<fn.size();++q)add(ns[fn[q]],area,-w[i]*dr*ds*N[q]*pressure);
        }
    }
}
template<class Model> void compile(Model& model) {
    const auto& f=model.foundation;
    for(std::size_t i=0;i<model.analysis.steps.size();++i) {
        auto& actions=model.analysis.steps[i].loads;
        for(const auto& active:f.effective_loads(StepId::checked(i))) for(auto origin:active.contributions) {
            auto v=f.load_contribution(active.id,origin);auto &r=f.load(active.id,origin);
            std::map<Index,Vec> sum;
            auto add=[&](Index n,Vec vec,double w){
                auto& dst=sum[n]; for(unsigned d=0;d<3;++d) dst[d]+=vec[d]*w;
            };
            if(model.definition.finite_geometry && v.kind==LoadValueKind::Pressure) continue;
            if(v.definition.coordinate.value()!=0 || v.definition.follower)
                throw std::invalid_argument("only fixed global DLOAD accepted");
            if(v.kind==LoadValueKind::Pressure) {
                for(auto face:model.mesh.surfaces().at(r.surface.value()).faces)
                    integrate_pressure(model.mesh,face,v.scalar,add);
            } else if(v.kind==LoadValueKind::Acceleration || v.kind==LoadValueKind::ForcePerVolume) {
                for(auto id:r.elements) {
                    auto body=v.vector;
                    if(v.kind==LoadValueKind::Acceleration) {
                        const auto& mat=model.mesh.materials()[model.mesh.element(id.value()).material];
                        for(auto &x:body)x*=mat.parameters.at(0);
                    }
                    integrate_body(model.mesh,id.value(),body,add);
                }
            } else if(v.kind==LoadValueKind::Hydrostatic) {
                for(auto face:model.mesh.surfaces().at(r.surface.value()).faces) hydrostatic(model.mesh,face,v,add);
            } else if(v.kind>=LoadValueKind::Centrifugal && v.kind<=LoadValueKind::Coriolis) {
                for(auto id:r.elements) {
                    if(v.kind==LoadValueKind::Coriolis) {
                        auto axis=v.vector;for(auto& c:axis)c*=-2*v.scalar;
                        integrate_body(model.mesh,id.value(),axis,add);
                    } else rotation(model.mesh,id.value(),v,add);
                }
            } else throw std::invalid_argument("unsupported DLOAD kind");
            auto& out=v.kind==LoadValueKind::Coriolis?model.analysis.steps[i].coriolis:actions;
            for(auto &[n,vec]:sum) for(std::uint8_t d=0;d<3;++d) if(vec[d]!=0) {
                if(!std::isfinite(vec[d])) throw std::overflow_error("distributed nodal force overflow");
                out.push_back({n,d,vec[d],v.definition.amplitude,origin.value()});
            }
        }
    }
}
}
