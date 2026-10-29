// Synthetic storage samples only. None of these manually populated values is a
// shell/beam/contact numerical solution. The executable is also a fresh-process reader.
#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <fstream>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks=0;
void check(bool ok,const char *message) { ++checks; if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    ++checks; try { f(); } catch(const std::exception &) { return; }
    throw std::runtime_error("expected rejection");
}
template<class T> FieldDescriptor desc(std::string name,Association a,FieldLocation l=FieldLocation::Entity) {
    FieldDescriptor d; d.name=std::move(name); d.scalar=scalar_type<T>(); d.association=a; d.location=l;
    d.persistence=Persistence::Derived; d.restart=RestartPolicy::Omit; return d;
}
template<class T> void read_oracle(const std::filesystem::path &dir) {
    auto tag=sizeof(T)==4?"float":"double";
    auto m=ModelStore::load<T>(dir/(std::string(tag)+".sdbmodel"));
    StateDB<T> s(m.mesh,true);
    auto info=RestartDB::load(dir/(std::string(tag)+".sdbrst"),m,s,77);
    check(info.state_schema==4 && info.model_schema==3,"rotation/schema3 restart");
    check(s.data().history.read()(5)==T(19),"layered history restored");
    check(s.data().rotation->angle.read()(0,2)==T(.5) && s.data().rotation->torque.read()(0,1)==T(8),"rotation fields restored");
    check(s.data().element_active.read()(5)==0 && s.data().time==.5 && s.data().step==10,"activation and time restored");
    ResultReader r(dir/(std::string(tag)+".sdbres"));
    check(r.index.size()==2 && r.model.nodes.size()==32 && r.model.elements.size()==8,"mixed result model");
    check(r.model.nodes[0].key.label==r.model.nodes[16].key.label && r.model.nodes[0].key.instance!=r.model.nodes[16].key.instance,"repeated external labels preserved");
    auto plastic=r.typed_field(0,"plastic");
    check(plastic.typed->schema_id=="synthetic-storage-fixture.plastic","history descriptor persisted");
    ResultAddress a; a.entity=2;a.integration_point=1;a.layer=2;a.section_point=2;
    auto selected=r.typed_field(0,"plastic",a);
    check(selected.typed->addresses==std::vector<ResultAddress>{a} && typed_values<T>(selected)==std::vector<T>{T(2),T(19)},"full layer address/value oracle");
    auto tensor=r.typed_field(0,"S_LOCAL");
    check(tensor.typed->basis_id==8 && tensor.typed->schema.components==std::vector<std::string>{"XX","YY","ZZ","XY","XZ","YZ"},"tensor order and basis oracle");
    auto b=std::find_if(r.model.bases.begin(),r.model.bases.end(),[](const auto &v){return v.id==8;});
    check(b!=r.model.bases.end() && b->rotation==std::array<double,9>{0,-1,0,1,0,0,0,0,1},"resolved local basis oracle");
    auto flags=r.typed_field(0,"FLAGS");
    check(typed_values<std::uint64_t>(flags)==std::vector<std::uint64_t>{UINT64_MAX,0} && flags.typed->validity==std::vector<std::uint8_t>{1,0},"uint64 and missingness oracle");
    auto contact=r.typed_field(1,"CONTACT");
    check(contact.typed->addresses[0].contact_generation==4 && typed_values<double>(contact)[0]==2.5,"stable contact address persisted");
    check(typed_values<double>(r.typed_field(0,"GLOBAL"))[0]==42,"global value oracle");
    check(r.typed_field(0,"FACE").typed->addresses[0].side==1,"face side persisted");
    check(r.typed_field(0,"NODE").typed->addresses.size()==2,"sparse node samples");
    check(r.typed_field(0,"ELEMENT_NODE").typed->addresses[0].element_node==2,"element-node local index");
}
template<class T> void write_fixture(const std::filesystem::path &models,const std::filesystem::path &dir) {
    auto tag=std::string(sizeof(T)==4?"float":"double");
    auto m=ModelStore::load<T>(models/(tag+".sdbmodel"));
    ModelStore::save(dir/(tag+".sdbmodel"),m);
    StateDB<T> s(m.mesh,true);
    s.data().dt=.01;s.data().time=.5;s.data().step=10;
    s.data().history.write()(5)=T(19);
    s.data().element_active.write()(5)=0;
    s.data().rotation->angle.write()(0,2)=T(.5);
    s.data().rotation->torque.write()(0,1)=T(8);
    s.data().rotation->inertia.fill(T(2));s.data().rotation->inverse_inertia.fill(T(.5));
    auto rm=ResultModel::from(m,77);
    FrameInfo fi;fi.id=10;fi.increment=10;fi.time=.5;fi.local_time=.5;
    auto f=ResultPublisher::snapshot_history(m,s,fi,{},native::default_limit,77);
    auto node=desc<float>("NODE",Association::Node); node.storage=StorageKind::Sparse;
    ResultAddress n1,n2;n1.entity=1;n2.entity=17;
    f.fields.push_back(make_typed_field<float>(node,{n1,n2},{1.25f,-2.f}));
    auto flags=desc<std::uint64_t>("FLAGS",Association::Element);flags.missing=MissingPolicy::ValidityMask;
    ResultAddress e1,e2;e1.entity=1;e2.entity=5;
    f.fields.push_back(make_typed_field<std::uint64_t>(flags,{e1,e2},{UINT64_MAX,0},{1,0}));
    auto local=desc<double>("S_LOCAL",Association::Element,FieldLocation::SectionPoint);
    local.shape={6};local.components={"XX","YY","ZZ","XY","XZ","YZ"};local.shear=ShearConvention::Tensor;
    local.basis="FIBER";local.configuration=Configuration::Current;
    ResultAddress p;p.entity=2;p.integration_point=1;p.layer=2;p.section_point=2;
    auto tensor=make_typed_field<double>(local,{p},{1,2,3,4,5,6});tensor.typed->basis_id=8;f.fields.push_back(tensor);
    ResultAddress ip;ip.entity=4;ip.integration_point=8;
    f.fields.push_back(make_typed_field<float>(desc<float>("IP",Association::Element,FieldLocation::IntegrationPoint),{ip},{.125f}));
    ResultAddress face;face.entity=2;face.face=1;face.side=1;
    f.fields.push_back(make_typed_field<double>(desc<double>("FACE",Association::Face),{face},{4}));
    ResultAddress en;en.entity=3;en.element_node=2;
    f.fields.push_back(make_typed_field<std::int32_t>(desc<std::int32_t>("ELEMENT_NODE",Association::Element,FieldLocation::ElementNode),{en},{-7}));
    ResultAddress c;c.domain=1;c.contact_generation=3;c.interaction=11;c.contact_point=1;
    c.endpoint_a={0,1,0,0};c.endpoint_b={2,2,1,1};
    f.fields.push_back(make_typed_field<double>(desc<double>("CONTACT",Association::Contact),{c},{2.5}));
    f.fields.push_back(make_typed_field<double>(desc<double>("GLOBAL",Association::Global),{{}},{42}));
    auto result=dir/(tag+".sdbres");std::filesystem::remove(result);
    {
        NativeResultStorage storage(result,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
        AsyncResultWriter async(storage,8*1024*1024);
        auto next=f;
        async.write(std::move(f));
        next.info.id=11;next.info.step=1;next.info.increment=1;next.info.time=1;next.info.local_time=0;
        for(auto &field:next.fields) if(field.descriptor.name=="CONTACT") field.typed->addresses[0].contact_generation=4;
        bind_result_frame(next,rm);async.write(std::move(next));async.close();
    }
    check(s.data().history.read()(5)==T(19) && s.data().rotation->angle.read()(0,2)==T(.5),"publication never advances state");
    RestartInfo info;info.configuration=77;
    auto restart=dir/(tag+".sdbrst");RestartDB::save(restart,m,s,info);
    StateDB<T> translation(m.mesh);translation.data().history.write()(5)=T(99);
    rejects([&]{RestartDB::load(restart,m,translation,77);});
    check(translation.data().history.read()(5)==T(99),"rotation capability mismatch atomicity");
    auto bad=dir/(tag+"-truncated.sdbrst");
    std::filesystem::copy_file(restart,bad,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(bad,std::filesystem::file_size(bad)-3);
    StateDB<T> destination(m.mesh,true);destination.data().rotation->angle.fill(T(9));
    rejects([&]{RestartDB::load(bad,m,destination,77);});
    check(destination.data().rotation->angle.read()(0,2)==T(9),"truncated rotation restore atomicity");
    auto changed=m.definition;changed.foundation.shell_sections[0].offset=.1;
    auto other=ModelCompiler{}.compile<T>(changed);StateDB<T> incompatible(other.mesh,true);
    rejects([&]{RestartDB::load(restart,other,incompatible,77);});
    s.data().rotation->inverse_inertia.write()(0,0)=T(.25);
    RestartDB::save(bad,m,s,info);
    rejects([&]{RestartDB::load(bad,m,destination,77);});
    check(destination.data().rotation->angle.read()(0,2)==T(9),"inertia validation before publish");
    read_oracle<T>(dir);
}
int main(int argc,char **argv) {
    try {
        if(argc!=3) throw std::invalid_argument("usage: foundation_integration_tests <model-dir> <scratch> | --read <scratch>");
        if(std::string(argv[1])=="--legacy") {
            auto dir=std::filesystem::path(argv[2]);
            auto m=ModelStore::load<double>(dir/"schema2.sdbmodel");
            check(m.definition.schema==2 && !m.definition.history_bindings.empty(),"frozen schema2 decoded");
            ModelStore::save(dir/"schema2-resaved.sdbmodel",m);
            for(auto name:{"native1.sdbres","indexed2.sdbres"}) {
                // forced_tet has five nodes/two elements and frequency=100; a
                // stop-after=1 run publishes only its initial frame.
                ResultReader r(dir/name);check(r.index.size()==1,"frozen legacy result frames");
                auto f=r.field(0,"U");check(f.ids.size()==5 && f.values.size()==15,"frozen initial displacement shape");
                for(auto v:f.values) check(v==0,"frozen independent zero displacement oracle");
            }
        } else if(std::string(argv[1])=="--read") {read_oracle<float>(argv[2]);read_oracle<double>(argv[2]);}
        else {std::filesystem::create_directories(argv[2]);write_fixture<float>(argv[1],argv[2]);write_fixture<double>(argv[1],argv[2]);}
        std::cout<<checks<<" synthetic storage integration checks passed\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
