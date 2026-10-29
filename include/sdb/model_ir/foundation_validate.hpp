#pragma once
#include "model_ir.hpp"

namespace sdb::lifecycle {
inline Rotation multiply_rotation(const Rotation &a,const Rotation &b) {
    Rotation out{};
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) for(unsigned k=0;k<3;++k)
        out[i*3+j]+=a[i*3+k]*b[k*3+j];
    return out;
}
inline void validate_rotation(const Rotation &r,const Source &s) {
    for(double x:r) if(!std::isfinite(x)) fail(s,"nonfinite rotation");
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        double dot=0; for(unsigned k=0;k<3;++k) dot+=r[k*3+i]*r[k*3+j];
        if(std::abs(dot-(i==j?1:0))>1e-10) fail(s,"rotation must be orthonormal");
    }
    double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    if(std::abs(det-1)>1e-10) fail(s,"rotation must be right handed");
}
template<class T> void foundation_real(double x,const Source &s) {
    if(!std::isfinite(x) || std::abs(x)>double(std::numeric_limits<T>::max()))
        fail(s,"foundation value exceeds finite model precision");
}
inline std::uint64_t foundation_product(std::uint64_t a,std::uint64_t b,const Source &s) {
    if(!b || a>std::numeric_limits<std::uint32_t>::max()/b) fail(s,"foundation shape/count overflow");
    return a*b;
}
template<class T> CompiledFoundation validate_foundation(const ModelFoundation &f) {
    CompiledFoundation out;
    out.definition=f;
    std::map<CoordinateId,const IRCoordinate*> coordinates;
    std::set<std::string> names;
    for(auto &c:f.coordinates) {
        if(!c.id.valid() || c.id.value()==0 || !c.parent.valid() || c.name.empty() ||
           !coordinates.emplace(c.id,&c).second || !names.insert(c.name).second)
            fail(c.source,"invalid/duplicate coordinate ID/name; zero is global");
        validate_rotation(c.rotation,c.source);
        for(double v:c.origin) foundation_real<T>(v,c.source);
    }
    std::set<CoordinateId> active,done{CoordinateId(0)};
    std::function<void(CoordinateId)> resolve = [&](CoordinateId id) {
        if(done.contains(id)) return;
        auto it=coordinates.find(id);
        if(it==coordinates.end()) throw std::invalid_argument("unknown coordinate parent");
        auto &c=*it->second;
        if(!active.insert(id).second) fail(c.source,"coordinate parent cycle");
        if(active.size()>256) fail(c.source,"coordinate parent depth budget");
        resolve(c.parent);
        auto parent=out.coordinate(c.parent);
        ResolvedCoordinate r{id,parent.point(c.origin),multiply_rotation(parent.rotation,c.rotation)};
        for(double x:r.origin) foundation_real<T>(x,c.source);
        out.coordinates.push_back(r);
        active.erase(id); done.insert(id);
    };
    for(auto &[id,c]:coordinates) { (void)c; resolve(id); }
    names.clear();
    for(auto &o:f.orientations) {
        if(!o.id.valid() || o.name.empty() || !names.insert(o.name).second || out.orientation_bases.contains(o.id))
            fail(o.source,"invalid/duplicate orientation ID/name");
        validate_rotation(o.rotation,o.source);
        out.orientation_bases.emplace(o.id,multiply_rotation(out.coordinate(o.coordinate).rotation,o.rotation));
    }
    names.clear();
    for(auto &m:f.material_definitions) {
        if(m.material.empty() || m.schema_id.empty() || !m.version || !names.insert(m.material).second)
            fail(m.source,"invalid/duplicate typed material schema");
        std::set<std::string> properties;
        for(auto &p:m.parameters) {
            if(p.name.empty() || p.units.empty() || p.shape.empty() || !properties.insert(p.name).second)
                fail(m.source,"empty/duplicate material parameter");
            std::uint64_t count=1;
            for(auto n:p.shape) count=foundation_product(count,n,m.source);
            if(!std::isfinite(p.minimum) || !std::isfinite(p.maximum) || (p.bounded && p.minimum>p.maximum))
                fail(m.source,"invalid parameter bounds");
            const bool integer_scalar=p.scalar!=ScalarType::Float32 && p.scalar!=ScalarType::Float64;
            if(p.bounded && integer_scalar && (std::abs(p.minimum)>9007199254740992.0 || std::abs(p.maximum)>9007199254740992.0))
                fail(m.source,"integer parameter bounds exceed exact binary64 range");
            auto bounds=[&](auto x) {
                if constexpr(std::is_integral_v<decltype(x)>) {
                    if(p.bounded) {
                        if constexpr(std::is_signed_v<decltype(x)>) {
                            if(x < -INT64_C(9007199254740992) || x > INT64_C(9007199254740992)) fail(m.source,"bounded integer exceeds exact binary64 range");
                        } else if(x>UINT64_C(9007199254740992)) fail(m.source,"bounded integer exceeds exact binary64 range");
                    }
                }
                if(p.bounded && (static_cast<double>(x)<p.minimum || static_cast<double>(x)>p.maximum)) fail(m.source,"parameter out of bounds");
            };
            if(p.scalar==ScalarType::Float32 || p.scalar==ScalarType::Float64) {
                if(p.real.size()!=count || !p.signed_integer.empty() || !p.unsigned_integer.empty()) fail(m.source,"material parameter scalar lane/shape mismatch");
                for(auto v:p.real) { foundation_real<T>(v,m.source); if(p.scalar==ScalarType::Float32) foundation_real<float>(v,m.source); bounds(v); }
            } else if(p.scalar==ScalarType::Int32 || p.scalar==ScalarType::Int64) {
                if(p.signed_integer.size()!=count || !p.real.empty() || !p.unsigned_integer.empty()) fail(m.source,"material parameter scalar lane/shape mismatch");
                for(auto v:p.signed_integer) { if(p.scalar==ScalarType::Int32 && (v<INT32_MIN || v>INT32_MAX)) fail(m.source,"material integer width overflow"); bounds(v); }
            } else if(p.scalar==ScalarType::UInt8 || p.scalar==ScalarType::UInt32 || p.scalar==ScalarType::UInt64) {
                if(p.unsigned_integer.size()!=count || !p.real.empty() || !p.signed_integer.empty()) fail(m.source,"material parameter scalar lane/shape mismatch");
                for(auto v:p.unsigned_integer) { if((p.scalar==ScalarType::UInt8 && v>255) || (p.scalar==ScalarType::UInt32 && v>UINT32_MAX)) fail(m.source,"material integer width overflow"); bounds(v); }
            } else fail(m.source,"unknown material scalar type");
        }
        for(auto &t:m.tables) {
            if(t.name.empty() || t.units.empty() || t.axes.empty() || !t.components || !properties.insert(t.name).second ||
               t.interpolation>InterpolationPolicy::Constant || t.extrapolation>ExtrapolationPolicy::Linear ||
               (t.interpolation==InterpolationPolicy::Constant && t.extrapolation==ExtrapolationPolicy::Linear))
                fail(m.source,"invalid material table contract");
            std::set<std::string> axes;
            std::uint64_t count=t.components;
            for(auto &a:t.axes) {
                if(a.name.empty() || a.units.empty() || !axes.insert(a.name).second || a.points.empty() ||
                   (t.extrapolation==ExtrapolationPolicy::Linear && a.points.size()<2)) fail(m.source,"invalid table axis");
                count=foundation_product(count,a.points.size(),m.source);
                for(std::size_t i=0;i<a.points.size();++i) {
                    foundation_real<T>(a.points[i],m.source);
                    if(i && !(a.points[i]>a.points[i-1])) fail(m.source,"table axes must be strictly increasing");
                }
            }
            if(t.values.size()!=count) fail(m.source,"table Cartesian shape mismatch");
            for(auto v:t.values) foundation_real<T>(v,m.source);
        }
    }
    return out;
}

// Resolve cold metadata once. Kernel-facing IDs stay compact; no string lookup in hot loops.
template<class T,class Resolve> void resolve_foundation(const ModelIR &ir,CompiledFoundation &out,
    const SourceMap &sources,const std::vector<std::uint8_t> &node_dofs,Resolve resolve) {
    auto &f=ir.foundation;
    auto node=[&](const SourceKey &key,const Source &s) {
        if(!key.label) fail(s,"zero source node label");
        try { return sources.node_id(key); } catch(const std::out_of_range &) { fail(s,"unknown source node identity"); }
    };
    auto dof=[&](NodeId n,Dof d,const Source &s) {
        if(static_cast<unsigned>(d)>=node_dofs.at(n.value())) fail(s,"DOF unavailable on node");
    };
    auto nodes=[&](const std::string &name,const Source &s) {
        std::vector<NodeId> ids;
        for(auto n:resolve(true,name,"",s)) ids.push_back(NodeId(n));
        if(ids.empty()) fail(s,"empty node region");
        return ids;
    };
    auto elements=[&](const std::string &name,const Source &s) {
        std::vector<ElementId> ids;
        for(auto e:resolve(false,name,"",s)) ids.push_back(ElementId(e));
        if(ids.empty()) fail(s,"empty element region");
        return ids;
    };
    std::set<ConstraintId> constraints;
    auto constraint=[&](ConstraintId id,const Source &s) {
        if(!id.valid() || !constraints.insert(id).second) fail(s,"invalid/duplicate constraint ID");
    };
    for(auto &e:f.equations) {
        constraint(e.id,e.source); foundation_real<T>(e.rhs,e.source);
        if(e.terms.size()<2) fail(e.source,"equation requires at least two terms");
        ResolvedEquation r{e.id,{}};
        std::set<std::pair<NodeId,Dof>> terms;
        for(auto &t:e.terms) {
            auto n=node(t.node,e.source); dof(n,t.dof,e.source);
            foundation_real<T>(t.coefficient,e.source);
            if(t.coefficient==0 || !terms.emplace(n,t.dof).second) fail(e.source,"zero/duplicate equation term");
            r.nodes.push_back(n);
        }
        out.equations.push_back(std::move(r));
    }
    for(auto &t:f.ties) {
        constraint(t.id,t.source); foundation_real<T>(t.tolerance,t.source);
        if(t.tolerance<0 || t.primary_surface==t.secondary_surface || !out.surface_ids.contains(t.primary_surface) || !out.surface_ids.contains(t.secondary_surface))
            fail(t.source,"invalid tie surface/tolerance");
        out.ties.push_back({t.id,out.surface_ids.at(t.primary_surface),out.surface_ids.at(t.secondary_surface)});
    }
    for(auto &r:f.rigids) {
        constraint(r.id,r.source);
        auto ref=node(r.reference,r.source); auto region=nodes(r.nodes,r.source);
        if(!r.dof_mask || r.dof_mask>63) fail(r.source,"invalid rigid DOF mask");
        for(unsigned d=0;d<6;++d) if(r.dof_mask&(1u<<d)) {
            dof(ref,static_cast<Dof>(d),r.source);
            for(auto n:region) dof(n,static_cast<Dof>(d),r.source);
        }
        if(std::find(region.begin(),region.end(),ref)!=region.end()) fail(r.source,"rigid reference belongs to dependent region");
        out.rigids.push_back({r.id,ref,std::move(region)});
    }
    struct LoadIdentity { unsigned kind; std::string region; Dof dof; };
    std::map<LoadId,LoadIdentity> identities;
    std::set<std::pair<LoadId,StepId>> load_events;
    auto load=[&](const LoadDefinition &d,unsigned kind,Dof component) {
        if(!d.id.valid() || !d.step.valid() || d.step.value()>=ir.steps.size() || d.region.empty() ||
           d.update>LoadUpdate::Remove || !load_events.emplace(d.id,d.step).second) fail(d.source,"invalid/duplicate load event");
        (void)out.coordinate(d.coordinate);
        if(!d.amplitude.empty() && std::none_of(ir.amplitudes.begin(),ir.amplitudes.end(),[&](auto &a){return a.name==d.amplitude;})) fail(d.source,"unknown typed load amplitude");
        auto [it,inserted]=identities.emplace(d.id,LoadIdentity{kind,d.region,component});
        if(!inserted && (it->second.kind!=kind || it->second.region!=d.region || it->second.dof!=component)) fail(d.source,"load identity changes kind/region/component");
        return ResolvedLoad{d.id,d.step,{},{},{}};
    };
    for(auto &l:f.nodal_loads) {
        auto r=load(l.definition,0,l.dof); foundation_real<T>(l.value,l.definition.source);
        r.nodes=nodes(l.definition.region,l.definition.source);
        for(auto n:r.nodes) dof(n,l.dof,l.definition.source);
        out.loads.push_back(std::move(r));
    }
    for(auto &l:f.face_loads) {
        auto r=load(l.definition,10+static_cast<unsigned>(l.kind),Dof::X);
        if(l.kind>FaceLoadKind::Traction || !out.surface_ids.contains(l.definition.region)) fail(l.definition.source,"invalid face load surface/kind");
        foundation_real<T>(l.pressure,l.definition.source); for(auto v:l.traction) foundation_real<T>(v,l.definition.source);
        if((l.kind==FaceLoadKind::Pressure && l.traction!=std::array<double,3>{}) || (l.kind==FaceLoadKind::Traction && l.pressure!=0)) fail(l.definition.source,"face load inactive value lane is nonzero");
        r.surface=out.surface_ids.at(l.definition.region); out.loads.push_back(std::move(r));
    }
    for(auto &l:f.body_loads) {
        auto r=load(l.definition,20+static_cast<unsigned>(l.kind),Dof::X);
        if(l.kind>BodyLoadKind::ForcePerVolume) fail(l.definition.source,"invalid body load kind");
        for(auto v:l.value) foundation_real<T>(v,l.definition.source);
        r.elements=elements(l.definition.region,l.definition.source); out.loads.push_back(std::move(r));
    }
    // Evaluate update validity in chronological step order, independent of input vector ordering.
    std::map<std::pair<StepId,LoadId>,const LoadDefinition*> events;
    auto add_events=[&](auto &loads) { for(auto &l:loads) events.emplace(std::make_pair(l.definition.step,l.definition.id),&l.definition); };
    add_events(f.nodal_loads); add_events(f.face_loads); add_events(f.body_loads);
    std::set<LoadId> active;
    for(auto &[key,d]:events) {
        (void)key;
        if(d->update==LoadUpdate::Remove) { if(!active.erase(d->id)) fail(d->source,"remove of inactive load"); }
        else if(d->update==LoadUpdate::Add) { if(!active.contains(d->id)) fail(d->source,"add to inactive load"); }
        else active.insert(d->id);
    }
    std::set<std::uint64_t> initial_ids,activation_ids;
    std::set<std::tuple<NodeId,InitialQuantity,Dof>> initial_targets;
    for(auto &i:f.initial_conditions) {
        auto n=node(i.node,i.source); dof(n,i.dof,i.source); (void)out.coordinate(i.coordinate); foundation_real<T>(i.value,i.source);
        if(i.quantity>InitialQuantity::Temperature || (i.quantity==InitialQuantity::Temperature && i.dof!=Dof::X) || !initial_ids.insert(i.id).second || !initial_targets.emplace(n,i.quantity,i.dof).second)
            fail(i.source,"invalid/duplicate sparse initial condition");
        out.initial_conditions.push_back({i.id,n});
    }
    std::set<std::pair<StepId,ElementId>> activation_targets;
    for(auto &a:f.activations) {
        if(!a.step.valid() || a.step.value()>=ir.steps.size() || !activation_ids.insert(a.id).second) fail(a.source,"invalid/duplicate activation event");
        auto ids=elements(a.elements,a.source);
        for(auto e:ids) if(!activation_targets.emplace(a.step,e).second) fail(a.source,"overlapping activation events");
        out.activations.push_back({a.id,std::move(ids)});
    }
}
} // namespace sdb::lifecycle
