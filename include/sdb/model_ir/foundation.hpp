#pragma once
#include "../model.hpp"
#include <set>
#include <functional>

namespace sdb::lifecycle {
struct Source {
    std::string file;
    std::uint64_t line = 1, column = 1;
    std::string str() const { return file + ":" + std::to_string(line) + ":" + std::to_string(column); }
    template <class F> void visit(F f) { f(file, line, column); }
};
struct SourceKey {
    std::string part, instance;
    Id label = 0;
    auto operator<=>(const SourceKey &) const = default;
    template <class F> void visit(F f) { f(part, instance, label); }
};
using Rotation = std::array<double, 9>;
inline constexpr Rotation identity_rotation{1,0,0,0,1,0,0,0,1};
// R maps local vectors to parent vectors (row major); origin is in parent coordinates.
struct IRCoordinate {
    CoordinateId id, parent{0};
    std::string name;
    std::array<double,3> origin{};
    Rotation rotation = identity_rotation;
    Source source;
    template<class F> void visit(F f) { f(id,parent,name,origin,rotation,source); }
};
struct IROrientation {
    OrientationId id;
    std::string name;
    CoordinateId coordinate{0};
    Rotation rotation = identity_rotation;
    Source source;
    template<class F> void visit(F f) { f(id,name,coordinate,rotation,source); }
};
enum class InterpolationPolicy : std::uint8_t { Linear, Constant };
enum class ExtrapolationPolicy : std::uint8_t { Reject, Clamp, Linear };
struct MaterialParameter {
    std::string name, units = "UNSPECIFIED";
    ScalarType scalar = ScalarType::Float64;
    std::vector<std::uint32_t> shape{1};
    // Exactly one typed lane is populated. Integers never pass through floating point.
    std::vector<double> real;
    std::vector<std::int64_t> signed_integer;
    std::vector<std::uint64_t> unsigned_integer;
    bool bounded = false;
    double minimum = 0, maximum = 0;
    template<class F> void visit(F f) { f(name,units,scalar,shape,real,signed_integer,unsigned_integer,bounded,minimum,maximum); }
};
struct TableAxis {
    std::string name, units = "UNSPECIFIED";
    std::vector<double> points;
    template<class F> void visit(F f) { f(name,units,points); }
};
struct MaterialTable {
    std::string name, units = "UNSPECIFIED";
    std::vector<TableAxis> axes;
    std::uint32_t components = 1;
    // Cartesian product, final axis fastest; components contiguous per sample.
    std::vector<double> values;
    InterpolationPolicy interpolation = InterpolationPolicy::Linear;
    ExtrapolationPolicy extrapolation = ExtrapolationPolicy::Reject;
    template<class F> void visit(F f) { f(name,units,axes,components,values,interpolation,extrapolation); }
};
struct MaterialDefinition {
    std::string material, schema_id;
    std::uint32_t version = 1, algorithm = 0;
    std::vector<MaterialParameter> parameters;
    std::vector<MaterialTable> tables;
    Source source;
    template<class F> void visit(F f) { f(material,schema_id,version,algorithm,parameters,tables,source); }
};
struct SectionAssignment {
    SectionId id;
    std::string scope, set, material;
    OrientationId orientation; // invalid means no material orientation binding
    Source source;
    template<class F> void visit(F f) { f(id,scope,set,material,orientation,source); }
};
struct IRSolidSection {
    SectionAssignment assignment;
    template<class F> void visit(F f) { f(assignment); }
};
struct IRShellLayer {
    std::string material;
    double thickness = 0, angle_degrees = 0;
    std::uint32_t section_points = 3;
    OrientationId orientation;
    template<class F> void visit(F f) { f(material,thickness,angle_degrees,section_points,orientation); }
};
struct IRShellSection {
    SectionAssignment assignment;
    double thickness = 0, offset = 0; // offset / total thickness, [-0.5, 0.5]
    std::uint32_t section_points = 5;
    std::vector<IRShellLayer> layers;
    template<class F> void visit(F f) { f(assignment,thickness,offset,section_points,layers); }
};
struct IRBeamSection {
    SectionAssignment assignment;
    double area = 0, i11 = 0, i22 = 0, i12 = 0, torsion = 0;
    template<class F> void visit(F f) { f(assignment,area,i11,i22,i12,torsion); }
};
struct IREquationTerm {
    SourceKey node;
    Dof dof = Dof::X;
    double coefficient = 0;
    template<class F> void visit(F f) { f(node,dof,coefficient); }
};
struct IREquation {
    ConstraintId id;
    std::vector<IREquationTerm> terms;
    double rhs = 0;
    Source source;
    template<class F> void visit(F f) { f(id,terms,rhs,source); }
};
struct IRTie {
    ConstraintId id;
    std::string primary_surface, secondary_surface;
    double tolerance = 0;
    bool adjust = false;
    Source source;
    template<class F> void visit(F f) { f(id,primary_surface,secondary_surface,tolerance,adjust,source); }
};
struct IRRigid {
    ConstraintId id;
    SourceKey reference;
    std::string nodes;
    std::uint8_t dof_mask = 7;
    Source source;
    template<class F> void visit(F f) { f(id,reference,nodes,dof_mask,source); }
};
enum class LoadUpdate : std::uint8_t { Replace, Add, Remove };
struct LoadDefinition {
    LoadId id;
    StepId step;
    std::string region, amplitude;
    CoordinateId coordinate{0};
    bool follower = false;
    LoadUpdate update = LoadUpdate::Replace;
    Source source;
    template<class F> void visit(F f) { f(id,step,region,amplitude,coordinate,follower,update,source); }
};
struct IRNodalLoad {
    LoadDefinition definition;
    Dof dof = Dof::X;
    double value = 0;
    template<class F> void visit(F f) { f(definition,dof,value); }
};
enum class FaceLoadKind : std::uint8_t { Pressure, Traction };
struct IRFaceLoad {
    LoadDefinition definition;
    FaceLoadKind kind = FaceLoadKind::Pressure;
    double pressure = 0;
    std::array<double,3> traction{};
    template<class F> void visit(F f) { f(definition,kind,pressure,traction); }
};
enum class BodyLoadKind : std::uint8_t { Acceleration, ForcePerVolume };
struct IRBodyLoad {
    LoadDefinition definition;
    BodyLoadKind kind = BodyLoadKind::Acceleration;
    std::array<double,3> value{};
    template<class F> void visit(F f) { f(definition,kind,value); }
};
enum class InitialQuantity : std::uint8_t { Displacement, Velocity, Acceleration, Temperature };
struct IRInitialCondition {
    std::uint64_t id = 0;
    InitialQuantity quantity = InitialQuantity::Velocity;
    SourceKey node;
    Dof dof = Dof::X;
    CoordinateId coordinate{0};
    double value = 0;
    bool valid = true;
    Source source;
    template<class F> void visit(F f) { f(id,quantity,node,dof,coordinate,value,valid,source); }
};
struct IRActivation {
    std::uint64_t id = 0;
    StepId step;
    std::string elements;
    bool active = true;
    Source source;
    template<class F> void visit(F f) { f(id,step,elements,active,source); }
};
struct ModelFoundation {
    std::vector<IRCoordinate> coordinates;
    std::vector<IROrientation> orientations;
    std::vector<MaterialDefinition> material_definitions;
    std::vector<IRSolidSection> solid_sections;
    std::vector<IRShellSection> shell_sections;
    std::vector<IRBeamSection> beam_sections;
    std::vector<IREquation> equations;
    std::vector<IRTie> ties;
    std::vector<IRRigid> rigids;
    std::vector<IRNodalLoad> nodal_loads;
    std::vector<IRFaceLoad> face_loads;
    std::vector<IRBodyLoad> body_loads;
    std::vector<IRInitialCondition> initial_conditions;
    std::vector<IRActivation> activations;
    template<class F> void visit(F f) { f(coordinates,orientations,material_definitions,solid_sections,shell_sections,beam_sections,equations,ties,rigids,nodal_loads,face_loads,body_loads,initial_conditions,activations); }
    bool empty() const {
        bool result = true;
        const_cast<ModelFoundation*>(this)->visit([&](const auto &...v) { ((result &= v.empty()), ...); });
        return result;
    }
};
// Schema-4 extension tail. The schema-3 ModelFoundation visitor is intentionally frozen.
enum class CouplingKind : std::uint8_t { Kinematic, Distributing };
struct CouplingWeight {
    SourceKey node;
    double weight=1;
    template<class F> void visit(F f) { f(node,weight); }
};
struct IRCoupling {
    ConstraintId id;
    std::string name;
    SourceKey reference;
    std::string nodes, surface; // exactly one scoped node region or semantic surface
    CouplingKind kind=CouplingKind::Kinematic;
    CoordinateId coordinate{0};
    std::uint8_t dof_mask=7;
    std::vector<CouplingWeight> weights; // distributing: one explicit positive weight per resolved node
    Source source;
    template<class F> void visit(F f) { f(id,name,reference,nodes,surface,kind,coordinate,dof_mask,weights,source); }
};
enum class AmplitudeTimeDomain : std::uint8_t { Step, Total };
struct IRAmplitudePolicy {
    std::string amplitude;
    AmplitudeTimeDomain time=AmplitudeTimeDomain::Step;
    InterpolationPolicy interpolation=InterpolationPolicy::Linear;
    ExtrapolationPolicy extrapolation=ExtrapolationPolicy::Clamp;
    Source source;
    template<class F> void visit(F f) { f(amplitude,time,interpolation,extrapolation,source); }
};
enum class PropertyValueKind : std::uint8_t { Constant, Table };
struct IRPropertyDistribution {
    std::uint64_t id=0;
    std::string name, scope, region, material, parameter;
    PropertyValueKind kind=PropertyValueKind::Constant;
    MaterialParameter constant;
    std::string table;
    OrientationId orientation;
    Source source;
    template<class F> void visit(F f) { f(id,name,scope,region,material,parameter,kind,constant,table,orientation,source); }
};
struct IRInitialEvent {
    StepId step;
    LoadUpdate update=LoadUpdate::Replace;
    IRInitialCondition condition;
    template<class F> void visit(F f) { f(step,update,condition); }
};
enum class ReactivationPolicy : std::uint8_t { StrainFree, WithStrain };
struct IRActivationPolicy {
    std::uint64_t activation=0;
    ReactivationPolicy reactivation=ReactivationPolicy::StrainFree;
    Source source;
    template<class F> void visit(F f) { f(activation,reactivation,source); }
};
struct ModelExtensions {
    std::uint32_t topology_numbering_version=1;
    SampleAxisDescriptor sample_axis;
    std::vector<IRCoupling> couplings;
    std::vector<IRAmplitudePolicy> amplitude_policies;
    std::vector<IRPropertyDistribution> property_distributions;
    std::vector<IRInitialEvent> initial_events;
    std::vector<IRActivationPolicy> activation_policies;
    template<class F> void visit(F f) { f(topology_numbering_version,sample_axis,couplings,amplitude_policies,property_distributions,initial_events,activation_policies); }
    bool empty() const {
        return topology_numbering_version==1 && semantic_hash(sample_axis)==semantic_hash(SampleAxisDescriptor{}) && couplings.empty() && amplitude_policies.empty() && property_distributions.empty() && initial_events.empty() && activation_policies.empty();
    }
};
struct ResolvedCoupling {
    ConstraintId id;
    NodeId reference;
    std::vector<NodeId> nodes;
    std::vector<double> weights; // normalized in node order; empty for kinematic
};
struct ResolvedProperty {
    std::uint64_t id=0;
    MaterialId material;
    std::vector<ElementId> elements;
};
struct EffectiveLoad {
    LoadId id;
    std::vector<StepId> contributions; // Replace resets; Add retains distinct amplitude/basis provenance
};
enum class LoadValueKind : std::uint8_t { Nodal, Pressure, Traction, Acceleration, ForcePerVolume };
struct LoadContribution {
    LoadDefinition definition;
    LoadValueKind kind=LoadValueKind::Nodal;
    Dof dof=Dof::X;
    double scalar=0;
    std::array<double,3> vector{};
};
struct PropertySelection {
    MaterialId material;
    std::string parameter;
    PropertyValueKind kind=PropertyValueKind::Constant;
    MaterialParameter constant;
    MaterialTable table;
    OrientationId orientation;
    std::uint64_t distribution=UINT64_MAX; // no region override
    Source source;
};
struct ResolvedCoordinate {
    CoordinateId id{0};
    std::array<double,3> origin{};
    Rotation rotation = identity_rotation;
    std::array<double,3> vector(const std::array<double,3> &v) const {
        std::array<double,3> out{};
        for (unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) out[i]+=rotation[3*i+j]*v[j];
        return out;
    }
    std::array<double,3> point(const std::array<double,3> &v) const {
        auto out=vector(v); for(unsigned i=0;i<3;++i) out[i]+=origin[i]; return out;
    }
};
struct ResolvedSection { SectionId id; std::vector<ElementId> elements; MaterialId material; };
struct ResolvedEquation { ConstraintId id; std::vector<NodeId> nodes; };
struct ResolvedTie { ConstraintId id; SurfaceId primary, secondary; };
struct ResolvedRigid { ConstraintId id; NodeId reference; std::vector<NodeId> nodes; };
struct ResolvedLoad { LoadId id; StepId step; std::vector<NodeId> nodes; std::vector<ElementId> elements; SurfaceId surface; };
struct ResolvedInitial { std::uint64_t id; NodeId node; };
struct ResolvedActivation { std::uint64_t id; std::vector<ElementId> elements; };
struct CompiledFoundation {
    ModelFoundation definition;
    ModelExtensions extensions;
    std::size_t step_count=0, element_count=0;
    std::vector<ResolvedCoupling> couplings;
    std::vector<ResolvedProperty> properties;
    std::map<std::string,std::vector<NodeId>> surface_nodes;
    std::vector<MaterialId> element_materials;
    std::vector<OrientationId> element_orientations;
    std::vector<ResolvedCoordinate> coordinates{{}};
    std::map<OrientationId,Rotation> orientation_bases;
    std::map<std::string,MaterialId> material_ids;
    std::map<std::string,SurfaceId> surface_ids;
    std::vector<ResolvedSection> sections;
    std::vector<ResolvedEquation> equations;
    std::vector<ResolvedTie> ties;
    std::vector<ResolvedRigid> rigids;
    std::vector<ResolvedLoad> loads;
    std::vector<ResolvedInitial> initial_conditions;
    std::vector<ResolvedActivation> activations;
    void require_step(StepId step) const {
        if(!step.valid() || step.value()>=step_count) throw std::out_of_range("unknown analysis step ID");
    }
    const ResolvedCoupling &coupling(ConstraintId id) const { return by_id(couplings,id); }
    const ResolvedProperty &property_distribution(std::uint64_t id) const { return by_id(properties,id); }
    const IRAmplitudePolicy *amplitude_policy(const std::string &name) const {
        for(const auto &p:extensions.amplitude_policies) if(p.amplitude==name) return &p;
        return nullptr;
    }
    std::vector<EffectiveLoad> effective_loads(StepId step) const {
        require_step(step);
        std::map<std::pair<StepId,LoadId>,const LoadDefinition*> events;
        auto append=[&](const auto &loads) { for(const auto &l:loads) if(l.definition.step<=step) events.emplace(std::make_pair(l.definition.step,l.definition.id),&l.definition); };
        append(definition.nodal_loads); append(definition.face_loads); append(definition.body_loads);
        std::map<LoadId,EffectiveLoad> active;
        for(const auto &[key,d]:events) {
            (void)key;
            if(d->update==LoadUpdate::Remove) active.erase(d->id);
            else { auto &load=active[d->id]; load.id=d->id; if(d->update==LoadUpdate::Replace) load.contributions.clear(); load.contributions.push_back(d->step); }
        }
        std::vector<EffectiveLoad> out; for(auto &[id,v]:active) { (void)id; out.push_back(std::move(v)); } return out;
    }
    LoadContribution load_contribution(LoadId id,StepId step) const {
        require_step(step);
        for(const auto &l:definition.nodal_loads) if(l.definition.id==id && l.definition.step==step) return {l.definition,LoadValueKind::Nodal,l.dof,l.value,{}};
        for(const auto &l:definition.face_loads) if(l.definition.id==id && l.definition.step==step) return {l.definition,l.kind==FaceLoadKind::Pressure?LoadValueKind::Pressure:LoadValueKind::Traction,Dof::X,l.pressure,l.traction};
        for(const auto &l:definition.body_loads) if(l.definition.id==id && l.definition.step==step) return {l.definition,l.kind==BodyLoadKind::Acceleration?LoadValueKind::Acceleration:LoadValueKind::ForcePerVolume,Dof::X,0,l.value};
        throw std::out_of_range("unknown load contribution");
    }
    const IRActivationPolicy &activation_policy(std::uint64_t id) const {
        for(const auto &p:extensions.activation_policies) if(p.activation==id) return p;
        throw std::out_of_range("no explicit activation policy");
    }
    const IRInitialEvent &initial_event(std::uint64_t id,StepId step) const {
        require_step(step); for(const auto &e:extensions.initial_events) if(e.step==step && e.condition.id==id) return e;
        throw std::out_of_range("unknown initial event");
    }
    std::vector<IRInitialCondition> effective_initials(StepId step) const {
        require_step(step);
        std::map<std::uint64_t,IRInitialCondition> active;
        for(const auto &i:definition.initial_conditions) active.emplace(i.id,i);
        std::map<std::pair<StepId,std::uint64_t>,const IRInitialEvent*> events;
        for(const auto &e:extensions.initial_events) if(e.step<=step) events.emplace(std::make_pair(e.step,e.condition.id),&e);
        for(const auto &[key,e]:events) {
            (void)key;
            if(e->update==LoadUpdate::Remove) active.erase(e->condition.id);
            else if(e->update==LoadUpdate::Replace) active[e->condition.id]=e->condition;
            else active.at(e->condition.id).value+=e->condition.value;
        }
        std::vector<IRInitialCondition> out; for(auto &[id,v]:active) { (void)id; out.push_back(std::move(v)); } return out;
    }
    bool element_active(ElementId element,StepId step) const {
        require_step(step); if(!element.valid() || element.value()>=element_count) throw std::out_of_range("unknown activation element ID");
        bool active=true; StepId latest; bool found=false;
        for(std::size_t i=0;i<definition.activations.size();++i) {
            const auto &e=definition.activations[i];
            if(e.step<=step && (!found || e.step>latest)) {
                const auto &ids=activations.at(i).elements;
                if(std::find(ids.begin(),ids.end(),element)!=ids.end()) { found=true; latest=e.step; active=e.active; }
            }
        }
        return active;
    }
    PropertySelection property(ElementId element,const std::string &name) const {
        if(!element.valid() || element.value()>=element_materials.size()) throw std::out_of_range("unknown property element ID");
        PropertySelection result; result.material=element_materials[element.value()]; result.parameter=name;
        result.orientation=element_orientations.at(element.value());
        const auto &m=material(result.material);
        auto p=std::find_if(m.parameters.begin(),m.parameters.end(),[&](const auto &v){return v.name==name;});
        if(p==m.parameters.end()) throw std::out_of_range("unknown material property parameter");
        result.constant=*p; result.source=m.source;
        for(std::size_t i=0;i<properties.size();++i) {
            const auto &r=properties[i]; const auto &d=extensions.property_distributions.at(i);
            if(d.parameter!=name || std::find(r.elements.begin(),r.elements.end(),element)==r.elements.end()) continue;
            result.distribution=d.id; result.source=d.source; result.kind=d.kind;
            if(d.orientation.valid()) result.orientation=d.orientation;
            if(d.kind==PropertyValueKind::Constant) result.constant=d.constant;
            else { auto t=std::find_if(m.tables.begin(),m.tables.end(),[&](const auto &v){return v.name==d.table;}); result.table=*t; }
            break;
        }
        return result;
    }
    const ResolvedCoordinate &coordinate(CoordinateId id) const {
        for (auto &x:coordinates) if(x.id==id) return x;
        throw std::out_of_range("unknown coordinate ID");
    }
    const Rotation &orientation(OrientationId id) const {
        auto it = orientation_bases.find(id);
        if (it == orientation_bases.end()) throw std::out_of_range("unknown orientation ID");
        return it->second;
    }
    const MaterialDefinition &material(MaterialId id) const {
        for(auto &x:definition.material_definitions) if(material_ids.at(x.material)==id) return x;
        throw std::out_of_range("material has no typed definition");
    }
    const ResolvedSection &section(SectionId id) const {
        for(auto &x:sections) if(x.id==id) return x;
        throw std::out_of_range("unknown section ID");
    }
    template<class V,class I> static const typename V::value_type &by_id(const V &records,I id) {
        for(auto &x:records) if(x.id==id) return x;
        throw std::out_of_range("unknown foundation entity ID");
    }
    const ResolvedEquation &equation(ConstraintId id) const { return by_id(equations,id); }
    const ResolvedTie &tie(ConstraintId id) const { return by_id(ties,id); }
    const ResolvedRigid &rigid(ConstraintId id) const { return by_id(rigids,id); }
    const ResolvedInitial &initial(std::uint64_t id) const { return by_id(initial_conditions,id); }
    const ResolvedActivation &activation(std::uint64_t id) const { return by_id(activations,id); }
    const IRShellSection &shell_section(SectionId id) const {
        for(auto &x:definition.shell_sections) if(x.assignment.id==id) return x;
        throw std::out_of_range("unknown shell section ID");
    }
    const IRBeamSection &beam_section(SectionId id) const {
        for(auto &x:definition.beam_sections) if(x.assignment.id==id) return x;
        throw std::out_of_range("unknown beam section ID");
    }
    const IRSolidSection &solid_section(SectionId id) const {
        for(auto &x:definition.solid_sections) if(x.assignment.id==id) return x;
        throw std::out_of_range("unknown solid section ID");
    }
    const ResolvedLoad &load(LoadId id,StepId step) const {
        for(auto &x:loads) if(x.id==id && x.step==step) return x;
        throw std::out_of_range("unknown load event");
    }
};
} // namespace sdb::lifecycle
