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
