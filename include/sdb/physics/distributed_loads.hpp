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
            } else throw std::invalid_argument("unsupported DLOAD kind");
            for(auto &[n,vec]:sum) for(std::uint8_t d=0;d<3;++d) if(vec[d]!=0) {
                if(!std::isfinite(vec[d])) throw std::overflow_error("distributed nodal force overflow");
                actions.push_back({n,d,vec[d],v.definition.amplitude,origin.value()});
            }
        }
    }
}
}
