#include <sdb/results/async_writer.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks=0;
void check(bool b,const char *message) {++checks;if(!b) throw std::runtime_error(message);}
template<class F> void rejects(F f) {++checks;try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected rejection at check "+std::to_string(checks));}
ModelIR fixture() {
    ModelIR ir;ir.schema=3;ir.mode=ImportMode::MeshOnly;
    IRPart p;p.name="P";
    p.nodes={{1,{0,0,0},{}},{2,{1,0,0},{}},{3,{1,1,0},{}},{4,{0,1,0},{}}};
    p.elements={{1,descriptor("S4R",{}),{1,2,3,4},{}}};ir.parts={p};
    ir.instances={{"A","P",{0,0,0},{0,0,0,0,0,1,0},{}},
        {"B","P",{10,0,0},{0,0,0,0,0,1,90},{}}};
    ir.sets={{"P","ALL","",false,{"1"},{}}};ir.materials={{"M",1,100,.25,{}}};
    HistorySchema history;history.id="material.local-history";
    HistoryVariable variable;variable.field.name="HV";variable.field.scalar=ScalarType::Float64;
    variable.field.shape={3};variable.field.components={"X","Y","Z"};variable.field.basis="FIBER";
    variable.initial={1,2,3};history.variables={variable};ir.history_bindings={{"M",101,history}};
    auto &f=ir.foundation;
    f.coordinates={{CoordinateId(1),CoordinateId(0),"PARENT",{0,0,0},{0,-1,0,1,0,0,0,0,1},{}},
        {CoordinateId(2),CoordinateId(1),"CHILD",{1,0,0},identity_rotation,{}}};
    f.orientations={{OrientationId(7),"FIBER",CoordinateId(2),identity_rotation,{}}};
    IRShellSection shell;shell.assignment={SectionId(2),"P","ALL","M",OrientationId(7),{}};
    shell.thickness=2;shell.layers={{"M",1,0,1,{}},{"M",1,90,1,{}}};f.shell_sections={shell};
    return ir;
}
ResultAddress address(Id entity,unsigned layer) {
    ResultAddress a;a.entity=entity;a.integration_point=1;a.layer=layer;a.section_point=1;return a;
}
void read_oracle(const std::filesystem::path &path) {
    ResultReader reader(path);
    check(reader.index.size()==1,"independent frame count");
    const std::array<Rotation,4> expected{{
        {0,-1,0,1,0,0,0,0,1},{-1,0,0,0,-1,0,0,0,1},
        {-1,0,0,0,-1,0,0,0,1},{0,1,0,-1,0,0,0,0,1}}};
    for(Id entity=1;entity<=2;++entity) for(unsigned layer=1;layer<=2;++layer) {
        auto value=reader.typed_field(FrameId(0),"HV",address(entity,layer),{},"material.local-history");
        check(typed_values<double>(value)==std::vector<double>({1,2,3}),"local values retained, not transformed");
        auto basis=value.typed->basis_id;
        auto found=std::find_if(reader.model.bases.begin(),reader.model.bases.end(),[&](const auto &b){return b.id==basis;});
        check(found!=reader.model.bases.end(),"local basis catalog reference");
        const auto &oracle=expected[(entity-1)*2+layer-1];
        for(unsigned i=0;i<9;++i) check(std::abs(found->rotation[i]-oracle[i])<1e-12,"independent composed matrix oracle");
        auto group=reader.typed_field(0,"HV",{},{},"material.local-history",{},basis);
        check(group.typed->addresses.size()==1,"explicit basis group selection");
    }
    auto counter=reader.typed_field(0,"COUNTER");
    check(typed_values<std::uint64_t>(counter)[0]==UINT64_MAX,"registered uint64 exact");
    check(typed_values<std::uint64_t>(counter)[1]==0 && counter.typed->validity[1]==0,"registered missingness exact");
    auto ip=reader.typed_field(0,"IPFLAG",address(2,2));
    check(typed_values<std::int64_t>(ip)[0]==INT64_MIN && ip.typed->addresses[0]==address(2,2),"registered sample address/lane");
    check(reader.frame(FrameId(0)).info.frame_id()==FrameId(0),"strong frame lookup");
    check(reader.frame(FrameId(0)).info.sample_axis().kind==SampleAxisKind::Time,"physical sample axis");
    rejects([&]{reader.frame(FrameId(9));});
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="read") {read_oracle(argv[2]);std::cout<<"DBF3 independent reader "<<checks<<" PASS\n";return 0;}
        check(argc==2,"scratch argument");std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
        auto axis_fixture=fixture();axis_fixture.schema=4;axis_fixture.extensions.sample_axis.units="s";
        auto axis_model=ModelCompiler{}.compile<double>(axis_fixture);
        rejects([&]{ResultModel::from(axis_model,0);});
        StateDB<double> axis_state(axis_model.mesh);
        rejects([&]{ResultPublisher::snapshot_history(axis_model,axis_state);});
        auto model=ModelCompiler{}.compile<double>(fixture());StateDB<double> state(model.mesh);
        FieldDescriptor d;d.name="COUNTER";d.scalar=ScalarType::UInt64;d.association=Association::Node;
        d.location=FieldLocation::Entity;d.missing=MissingPolicy::ValidityMask;
        state.register_field<Field<std::uint64_t>>(d,model.mesh.nodes(),"registered.counter",1,{UINT64_MAX});
        state.registered_field<Field<std::uint64_t>>("COUNTER").write()(1)=0;
        state.registered_validity("COUNTER").write()(1)=0;
        d.name="IPFLAG";d.scalar=ScalarType::Int64;d.association=Association::Element;
        d.location=FieldLocation::IntegrationPoint;d.missing=MissingPolicy::Forbidden;
        state.register_field<Field<std::int64_t>>(d,state.ip_offsets().back(),"registered.ip",1,{INT64_MIN});
        state.freeze_registered_fields();
        auto frame=ResultPublisher::snapshot_history(model,state);
        check(frame.fields.size()==4,"split material basis groups");
        ResultPublisher::append_registered(frame,model,state);
        check(frame.fields.size()==6,"registered and material histories share pipeline");
        auto rm=ResultModel::from(model,0);
        validate_frame(frame,rm,{});
        rejects([&]{ResultPublisher::snapshot_registered(model,state,{}, {},1);});
        rejects([&]{ResultPublisher::snapshot_history(model,state,{}, {},1);});
        state.begin_trial();
        rejects([&]{ResultPublisher::snapshot_registered(model,state);});
        rejects([&]{ResultPublisher::snapshot_history(model,state);});state.rollback();
        auto missing=fixture();missing.foundation.shell_sections[0].assignment.orientation=OrientationId{};
        auto no_basis=ModelCompiler{}.compile<double>(missing);StateDB<double> no_basis_state(no_basis.mesh);
        rejects([&]{ResultPublisher::snapshot_history(no_basis,no_basis_state);});
        auto wrong=fixture();wrong.history_bindings[0].history_schema.variables[0].field.basis="UNBOUND AXES";
        auto wrong_model=ModelCompiler{}.compile<double>(wrong);StateDB<double> wrong_state(wrong_model.mesh);
        rejects([&]{ResultPublisher::snapshot_history(wrong_model,wrong_state);});
        StateDB<double> bad_registered(model.mesh);
        d.name="BAD";d.basis="LOCAL";
        bad_registered.register_field<Field<std::int64_t>>(d,bad_registered.ip_offsets().back(),"bad",1,{0},123456);
        bad_registered.freeze_registered_fields();rejects([&]{ResultPublisher::snapshot_registered(model,bad_registered);});
        const auto path=dir/"dbf3-results.sdbres";std::filesystem::remove(path);
        {
            NativeResultStorage storage(path,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
            auto charge=AsyncResultWriter::required_bytes(frame);
            auto denied=frame;
            AsyncResultWriter too_small(storage,AsyncResultWriter::required_bytes(denied)-1);
            rejects([&]{too_small.write(std::move(denied));});
            check(denied.fields.size()==6,"preadmission retains snapshot ownership");too_small.close();
            AsyncResultWriter writer(storage,charge);
            writer.write(std::move(frame));
            state.data().history.write()(0)=99;
            state.registered_field<Field<std::uint64_t>>("COUNTER").write()(0)=7;
            writer.close();check(writer.stats().peak_bytes<=charge,"async ownership budget");
        }
        read_oracle(path);
        auto r=ResultReader(path);auto changed=r.frame(0);changed.info.id=1;changed.info.increment=1;changed.info.time=1;
        bind_result_frame(changed,rm);changed.fields[0].typed->basis_id=8;
        // Keep the independent process fixture single-frame, separate from drift fixture.
        auto drift=dir/"basis-drift.sdbres";std::filesystem::copy_file(path,drift,std::filesystem::copy_options::overwrite_existing);
        {NativeResultStorage storage(drift,rm,true);storage.append(changed);}
        rejects([&]{ResultQuery::typed_history<double>(ResultReader(drift),"HV",address(1,1),0,0,2,"material.local-history");});
        std::cout<<"DBF3 result checks="<<checks<<" PASS\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
