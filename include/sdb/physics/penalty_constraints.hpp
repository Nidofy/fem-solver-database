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
                if (d>=3) throw std::invalid_argument("rotational equation needs rotary inertia");
                row.terms.push_back({nodes.at(i).value(),d,term.coefficient});
            }
            rows_.push_back(std::move(row));
        }
        auto coupling=[&](NodeId ref,const std::vector<NodeId>& nodes,unsigned mask,
                          const std::vector<double>& weights) {
            if (!mask || mask>7) throw std::invalid_argument("only global translations supported");
            for(unsigned d=0;d<3;++d) if(mask&(1u<<d)) {
                if (weights.empty()) {
                    for(auto n:nodes) rows_.push_back({0,{{ref.value(),d,1},{n.value(),d,-1}}});
                } else {
                    Row row{0,{{ref.value(),d,1}}};
                    for(std::size_t i=0;i<nodes.size();++i)
                        row.terms.push_back({nodes[i].value(),d,-weights.at(i)});
                    rows_.push_back(std::move(row));
                }
            }
        };
        for(const auto& r:f.definition.rigids) {
            const auto& resolved=f.rigid(r.id);
            coupling(resolved.reference,resolved.nodes,r.dof_mask,{});
        }
        for(const auto& c:f.extensions.couplings) {
            if (c.coordinate.value()!=0) throw std::invalid_argument("local coupling basis unsupported");
            const auto& r=f.coupling(c.id);
            coupling(r.reference,r.nodes,c.dof_mask,r.weights);
        }
    }
    std::size_t rows() const noexcept { return rows_.size(); }
    template<class T> struct Result {
        Field<T,3> restoring_force;
        double potential=0, max_gap=0, frequency_squared_bound=0;
    };
    // Recompute on displacement/mass changes. Finite supports may omit fixed DOFs
    // in their integrator; using all positive masses here is conservative.
    template<class T> Result<T> evaluate(const StateDB<T>& s) const {
        if(s.model_id()!=model_) throw std::invalid_argument("penalty model mismatch");
        s.validate_shape();
        auto u=s.data().displacement.read(), mass=s.data().mass.read();
        Result<T> out{Field<T,3>(u.count)};
        Field<double,3> sums(u.count);
        auto f=out.restoring_force.write(); auto k=sums.write();
        for(const auto& r:rows_) {
            double gap=-r.rhs, l1=0;
            for(auto t:r.terms) {
                gap+=t.weight*u(t.node,t.dof); l1+=std::abs(t.weight);
                if(!(mass(t.node)>0) || !std::isfinite(mass(t.node)))
                    throw std::domain_error("penalty requires finite positive participating mass");
            }
            out.max_gap=std::max(out.max_gap,std::abs(gap));
            out.potential+=.5*stiffness_*gap*gap;
            for(auto t:r.terms) {
                double value=f(t.node,t.dof)-stiffness_*gap*t.weight;
                if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<T>::max())
                    throw std::overflow_error("penalty force overflow");
                f(t.node,t.dof)=static_cast<T>(value);
                k(t.node,t.dof)+=stiffness_*std::abs(t.weight)*l1/mass(t.node);
                out.frequency_squared_bound=std::max(out.frequency_squared_bound,k(t.node,t.dof));
            }
        }
        if(!std::isfinite(out.potential) || !std::isfinite(out.frequency_squared_bound))
            throw std::overflow_error("penalty energy/stiffness overflow");
        return out;
    }
};
}
