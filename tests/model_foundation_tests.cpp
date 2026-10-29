#include <sdb/io/model_store.hpp>
#include <sdb/dynamics.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks=0;
void check(bool ok,const char *message) { ++checks; if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f,const std::string &message="") {
    ++checks;
    try { f(); } catch(const std::exception &e) {
        if(!message.empty() && std::string(e.what()).find(message)==std::string::npos)
            throw std::runtime_error(std::string("unexpected diagnostic: ")+e.what()+" expected "+message);
        return;
    }
    throw std::runtime_error("expected rejection: "+message);
}
template<class T> HistorySchema fixture_history(std::string name,std::vector<double> values) {
    HistorySchema h; h.id="synthetic-storage-fixture."+name;
    HistoryVariable v; v.field.name=name; v.field.scalar=scalar_type<T>();
    v.field.shape={static_cast<std::uint32_t>(values.size())}; v.field.components.clear();
    for(std::size_t i=0;i<values.size();++i) v.field.components.push_back("C"+std::to_string(i));
    v.initial=std::move(values); h.variables.push_back(std::move(v)); return h;
}
template<class T> ModelIR synthetic_model() {
    ModelIR ir; ir.schema=3; ir.units="UNSPECIFIED"; ir.assembly="SYNTHETIC-STORAGE-FIXTURE";
    IRPart p; p.name="P";
    const std::vector<std::array<double,3>> xyz={{0,0,0},{1,0,0},{0,1,0},{0,0,1},{1,1,0},{0,1,0},{0,0,2},{1,0,2},
        {2,0,0},{3,0,0},{3,1,0},{2,1,0},{2,0,1},{3,0,1},{3,1,1},{2,1,1}};
    for(std::size_t i=0;i<xyz.size();++i) p.nodes.push_back({i+1,xyz[i],{"synthetic",i+1,1}});
    p.elements={{1,descriptor("C3D4",{}),{1,2,3,4},{}},{2,descriptor("S4R",{}),{1,2,5,6},{}},
        {3,descriptor("B31",{}),{7,8},{}},{4,descriptor("C3D8",{}),{9,10,11,12,13,14,15,16},{}}};
    ir.parts.push_back(p);
    ir.instances.push_back({"A","P",{0,0,0},{0,0,0,0,0,1,0},{}});
    ir.instances.push_back({"B","P",{10,0,0},{0,0,0,0,0,1,90},{}});
    ir.sets={{"P","SOLID","",false,{"1","4"},{}},{"P","SHELL","",false,{"2"},{}},
        {"P","BEAM","",false,{"3"},{}},{"P","NODES","",true,{"1","2"},{}},
        {"P","DEPENDENT","",true,{"2","3"},{}}};
    ir.materials={{"ELASTIC",1,100,.25,{}},{"LAYER-A",0,0,0,{}},{"LAYER-B",0,0,0,{}}};
    ir.history_bindings={{"LAYER-A",101,fixture_history<T>("damage",{.25})},
        {"LAYER-B",102,fixture_history<T>("plastic",{2,3})}};
    ir.surfaces={{"P","TOP",{{"SHELL","SPOS"}},{}},{"P","BOTTOM",{{"SHELL","SNEG"}},{}}};
    ir.amplitudes={{"RAMP",false,{{0,0},{1,1}},{}},{"TOTAL",true,{{0,1},{2,2}},{}}};
    ir.steps={{"FIRST",.01,1,false,{},{},{},{}},{"SECOND",.01,1,false,{},{},{},{}}};
    auto &f=ir.foundation;
    f.coordinates={{CoordinateId(1),CoordinateId(0),"LOCAL",{1,2,3},{0,-1,0,1,0,0,0,0,1},{}},
        {CoordinateId(2),CoordinateId(1),"CHILD",{1,0,0},identity_rotation,{}}};
    f.orientations={{OrientationId(7),"FIBER",CoordinateId(2),identity_rotation,{}}};
    MaterialDefinition ma; ma.material="LAYER-A"; ma.schema_id="synthetic.plastic"; ma.algorithm=101;
    MaterialParameter yield; yield.name="yield"; yield.units="stress"; yield.real={10}; yield.bounded=true; yield.minimum=0; yield.maximum=100;
    MaterialParameter count; count.name="count"; count.scalar=ScalarType::UInt64; count.unsigned_integer={UINT64_C(9007199254740993)};
    ma.parameters={yield,count};
    ma.tables={{"hardening","stress",{{"strain","1",{0,1}},{"temperature","K",{100,200}}},1,{1,2,3,4},InterpolationPolicy::Linear,ExtrapolationPolicy::Clamp}};
    auto mb=ma; mb.material="LAYER-B"; mb.schema_id="synthetic.viscous"; mb.algorithm=102;
    f.material_definitions={ma,mb};
    f.solid_sections={{{SectionId(10),"P","SOLID","ELASTIC",{}, {}}}};
    IRShellSection shell; shell.assignment={SectionId(11),"P","SHELL","LAYER-A",OrientationId(7),{}};
    shell.thickness=.3; shell.offset=.25;
    shell.layers={{"LAYER-A",.1,0,2,OrientationId(7)},{"LAYER-B",.2,45,3,{}}};
    f.shell_sections={shell};
    f.beam_sections={{{SectionId(12),"P","BEAM","ELASTIC",OrientationId(7),{}},2,3,4,.2,5}};
    f.equations={{ConstraintId(1),{{{"P","A",1},Dof::X,1},{{"P","B",1},Dof::X,-1}},0,{}}};
    f.ties={{ConstraintId(2),"A.TOP","B.BOTTOM",.01,false,{}}};
    f.rigids={{ConstraintId(3),{"P","A",4},"A.DEPENDENT",7,{}}};
    LoadDefinition load{LoadId(1),StepId(0),"A.NODES","RAMP",CoordinateId(1),true,LoadUpdate::Replace,{}};
    f.nodal_loads={{load,Dof::X,2}};
    load.step=StepId(1); load.update=LoadUpdate::Add; f.nodal_loads.push_back({load,Dof::X,3});
    load={LoadId(2),StepId(0),"A.TOP","",CoordinateId(0),true,LoadUpdate::Replace,{}};
    f.face_loads={{load,FaceLoadKind::Pressure,4,{}}};
    load={LoadId(3),StepId(0),"B.SOLID","TOTAL",CoordinateId(0),false,LoadUpdate::Replace,{}};
    f.body_loads={{load,BodyLoadKind::Acceleration,{0,0,-9.8}}};
    f.initial_conditions={{20,InitialQuantity::Velocity,{"P","A",1},Dof::X,CoordinateId(1),5,true,{}},
        {21,InitialQuantity::Velocity,{"P","A",2},Dof::X,CoordinateId(0),0,false,{}}};
    f.activations={{30,StepId(1),"B.SHELL",false,{}}};
    return ir;
}
template<class T> void query_oracle(const CompiledModel<T> &m) {
    check(m.definition.schema==3,"schema3");
    check(m.mesh.nodes()==32 && m.mesh.elements()==8,"two instances mixed topology");
    auto a=m.sources.node_id({"P","A",1}), b=m.sources.node_id({"P","B",1});
    check(a!=b && m.sources.reverse(a).key.label==m.sources.reverse(b).key.label,"scoped strong SourceMap roundtrip");
    check(std::abs(m.mesh.reference()(b.value(),0))<1e-6 && std::abs(m.mesh.reference()(b.value(),1)-10)<1e-6,"instance translation then rotation oracle");
    auto shell=m.sources.element_id({"P","A",2}).value();
    check(m.mesh.sample_point_count(shell)==5,"two plus three layer points");
    auto sample=m.mesh.sample_point(shell,3);
    check(sample.integration_point==1 && sample.layer==2 && sample.section_point==2 && sample.material==2,"sample point oracle");
    StateDB<T> state(m.mesh);
    auto address=state.history_address(m.mesh,shell,0,"plastic",1,2,2);
    check(address==5 && state.data().history.read()(address)==T(3),"independent layered history address/value oracle");
    check(state.data().history.size()==16,"two instances with eight layered history values");
    check(m.mesh.dofs(m.sources.node_id({"P","A",3}).value())==3,"solid-only node has no rotations");
    check(m.mesh.dofs(a.value())==6,"shell-connected node rotations");
    auto point=m.foundation.coordinate(CoordinateId(2)).point({1,0,0});
    check(point==std::array<double,3>{1,4,3},"nested coordinate oracle");
    check(m.foundation.material(MaterialId(1)).parameters[1].unsigned_integer[0]==UINT64_C(9007199254740993),"lossless uint64 material parameter");
    check(m.mesh.materials()[1].parameters.empty(),"no fabricated elasticity");
    check(m.foundation.section(SectionId(11)).elements.size()==2,"resolved shared part section");
    check(m.foundation.load(LoadId(3),StepId(0)).elements.size()==2,"body load resolves element region");
    check(m.foundation.equations[0].nodes==std::vector<NodeId>{a,b},"equation resolved identities");
    check(m.foundation.ties[0].primary==SurfaceId(0) && m.foundation.ties[0].secondary==SurfaceId(3),"tie semantic surface identity");
    check(m.foundation.initial_conditions[1].node==m.sources.node_id({"P","A",2}) && !m.foundation.definition.initial_conditions[1].valid,"sparse missingness survives");
    check(!m.report.executable_cpu && m.mesh.requires_storage_only(),"storage-only gates");
    rejects([&]{m.require_cpu();},"executable CPU");
    rejects([&]{TetPlan<T> plan(m.mesh,state);},"storage-only");
}
template<class T> void model_contracts(const std::filesystem::path &dir) {
    auto ir=synthetic_model<T>(); auto m=ModelCompiler{}.compile<T>(ir); query_oracle(m);
    auto tag=sizeof(T)==4?"float":"double";
    ModelStore::save(dir/(std::string(tag)+".sdbmodel"),m);
    auto loaded=ModelStore::load<T>(dir/(std::string(tag)+".sdbmodel")); query_oracle(loaded);
    check(native::pack(ir)==native::pack(loaded.definition),"every schema3 field roundtrips");
    auto changed=ir;
    std::reverse(changed.parts[0].nodes.begin(),changed.parts[0].nodes.end());
    std::reverse(changed.parts[0].elements.begin(),changed.parts[0].elements.end());
    changed.parts[0].nodes[0].source={"other-file",200,9};
    auto reordered=ModelCompiler{}.compile<T>(changed);
    check(reordered.semantic_identity()==m.semantic_identity(),"semantic identity ignores numbering order/provenance");
    check(reordered.layout_identity()!=m.layout_identity(),"layout identity tracks reorder");
    check(reordered.sources.reverse(reordered.sources.node_id({"P","A",1})).key==m.sources.reverse(m.sources.node_id({"P","A",1})).key,"reordered external identity stable");
    for(unsigned kind=0;kind<5;++kind) {
        changed=ir;
        if(kind==0) changed.foundation.material_definitions[0].parameters[0].real[0]=11;
        if(kind==1) changed.foundation.shell_sections[0].offset=.2;
        if(kind==2) changed.foundation.orientations[0].rotation={0,-1,0,1,0,0,0,0,1};
        if(kind==3) changed.foundation.nodal_loads[0].value=9;
        if(kind==4) changed.history_bindings[0].history_schema.variables[0].field.name="other_damage";
        auto other=ModelCompiler{}.compile<T>(changed);
        check(other.semantic_identity()!=m.semantic_identity() && other.layout_identity()!=m.layout_identity(),"semantic changes affect identities");
        if(kind==3) check(other.analysis_identity()!=m.analysis_identity(),"analysis identity includes typed loads");
    }
    auto invalid=[&](auto mutate,const std::string &diagnostic) { auto bad=ir; mutate(bad); rejects([&]{ModelCompiler{}.compile<T>(bad);},diagnostic); };
    invalid([](auto &x){x.schema=2;},"schema 3");
    invalid([](auto &x){x.foundation.coordinates[0].parent=CoordinateId(2);},"cycle");
    invalid([](auto &x){x.foundation.coordinates[0].parent=CoordinateId(99);},"parent");
    invalid([](auto &x){x.foundation.coordinates[0].rotation[0]=2;},"orthonormal");
    invalid([](auto &x){x.foundation.coordinates[0].rotation={1,0,0,0,1,0,0,0,-1};},"right handed");
    invalid([](auto &x){x.foundation.coordinates[0].id=CoordinateId(0);},"global");
    invalid([](auto &x){x.foundation.orientations[0].coordinate=CoordinateId(99);},"coordinate");
    invalid([](auto &x){x.foundation.orientations.push_back(x.foundation.orientations[0]);},"duplicate orientation");
    invalid([](auto &x){x.foundation.material_definitions[0].parameters.push_back(x.foundation.material_definitions[0].parameters[0]);},"duplicate material parameter");
    invalid([](auto &x){x.foundation.material_definitions[0].parameters[0].real={101};},"bounds");
    invalid([](auto &x){x.foundation.material_definitions[0].parameters[0].shape={UINT32_MAX,2};},"overflow");
    invalid([](auto &x){x.foundation.material_definitions[0].parameters[1].scalar=ScalarType::UInt32;},"width overflow");
    invalid([](auto &x){x.foundation.material_definitions[0].parameters[1].real={1};},"lane");
    invalid([](auto &x){auto &p=x.foundation.material_definitions[0].parameters[1];p.bounded=true;p.minimum=0;p.maximum=9007199254740992.0;},"exact binary64");
    invalid([](auto &x){auto &p=x.foundation.material_definitions[0].parameters[1];p.bounded=true;p.minimum=0;p.maximum=1e30;},"exact binary64");
    invalid([](auto &x){x.foundation.material_definitions[0].tables[0].axes[0].points={1,0};},"increasing");
    invalid([](auto &x){x.foundation.material_definitions[0].tables[0].values.pop_back();},"shape");
    invalid([](auto &x){x.foundation.material_definitions[0].material="MISSING";},"anchor");
    invalid([](auto &x){x.history_bindings[0].model=900;},"algorithm identity mismatch");
    invalid([](auto &x){x.foundation.solid_sections[0].assignment.scope="UNKNOWN";},"scope");
    invalid([](auto &x){x.foundation.solid_sections[0].assignment.id=SectionId(11);},"duplicate typed section");
    invalid([](auto &x){x.sections.push_back({"P","SOLID","ELASTIC","SOLID SECTION",{},{}});},"overlapping section");
    invalid([](auto &x){x.foundation.shell_sections[0].layers[0].thickness=-1;},"layer thickness");
    invalid([](auto &x){x.foundation.shell_sections[0].thickness=.4;},"sum");
    invalid([](auto &x){x.foundation.shell_sections[0].layers[1].material="MISSING";},"layer thickness/material");
    invalid([](auto &x){x.foundation.shell_sections[0].offset=2;},"offset");
    invalid([](auto &x){x.foundation.beam_sections[0].i12=100;},"beam");
    invalid([](auto &x){x.foundation.beam_sections[0].assignment.orientation=OrientationId(99);},"unknown orientation ID");
    invalid([](auto &x){x.foundation.equations[0].terms[0].node.label=999;},"identity");
    invalid([](auto &x){x.foundation.equations[0].terms[0].node.label=3;x.foundation.equations[0].terms[0].dof=Dof::RX;},"DOF unavailable");
    invalid([](auto &x){x.foundation.equations[0].terms[1]=x.foundation.equations[0].terms[0];},"duplicate equation");
    invalid([](auto &x){x.foundation.equations[0].terms[0].coefficient=0;},"zero/duplicate");
    invalid([](auto &x){x.foundation.ties[0].secondary_surface="MISSING";},"tie surface");
    invalid([](auto &x){x.foundation.ties[0].tolerance=-1;},"tolerance");
    invalid([](auto &x){x.foundation.rigids[0].id=ConstraintId(1);},"constraint ID");
    invalid([](auto &x){x.foundation.rigids[0].reference.label=2;},"dependent region");
    invalid([](auto &x){x.foundation.nodal_loads[0].definition.step=StepId(9);},"load event");
    invalid([](auto &x){x.foundation.nodal_loads[0].definition.coordinate=CoordinateId(99);},"coordinate");
    invalid([](auto &x){x.foundation.nodal_loads[0].definition.amplitude="MISSING";},"amplitude");
    invalid([](auto &x){x.foundation.nodal_loads[1].definition.region="B.NODES";},"identity changes");
    invalid([](auto &x){x.foundation.nodal_loads[0].definition.update=LoadUpdate::Remove;},"inactive load");
    invalid([](auto &x){x.foundation.face_loads[0].definition.region="MISSING";},"face load");
    invalid([](auto &x){x.foundation.body_loads[0].definition.region="MISSING";},"ELSET");
    invalid([](auto &x){x.foundation.initial_conditions[1]=x.foundation.initial_conditions[0];},"initial condition");
    invalid([](auto &x){x.foundation.activations.push_back({31,StepId(1),"B.SHELL",true,{}});},"overlapping activation");
    // SourceMap rejects non-dense mutation without poisoning the lookup.
    SourceMap source;
    rejects([&]{source.add(true,{{"P","A",1},1,2,{}});},"densely");
    check(source.nodes.empty() && source.node_lookup.empty(),"SourceMap add failure atomicity");
}
void parser_contracts(const std::filesystem::path &fixtures) {
    auto assembly=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixtures/"assembly_equation.inp").model);
    check(assembly.foundation.equation(ConstraintId(0)).nodes==std::vector<NodeId>{assembly.sources.node_id({"P","A",1}),assembly.sources.node_id({"P","B",1})},"assembly equation scoped SourceKeys");
    auto parsed=ReaderRegistry{}.read(fixtures/"equation_orientation.inp");
    auto m=ModelCompiler{}.compile<double>(parsed.model);
    check(m.definition.foundation.equations[0].terms.size()==5,"equation continuation");
    auto &r=m.foundation.orientation(OrientationId(0));
    check(std::abs(r[0])<1e-12 && std::abs(r[3]-1)<1e-12 && r[8]==1,"orientation axis3 90 degree oracle");
    check(!m.report.executable_cpu,"parsed foundation is storage-only");
    StateDB<double> state(m.mesh);
    rejects([&]{TetPlan<double> plan(m.mesh,state);},"storage-only");
    std::ifstream in(fixtures/"equation_orientation.inp"); std::string text{std::istreambuf_iterator<char>(in),{}};
    auto reject_text=[&](std::string from,std::string to,const std::string &diagnostic) {
        auto value=text; auto pos=value.find(from); if(pos==value.npos) throw std::runtime_error("fixture edit missing"); value.replace(pos,from.size(),to);
        std::istringstream stream(value); rejects([&]{auto result=AbaqusInpReader{}.read(stream,fixtures/"mutated.inp",{}); ModelCompiler{}.compile<double>(result.model);},diagnostic);
    };
    reject_text("SYSTEM=RECTANGULAR","SYSTEM=CYLINDRICAL","rectangular");
    reject_text("*ORIENTATION","*ASSEMBLY,NAME=BAD\n*ORIENTATION","root rectangular");
    reject_text("*ORIENTATION","*PART,NAME=BAD\n*ORIENTATION","root rectangular");
    reject_text("*EQUATION","*EQUATION, UNKNOWN=YES","parameter");
    reject_text("1,1,1, 2,1,-1, 3,1,2, 4,1,-2","1,1,1, 2,1,-1, 3,1,2, 4,1,-2, 1,2,1","four");
    reject_text("1,1,1, 2,1,-1","ALL,1,1, 2,1,-1","node sets");
    reject_text("3,90","4,90","axis");
    reject_text("1,0,0, 0,1,0","1,0,0, 2,0,0","degenerate");
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--read") {
            query_oracle(ModelStore::load<float>(std::filesystem::path(argv[2])/"float.sdbmodel"));
            query_oracle(ModelStore::load<double>(std::filesystem::path(argv[2])/"double.sdbmodel"));
        } else {
            if(argc!=3) throw std::invalid_argument("usage: model_foundation_tests <fixtures> <scratch> | --read <scratch>");
            std::filesystem::create_directories(argv[2]);
            model_contracts<float>(argv[2]); model_contracts<double>(argv[2]); parser_contracts(argv[1]);
        }
        std::cout<<checks<<" model foundation checks passed\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
