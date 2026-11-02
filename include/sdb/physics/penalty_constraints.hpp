#pragma once
#include "../model_compiler/compiler.hpp"

namespace sdb::physics {
// Translation-space penalty equations. Coefficients are in the compiled model basis.
class PenaltyConstraints {
    struct Term { Index node; unsigned dof; double weight; };
    struct Row { double rhs; std::vector<Term> terms; };
    std::vector<Row> rows_;
    std::uint64_t model_;
    double stiffness_;
  public:
    template<class T>
    PenaltyConstraints(const lifecycle::CompiledModel<T>& model, double stiffness)
        : model_(model.mesh.fingerprint()), stiffness_(stiffness) {
        if (!(stiffness>0) || !std::isfinite(stiffness))
            throw std::invalid_argument("constraint penalty must be positive and finite");
        const auto& f=model.foundation;
        if (!f.definition.ties.empty()) throw std::invalid_argument("tie surfaces require a pairing plan");
        for (const auto& eq:f.definition.equations) {
            Row row{eq.rhs,{}};
            const auto& nodes=f.equation(eq.id).nodes;
            for (std::size_t i=0;i<eq.terms.size();++i) {
                const auto& term=eq.terms[i];
                auto d=static_cast<unsigned>(term.dof);
                if(d>=model.mesh.dofs(nodes.at(i).value())) throw std::invalid_argument("missing equation DOF");
                row.terms.push_back({nodes.at(i).value(),d,term.coefficient});
            }
            rows_.push_back(std::move(row));
        }
        auto coupling=[&](NodeId ref,const std::vector<NodeId>& nodes,unsigned mask,
                          const std::vector<double>& weights,bool surface_fit=false) {
            if (!mask || mask>63) throw std::invalid_argument("invalid rigid DOF mask");
            auto x=model.mesh.reference();using Vec=std::array<double,3>;
            using lifecycle::distributed::cross;using lifecycle::distributed::dot;
            const bool rotary=model.mesh.dofs(ref.value())==6;
            std::vector<Vec> arm(nodes.size());Vec centroid{};
            for(std::size_t i=0;i<nodes.size();++i)for(unsigned c=0;c<3;++c){
                arm[i][c]=double(x(nodes[i].value(),c))-x(ref.value(),c);
                if(!weights.empty()) centroid[c]+=weights.at(i)*arm[i][c];
            }
            for(unsigned d=0;d<6;++d) if(mask&(1u<<d)) {
                if(weights.empty()){
                    for(std::size_t i=0;i<nodes.size();++i){
                        Row row{0,{{ref.value(),d,1},{nodes[i].value(),d,-1}}};
                        if(d<3 && rotary) {
                            Vec axis{};axis[d]=1;
                            auto lever=cross(arm[i],axis);
                            for(unsigned c=0;c<3;++c)if(lever[c]!=0)row.terms.push_back({ref.value(),3+c,lever[c]});
                        }
                        rows_.push_back(std::move(row));
                    }
                    continue;
                }
                Row row{0,{{ref.value(),d,1}}};
                Vec target{}, axis{};axis[d%3]=1;
                if(d>=3)target=axis;
                else if(rotary || surface_fit) {target=cross(axis,centroid);}
                Vec dual{};
                if(dot(target,target)>0) {
                    // Weighted rigid fit about the centroid: I omega = sum w r x u.
                    Vec inertia[3]{};
                    for(std::size_t i=0;i<nodes.size();++i){
                        Vec r=arm[i];for(unsigned c=0;c<3;++c)r[c]-=centroid[c];
                        for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)
                            inertia[a][b]+=weights[i]*((a==b?dot(r,r):0)-r[a]*r[b]);
                    }
                    double scale=std::max({inertia[0][0],inertia[1][1],inertia[2][2]});
                    if(!(scale>0)) throw std::invalid_argument("singular distributing cloud");
                    for(auto& v:inertia)for(auto& t:v)t/=scale;
                    auto c0=cross(inertia[1],inertia[2]),c1=cross(inertia[2],inertia[0]),c2=cross(inertia[0],inertia[1]);
                    double det=dot(inertia[0],c0);
                    if(!(det>128*std::numeric_limits<double>::epsilon()))
                        throw std::invalid_argument("distributing cloud does not resolve requested moment");
                    dual={dot(c0,target)/(det*scale),dot(c1,target)/(det*scale),dot(c2,target)/(det*scale)};
                }
                for(std::size_t i=0;i<nodes.size();++i){
                    Vec r=arm[i];for(unsigned c=0;c<3;++c)r[c]-=centroid[c];
                    auto lever=cross(dual,r);
                    for(unsigned c=0;c<3;++c) {
                        double value=-weights[i]*((d==c?1.:0.)+lever[c]);
                        if(value!=0)row.terms.push_back({nodes[i].value(),c,value});
                    }
                }
                rows_.push_back(std::move(row));
            }
        };
        for(const auto& r:f.definition.rigids) {
            const auto& resolved=f.rigid(r.id);
            coupling(resolved.reference,resolved.nodes,r.dof_mask,{});
        }
        for(const auto& c:f.extensions.couplings) {
            if (c.coordinate.value()!=0) throw std::invalid_argument("local coupling basis unsupported");
            const auto& r=f.coupling(c.id);
            coupling(r.reference,r.nodes,c.dof_mask,r.weights,c.kind==lifecycle::CouplingKind::SurfaceDistributing);
        }
    }
    std::size_t rows() const noexcept { return rows_.size(); }
    template<class T> struct Result {
        Field<T,3> restoring_force, restoring_moment;
        double potential=0, max_gap=0, frequency_squared_bound=0;
    };
    // Recompute on displacement/mass changes. Finite supports may omit fixed DOFs
    // in their integrator; using all positive masses here is conservative.
    template<class T> Result<T> evaluate(const StateDB<T>& s) const {
        if(s.model_id()!=model_) throw std::invalid_argument("penalty model mismatch");
        s.validate_shape();
        auto u=s.data().displacement.read();
        auto mass=s.data().mass.read();
        Result<T> out{Field<T,3>(u.count),Field<T,3>(u.count)};
        Field<double,6> sums(u.count);
        auto f=out.restoring_force.write();auto m=out.restoring_moment.write();auto k=sums.write();
        auto& rotation=s.data().rotation;
        auto inertia=[&](Term t)->double {return t.dof<3?mass(t.node):rotation->inertia.read()(t.node,t.dof-3);};
        for(const auto& r:rows_) {
            double gap=-r.rhs, l1=0;
            for(auto t:r.terms) {
                if(t.dof>=3 && !rotation) throw std::invalid_argument("constraint requires rotation state");
                gap+=t.weight*(t.dof<3?u(t.node,t.dof):rotation->angle.read()(t.node,t.dof-3)); l1+=std::abs(t.weight);
                if(!(inertia(t)>0) || !std::isfinite(inertia(t)))
                    throw std::domain_error("penalty requires finite positive participating mass");
            }
            out.max_gap=std::max(out.max_gap,std::abs(gap));
            out.potential+=.5*stiffness_*gap*gap;
            for(auto t:r.terms) {
                auto& lane=t.dof<3?f(t.node,t.dof):m(t.node,t.dof-3);
                double value=lane-stiffness_*gap*t.weight;
                if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<T>::max())
                    throw std::overflow_error("penalty force overflow");
                lane=static_cast<T>(value);
                k(t.node,t.dof)+=stiffness_*std::abs(t.weight)*l1/inertia(t);
                out.frequency_squared_bound=std::max(out.frequency_squared_bound,k(t.node,t.dof));
            }
        }
        if(!std::isfinite(out.potential) || !std::isfinite(out.frequency_squared_bound))
            throw std::overflow_error("penalty energy/stiffness overflow");
        return out;
    }
};
}
