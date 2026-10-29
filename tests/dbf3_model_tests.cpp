#include <sdb/lifecycle.hpp>
#include <sdb/dynamics.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
unsigned checks=0;
void check(bool value,const char *message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F f,const std::string &text="") {
    ++checks;
    try {f();} catch(const std::exception &e) {
        if(!text.empty() && std::string(e.what()).find(text)==std::string::npos) throw std::runtime_error(std::string("unexpected diagnostic: ")+e.what()+" expected "+text);
        return;
    }
    throw std::runtime_error("expected rejection "+text);
}
ModelIR fixture() {
    ModelIR ir;ir.schema=4;ir.units="UNSPECIFIED";
    IRPart p;p.nodes={{1,{0,0,0},{}},{2,{1,0,0},{}},{3,{0,1,0},{}},{4,{0,0,1},{}},{5,{1,1,1},{}},{6,{2,2,2},{}}};
    p.elements={{1,descriptor("C3D4",{}),{1,2,3,4},{}},{2,descriptor("C3D4",{}),{2,3,4,5},{}}};ir.parts={p};
    ir.sets={{"","ALL","",false,{"1","2"},{}},{"","LEFT","",false,{"1"},{}},{"","RIGHT","",false,{"2"},{}},{"","DEPENDENT","",true,{"1","2"},{}}};
    ir.materials={{"M",0,0,0,{}}};
    MaterialDefinition m;m.material="M";m.schema_id="synthetic-storage-fixture.properties";m.algorithm=99;
    MaterialParameter parameter;parameter.name="yield";parameter.units="stress";parameter.real={10};parameter.bounded=true;parameter.minimum=0;parameter.maximum=100;m.parameters={parameter};
    m.tables={{"curve","stress",{{"strain","1",{0,1}}},1,{20,40},InterpolationPolicy::Linear,ExtrapolationPolicy::Clamp}};
    ir.foundation.material_definitions={m};
    ir.foundation.coordinates={{CoordinateId(1),CoordinateId(0),"LOCAL",{}, {0,-1,0,1,0,0,0,0,1},{}}};
    ir.foundation.orientations={{OrientationId(7),"FIBER",CoordinateId(1),identity_rotation,{}}};
    ir.foundation.solid_sections={{{SectionId(1),"","ALL","M",OrientationId(7),{}}}};
    ir.surfaces={{"","FACE",{{"LEFT","S1"}},{}}};
    ir.steps={{"S1",.1,2,false,{},{},{},{}},{"S2",.1,2,false,{},{},{},{}},{"S3",.1,2,false,{},{},{},{}},{"S4",.1,2,false,{},{},{},{}}};
    ir.amplitudes={{"RAMP",false,{{0,0},{2,2}},{}},{"STAIR",false,{{0,1},{1,3},{2,5}},{}},{"TOTAL",true,{{0,0},{8,8}},{}}};
    auto &x=ir.extensions;
    x.couplings={{ConstraintId(1),"WEIGHTED",{"","",6},"DEPENDENT","",CouplingKind::Distributing,CoordinateId(1),7,{{{"","",1},1},{{"","",2},3}},{}},
        {ConstraintId(2),"KINEMATIC",{"","",6},"","FACE",CouplingKind::Kinematic,CoordinateId(0),7,{},{}}};
    x.amplitude_policies={{"RAMP",AmplitudeTimeDomain::Step,InterpolationPolicy::Linear,ExtrapolationPolicy::Linear,{}},
        {"STAIR",AmplitudeTimeDomain::Step,InterpolationPolicy::Constant,ExtrapolationPolicy::Reject,{}},
        {"TOTAL",AmplitudeTimeDomain::Total,InterpolationPolicy::Linear,ExtrapolationPolicy::Clamp,{}}};
    IRPropertyDistribution a;a.id=1;a.name="LEFT-OVERRIDE";a.region="LEFT";a.material="M";a.parameter="yield";a.constant=parameter;a.constant.real={15};a.orientation=OrientationId(7);a.source={"property-source",9,1};
    IRPropertyDistribution b;b.id=2;b.name="RIGHT-TABLE";b.region="RIGHT";b.material="M";b.parameter="yield";b.kind=PropertyValueKind::Table;b.table="curve";
    x.property_distributions={a,b};
    LoadDefinition load{LoadId(4),StepId(0),"DEPENDENT","RAMP",CoordinateId(0),false,LoadUpdate::Replace,{}};
    ir.foundation.nodal_loads={{load,Dof::X,2}};
    load.step=StepId(1);load.update=LoadUpdate::Add;load.amplitude="TOTAL";load.coordinate=CoordinateId(1);ir.foundation.nodal_loads.push_back({load,Dof::X,3});
    load.step=StepId(3);load.update=LoadUpdate::Remove;ir.foundation.nodal_loads.push_back({load,Dof::X,0});
    IRInitialCondition initial{10,InitialQuantity::Velocity,{"","",1},Dof::Y,CoordinateId(0),1,true,{}};
    ir.foundation.initial_conditions={initial};initial.value=2;x.initial_events.push_back({StepId(1),LoadUpdate::Add,initial});
    initial.value=0;initial.valid=false;x.initial_events.push_back({StepId(2),LoadUpdate::Replace,initial});x.initial_events.push_back({StepId(3),LoadUpdate::Remove,initial});
    ir.foundation.activations={{20,StepId(1),"RIGHT",false,{}},{21,StepId(3),"RIGHT",true,{}}};
    x.activation_policies={{21,ReactivationPolicy::WithStrain,{}}};
    return ir;
}
template<class T> void oracle(const CompiledModel<T> &m) {
    check(m.definition.schema==4,"schema4");
    const auto &c=m.foundation.coupling(ConstraintId(1));
    check(c.reference==NodeId(5) && c.nodes==std::vector<NodeId>{NodeId(0),NodeId(1)},"coupling scoped resolution");
    check(c.weights==std::vector<double>{.25,.75},"independent normalized weight oracle");
    check(m.foundation.coupling(ConstraintId(2)).nodes==std::vector<NodeId>{NodeId(0),NodeId(1),NodeId(2)},"surface coupling node membership");
    auto left=m.foundation.property(ElementId(0),"yield"),right=m.foundation.property(ElementId(1),"yield");
    check(left.constant.real==std::vector<double>{15} && left.distribution==1 && left.orientation==OrientationId(7) && left.source.line==9,"constant distribution/orientation provenance");
    check(right.kind==PropertyValueKind::Table && right.table.values==std::vector<double>{20,40},"table property reference survives");
    check(m.amplitude_value("RAMP",3,0)==3 && m.amplitude_value("RAMP",-1,0)==-1,"linear extrapolation oracle");
    check(m.amplitude_value("STAIR",.5,0)==1 && m.amplitude_value("STAIR",1,0)==3,"constant interpolation knot convention");
    rejects([&]{m.amplitude_value("STAIR",3,0);},"outside table");
    check(m.amplitude_value("TOTAL",0,4)==4,"total time amplitude");
    check(m.load_amplitude("RAMP",StepId(0),StepId(2),.5,4.5)==2,"propagated step-time amplitude holds origin endpoint");
    check(m.load_amplitude("TOTAL",StepId(0),StepId(2),.5,4.5)==4.5,"propagated total-time amplitude continues");
    const auto loads=m.foundation.effective_loads(StepId(2));
    check(loads.size()==1 && loads[0].contributions==std::vector<StepId>{StepId(0),StepId(1)},"load Add keeps distinct amplitude/basis contributions");
    check(m.foundation.effective_loads(StepId(3)).empty(),"load Remove");
    auto values=m.effective_load_values(StepId(2),.5,4.5);
    check(values.size()==2 && values[0].scalar==4 && values[1].scalar==13.5 && values[1].definition.coordinate==CoordinateId(1),"effective values preserve distinct bases and amplitude origins");
    check(m.foundation.activation_policy(21).reactivation==ReactivationPolicy::WithStrain && m.foundation.initial_event(10,StepId(1)).update==LoadUpdate::Add,"event policy queries");
    check(m.foundation.effective_initials(StepId(1))[0].value==3,"initial Add query");
    check(!m.foundation.effective_initials(StepId(2))[0].valid && m.foundation.effective_initials(StepId(3)).empty(),"initial replacement missingness/remove");
    check(m.foundation.element_active(ElementId(1),StepId(0)) && !m.foundation.element_active(ElementId(1),StepId(2)) && m.foundation.element_active(ElementId(1),StepId(3)),"activation propagation");
    check(m.step_id("S3")==StepId(2) && m.step(StepId(2)).name=="S3" && m.material_id("M")==MaterialId(0) && m.surface_id("FACE")==SurfaceId(0),"typed boundary queries");
    rejects([&]{m.foundation.effective_loads(StepId(99));},"step");
    rejects([&]{m.foundation.property(ElementId(),"yield");},"element");
    StateDB<T> state(m.mesh);rejects([&]{TetPlan<T> plan(m.mesh,state);},"storage-only");rejects([&]{m.require_cpu();},"executable CPU");
}
template<class T> void contracts(const std::filesystem::path &scratch) {
    auto ir=fixture();auto m=ModelCompiler{}.compile<T>(ir);oracle(m);
    auto path=scratch/(sizeof(T)==4?"dbf3-float.sdbmodel":"dbf3-double.sdbmodel");ModelStore::save(path,m);auto loaded=ModelStore::load<T>(path);oracle(loaded);
    check(native::pack(ir)==native::pack(loaded.definition),"all schema4 fields roundtrip");
    auto changed=ir;std::reverse(changed.extensions.couplings.begin(),changed.extensions.couplings.end());std::reverse(changed.extensions.initial_events.begin(),changed.extensions.initial_events.end());
    auto reordered=ModelCompiler{}.compile<T>(changed);check(reordered.semantic_identity()==m.semantic_identity(),"extension canonical ordering");
    check(reordered.foundation.effective_initials(StepId(1))[0].value==3,"event ordering independent");
    changed=ir;changed.extensions.property_distributions[0].constant.real={16};auto other=ModelCompiler{}.compile<T>(changed);
    check(other.semantic_identity()!=m.semantic_identity() && other.mesh.fingerprint()!=m.mesh.fingerprint(),"property semantic identity");
    changed=ir;changed.extensions.amplitude_policies[0].extrapolation=ExtrapolationPolicy::Clamp;auto amplitude_other=ModelCompiler{}.compile<T>(changed);
    check(amplitude_other.analysis_identity()!=m.analysis_identity(),"amplitude policy analysis identity");
    auto bad=[&](auto mutate,const std::string &diagnostic=""){auto copy=ir;mutate(copy);rejects([&]{ModelCompiler{}.compile<T>(copy);},diagnostic);};
    bad([](auto &v){v.schema=3;},"schema 4");
    bad([](auto &v){v.extensions.topology_numbering_version=2;},"numbering");
    bad([](auto &v){v.extensions.sample_axis.version=2;},"sample axis");
    bad([](auto &v){v.extensions.couplings[0].reference.label=99;});
    bad([](auto &v){v.extensions.couplings[0].reference.label=1;},"reference in dependent");
    bad([](auto &v){v.extensions.couplings[0].id=ConstraintId(2);},"duplicate coupling");
    bad([](auto &v){v.extensions.couplings[0].weights[0].weight=0;},"weight");
    bad([](auto &v){v.extensions.couplings[0].weights.pop_back();},"every node");
    bad([](auto &v){v.extensions.couplings[0].weights[0].node.label=3;},"weight node");
    bad([](auto &v){v.extensions.couplings[0].weights.push_back(v.extensions.couplings[0].weights[0]);},"duplicate coupling weight");
    bad([](auto &v){v.extensions.couplings[1].surface="MISSING";},"surface");
    bad([](auto &v){v.extensions.couplings[1].weights={{{"","",1},1}};},"kinematic");
    bad([](auto &v){v.extensions.couplings[0].dof_mask=63;},"DOF");
    bad([](auto &v){v.extensions.amplitude_policies[0].time=AmplitudeTimeDomain::Total;},"amplitude policy");
    bad([](auto &v){v.extensions.amplitude_policies[0].amplitude="MISSING";},"amplitude policy");
    bad([](auto &v){v.extensions.amplitude_policies[1].extrapolation=ExtrapolationPolicy::Linear;},"amplitude policy");
    bad([](auto &v){v.extensions.amplitude_policies.push_back(v.extensions.amplitude_policies[0]);},"amplitude policy");
    bad([](auto &v){v.extensions.property_distributions[0].scope="MISSING";},"scope");
    bad([](auto &v){v.extensions.property_distributions[0].material="MISSING";},"material");
    bad([](auto &v){v.extensions.property_distributions[0].parameter="MISSING";},"parameter");
    bad([](auto &v){v.extensions.property_distributions[0].constant.real={101};},"bounds");
    bad([](auto &v){v.extensions.property_distributions[0].constant.units="other";},"descriptor");
    bad([](auto &v){v.extensions.property_distributions[1].region="LEFT";},"overlap");
    bad([](auto &v){v.extensions.property_distributions[1].table="MISSING";},"table descriptor");
    bad([](auto &v){v.extensions.property_distributions[0].orientation=OrientationId(99);},"orientation");
    bad([](auto &v){v.extensions.initial_events[0].step=StepId(99);},"step");
    bad([](auto &v){v.extensions.initial_events[0].condition.node.label=2;},"identity changed");
    bad([](auto &v){v.extensions.initial_events[0].condition.valid=false;},"valid active");
    bad([](auto &v){v.extensions.initial_events[0].condition.id=11;},"valid active");
    bad([](auto &v){v.extensions.initial_events.push_back(v.extensions.initial_events[0]);},"duplicate initial event");
    bad([](auto &v){v.extensions.activation_policies[0].activation=20;},"activation policy");
    // A frozen pre-schema4 encoding oracle explicitly lists the old tail boundary.
    for(std::uint32_t schema=1;schema<=3;++schema) {
        ModelIR legacy;legacy.schema=schema;legacy.parts={IRPart{}};
        if(schema>=3) legacy.foundation.coordinates=ir.foundation.coordinates;
        native::Writer w;
        auto emit=[&](const auto &...values){(native::encode(w,values),...);};
        emit(legacy.schema,legacy.units,legacy.assembly,legacy.mode,legacy.parts,legacy.instances,legacy.sets,legacy.materials,legacy.sections,legacy.surfaces,legacy.amplitudes,legacy.initial_boundaries,legacy.steps,legacy.opaque);
        if(schema>=2) emit(legacy.history_bindings);
        if(schema>=3) emit(legacy.foundation);
        check(w.bytes==native::pack(legacy),"schema1/2/3 frozen bytes");
        auto bytes=w.bytes;bytes[0]=9;rejects([&]{native::unpack<ModelIR>(bytes);},"schema");
    }
}
void parser(const std::filesystem::path &directory) {
    auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(directory/"coupling_load.inp").model);
    check(m.definition.schema==4 && m.foundation.couplings.size()==1,"controlled coupling parse");
    check(m.foundation.effective_loads(StepId(1)).size()==1 && m.foundation.definition.face_loads[1].pressure==8,"DSLOAD modifies stable identity");
    check(m.foundation.effective_initials(StepId(1))[0].value==3,"initial velocity parsed");
    auto id=m.instance_id("A");check(m.instance(id).part=="P","typed instance query");
    std::ifstream input(directory/"coupling_load.inp");std::string text{std::istreambuf_iterator<char>(input),{}};
    auto bad=[&](const std::string &from,const std::string &to,const std::string &diagnostic) {
        auto changed=text;auto at=changed.find(from);if(at==changed.npos) throw std::runtime_error("bad fixture edit");changed.replace(at,from.size(),to);
        std::istringstream stream(changed);rejects([&]{auto ir=AbaqusInpReader{}.read(stream,directory/"negative.inp",{}).model;ModelCompiler{}.compile<double>(ir);},diagnostic);
    };
    bad("REF NODE=A.5","REF NODE=MISSING","node sets");
    bad("*KINEMATIC","*DISTRIBUTING","KINEMATIC");
    bad("1,3\n*END ASSEMBLY","1,6\n*END ASSEMBLY","DOFs 1..3");
    bad("*DSLOAD,OP=MOD","*DSLOAD,OP=NEW","OP=MOD");
    bad("A.FACE,P,4","A.FACE,PNU,4","pressure P");
    bad("TYPE=VELOCITY","TYPE=STRESS","VELOCITY");
    bad("SURFACE=A.FACE","SURFACE=A.FACE,INFLUENCE RADIUS=2","unsupported parameter");
}
template<class T> native::Bytes mechanics(const StateDB<T> &state) {
    native::Writer w;
    state.data().fields([&](const auto &field) {
        using F=std::decay_t<decltype(field)>;
        native::encode(w,static_cast<std::uint64_t>(field.size()));
        const auto view=field.read();
        for(std::size_t n=0;n<field.size();++n) for(unsigned c=0;c<F::components;++c) native::encode(w,view(n,c));
    });
    native::encode(w,state.data().step);native::encode(w,state.data().time);native::encode(w,state.data().dt);
    return std::move(w.bytes);
}
template<class T> std::uint64_t legacy_configuration(const CpuRun<T> &run) {
    native::Writer w;
    native::encode(w,std::string("cpu/tet4/linear/cd-v1/serial/deterministic"));
    native::encode(w,run.plans());native::encode(w,std::uint8_t(sizeof(T)));
    return native::hash(w.bytes);
}
template<class T> void executable_amplitude_regression(const std::filesystem::path &fixtures,const std::filesystem::path &scratch) {
    auto original=ReaderRegistry{}.read(fixtures.parent_path()/"abaqus"/"forced_tet.inp").model;
    original.amplitudes={{"STEP",false,{{0,0},{.2,2}}, {}}};
    original.steps[0].loads[0].actions[0].amplitude="STEP";
    auto single=ModelCompiler{}.compile<T>(original);CpuRun<T> single_run(single);
    check(single_run.configuration()==legacy_configuration(single_run),"single-step configuration identity unchanged");
    auto ir=original;
    auto step2=ir.steps[0];step2.name="SECOND";step2.period=.1;step2.loads.clear();ir.steps.push_back(step2);
    auto step3=ir.steps[0];step3.name="REDEFINED";step3.period=.1;ir.steps.push_back(step3);
    auto step4=step2;step4.name="PROPAGATED-AGAIN";ir.steps.push_back(step4);
    const auto wire=native::pack(ir);auto model=ModelCompiler{}.compile<T>(ir);
    check(model.report.executable_cpu && native::pack(model.definition)==wire,"runtime provenance preserves executable ModelIR bytes");
    const auto node=model.sources.node_id({"","",40}).value();StateDB<T> state(model.mesh);
    auto force=[&](std::size_t step,double local,double total) {
        LoadEvaluator::apply(model.analysis,step,local,total,state);return state.data().external_force.read()(node,0);
    };
    check(force(0,.2,.2)==T(2),"step amplitude first endpoint");
    check(force(1,0,.2)==T(2) && force(1,.05,.25)==T(2),"inherited executable step amplitude holds endpoint");
    check(force(2,0,.3)==T(0) && force(2,.05,.35)==T(.5),"explicit redefinition restarts local amplitude");
    check(force(3,0,.4)==T(1),"propagation uses latest explicit origin period");
    check(model.analysis.steps[1].loads[0].origin_step==0 && model.analysis.steps[3].loads[0].origin_step==2,"runtime action origin provenance");
    auto invalid=model.analysis;invalid.steps[1].loads[0].origin_step=2;
    rejects([&]{LoadEvaluator::apply(invalid,1,0,.2,state);},"origin step");
    auto manual=model.analysis;manual.steps[1].loads[0].origin_step=UINT64_MAX;
    LoadEvaluator::apply(manual,1,.05,.25,state);check(state.data().external_force.read()(node,0)==T(.5),"manual unspecified origin retains local-time behavior");
    auto total_ir=ir;total_ir.amplitudes[0].total_time=true;total_ir.amplitudes[0].points={{0,0},{1,10}};
    auto total=ModelCompiler{}.compile<T>(total_ir);StateDB<T> total_state(total.mesh);CpuRun<T> total_run(total);
    LoadEvaluator::apply(total.analysis,1,0,.2,total_state);check(total_state.data().external_force.read()(node,0)==T(2),"total-time amplitude at transition");
    LoadEvaluator::apply(total.analysis,1,.05,.25,total_state);check(total_state.data().external_force.read()(node,0)==T(2.5),"inherited total-time amplitude continues");
    check(total_run.configuration()==legacy_configuration(total_run),"total-time-only configuration identity unchanged");
    auto reset_ir=ir;reset_ir.steps.resize(2);reset_ir.steps[1].loads={{true,{}, {}}};
    auto reset=ModelCompiler{}.compile<T>(reset_ir);StateDB<T> reset_state(reset.mesh);CpuRun<T> reset_run(reset);
    LoadEvaluator::apply(reset.analysis,1,0,.2,reset_state);check(reset_state.data().external_force.read()(node,0)==T(0),"OP NEW still removes inherited amplitude load");
    check(reset_run.configuration()==legacy_configuration(reset_run),"removed amplitude configuration identity unchanged");
    CpuRun<T> continuous(model);continuous.initialize();
    check(continuous.configuration()!=legacy_configuration(continuous),"affected multistep configuration rejects old solver semantics");
    for(std::uint64_t i=0;i<continuous.plans()[0].increments;++i) continuous.advance();
    const auto tag=sizeof(T)==4?"float":"double";
    auto boundary=scratch/(std::string(tag)+"-amplitude-boundary.sdbrst");continuous.save(boundary);
    CpuRun<T> resumed(model);resumed.restore(boundary);
    check(mechanics(continuous.state())==mechanics(resumed.state()),"boundary checkpoint restores identical mechanical state");
    continuous.advance();resumed.advance();
    check(continuous.step_index()==1 && continuous.state().data().external_force.read()(node,0)==T(2),"real CpuRun transition holds step-time load");
    auto middle=scratch/(std::string(tag)+"-amplitude-middle.sdbrst");continuous.save(middle);resumed.save(scratch/(std::string(tag)+"-amplitude-resumed.sdbrst"));
    check(mechanics(continuous.state())==mechanics(resumed.state()),"first transition increment checkpoint split equality");
    CpuRun<T> middle_resumed(model);middle_resumed.restore(middle);
    while(continuous.advance()) {}
    while(resumed.advance()) {}
    while(middle_resumed.advance()) {}
    continuous.save(scratch/(std::string(tag)+"-amplitude-final.sdbrst"));
    resumed.save(scratch/(std::string(tag)+"-amplitude-final-resumed.sdbrst"));
    middle_resumed.save(scratch/(std::string(tag)+"-amplitude-final-middle.sdbrst"));
    check(mechanics(continuous.state())==mechanics(resumed.state()) && mechanics(continuous.state())==mechanics(middle_resumed.state()),"real multistep continuous and both restart splits are bitwise equal");
    RestartInfo old_info;old_info.configuration=legacy_configuration(continuous);old_info.step=continuous.step_index();old_info.increment=continuous.increment();
    auto old_path=scratch/(std::string(tag)+"-old-amplitude-configuration.sdbrst");RestartDB::save(old_path,model,continuous.state(),old_info);
    auto before=mechanics(resumed.state());auto cursor=std::make_pair(resumed.step_index(),resumed.increment());
    rejects([&]{resumed.restore(old_path);},"configuration mismatch");
    check(mechanics(resumed.state())==before && std::make_pair(resumed.step_index(),resumed.increment())==cursor,"old amplitude solver configuration rejection is atomic");
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--read") {
            oracle(ModelStore::load<float>(std::filesystem::path(argv[2])/"dbf3-float.sdbmodel"));
            oracle(ModelStore::load<double>(std::filesystem::path(argv[2])/"dbf3-double.sdbmodel"));
        } else {
            if(argc!=3) throw std::invalid_argument("usage: dbf3_model_tests <fixtures> <scratch> | --read <scratch>");
            std::filesystem::create_directories(argv[2]);contracts<float>(argv[2]);contracts<double>(argv[2]);parser(argv[1]);
            executable_amplitude_regression<float>(argv[1],argv[2]);executable_amplitude_regression<double>(argv[1],argv[2]);
        }
        std::cout<<checks<<" DBF3 model checks passed\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
