#include "sdb/consumer_contract.hpp"
#include "sdb/io/foundation_store.hpp"
#include "sdb/data_contract.hpp"
#include "sdb/lifecycle.hpp"
#include "sdb/implicit.hpp"
#include <iostream>
#include <numeric>
#include <thread>

using namespace sdb;
namespace v1=sdb::consumer::v1;
namespace n=sdb::lifecycle::native;
static int checks=0;
void check(bool value,const char *message) { ++checks; if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    ++checks; try { f(); } catch(const std::exception &) { return; } throw std::runtime_error("expected rejection");
}
DofSpaceData definition() {
    DofSpaceData d; d.model=42; d.nodes=2; d.elements=1;
    d.variables={{1,3,DofQuantity::Translation,"u","m","global"},
                 {2,1,DofQuantity::Temperature,"temperature","K","scalar"},
                 {3,1,DofQuantity::Pressure,"pressure","Pa","scalar"},
                 {4,1,DofQuantity::Multiplier,"lambda","N","scalar"}};
    d.bindings={
        {{DofOwner::Node,0,0,1,0},DofStatus::Free,1,0,{}},
        {{DofOwner::Node,1,0,1,0},DofStatus::Free,0,0,{}},
        {{DofOwner::Node,0,0,2,0},DofStatus::Free,2,0,{}},
        {{DofOwner::Element,0,1,3,0},DofStatus::Free,3,0,{}},
        {{DofOwner::Global,0,0,4,0},DofStatus::Free,4,0,{}},
        {{DofOwner::Node,1,0,2,0},DofStatus::Prescribed,no_equation,333,{}},
        {{DofOwner::Node,0,0,1,1},DofStatus::Eliminated,no_equation,1.5,{{0,2},{1,-1}}},
        {{DofOwner::Node,1,0,1,1},DofStatus::Inactive,no_equation,9,{}}};
    d.external_equations={101,505,99,444,888}; d.solver_to_canonical={4,2,0,3,1}; return d;
}
FieldDescriptor descriptor(std::string name,Association a=Association::Node,StorageKind storage=StorageKind::Dense,
                           RestartPolicy restart=RestartPolicy::Required) {
    FieldDescriptor d; d.name=std::move(name); d.association=a; d.location=FieldLocation::Entity;
    d.basis="GLOBAL"; d.storage=storage; d.restart=restart;
    if(restart==RestartPolicy::Omit) d.persistence=Persistence::Transient;
    return d;
}
StateLayout node_layout() {
    StateLayout l; l.domain=2; lifecycle::ResultAddress a; a.entity=1; l.addresses.push_back(a);
    a.entity=2; l.addresses.push_back(a); return l;
}
StateLayout contact_layout() {
    StateLayout l; l.domain=16; l.indices={2,9};
    lifecycle::ResultAddress a; a.domain=1; a.contact_point=1; a.interaction=11; a.contact_generation=1;
    a.endpoint_a={0,1,0,0}; a.endpoint_b={2,1,1,1}; l.addresses.push_back(a);
    a.contact_point=2; l.addresses.push_back(a); return l;
}
RegisteredState registry(const DofSpace &space,std::uint64_t consumer=0) {
    RegisteredState s; s.bind_context(space.data().model,space.identity(),consumer);
    s.add<Field<double>>(descriptor("temperature"),2,"thermal",1,{300},0,node_layout());
    s.add<Field<double>>(descriptor("flux"),2,"thermal",1,{0},0,node_layout());
    auto contact=descriptor("contact",Association::Contact,StorageKind::Sparse); contact.missing=MissingPolicy::ValidityMask;
    s.add<Field<double>>(contact,2,"contact",1,{0},0,contact_layout());
    auto local=descriptor("element_node",Association::Element,StorageKind::Ragged); local.location=FieldLocation::ElementNode;
    local.scalar=ScalarType::UInt64;
    StateLayout l; l.domain=1; l.offsets={0,2};
    lifecycle::ResultAddress a; a.entity=1; a.element_node=1; l.addresses.push_back(a); a.element_node=2; l.addresses.push_back(a);
    s.add<Field<std::uint64_t>>(local,2,"exact",1,{9007199254740993ull},0,l);
    s.add<Field<double>>(descriptor("rebuild",Association::Global,StorageKind::Dense,RestartPolicy::Reconstruct),1,"aux",1,{4},0);
    s.add<Field<double>>(descriptor("omit",Association::Global,StorageKind::Dense,RestartPolicy::Omit),1,"aux",1,{8},0);
    s.freeze(); return s;
}
v1::FieldRequirement requirement(const RegisteredState &s,std::string name) {
    const auto &e=s.find(name); return {e.descriptor(),e.schema_id(),e.schema_version(),e.basis_id(),e.count(),e.addressing()};
}
void dof_tests() {
    DofSpace d(definition()); std::vector<double> x{5,300,20,10,4};
    auto y=d.gather(x);
    check(y==std::vector<double>({4,20,300,10,5,333,-10.5,9}),"mixed DOF gather");
    check(d.solver_equation(0)==4 && d.slot({DofOwner::Element,0,1,3,0})==3,"DOF lookup");
    std::vector<double> f{1,2,3,4,5,6,7,8}; auto r=d.scatter(f);
    check(r==std::vector<double>({5,3,-5,4,15}),"transpose scatter");
    auto offset=d.gather(std::vector<double>(5,0)); double work=0;
    for(std::size_t i=0;i<y.size();++i) work+=f[i]*(y[i]-offset[i]);
    check(work==std::inner_product(x.begin(),x.end(),r.begin(),0.0),"virtual work identity");
    auto bytes=d.encode(); check(DofSpace::decode(bytes).encode()==bytes,"DOF wire roundtrip");
    rejects([&]{d.encode(8);}); rejects([&]{DofSpace::decode(bytes,8);});
    for(unsigned kind=0;kind<10;++kind) {
        auto bad=definition();
        switch(kind) {
        case 0: bad.schema=7; break;
        case 1: bad.bindings[1].equation=1; break;
        case 2: bad.bindings[0].key.entity=2; break;
        case 3: bad.bindings[6].terms[0].slot=6; break;
        case 4: bad.bindings[6].terms[1].slot=0; break;
        case 5: bad.solver_to_canonical[0]=2; break;
        case 6: bad.external_equations[0]=99; break;
        case 7: bad.variables[1].components=3; break;
        case 8: bad.bindings[5].equation=0; break;
        case 9: bad.bindings[6].terms[0].coefficient=std::numeric_limits<double>::infinity(); break;
        }
        rejects([&]{DofSpace test(bad);});
    }
    rejects([&]{d.gather(std::vector<double>{1});});
    auto overflow=x; overflow[4]=std::numeric_limits<double>::max(); rejects([&]{d.gather(overflow);});
    DataCoverage coverage; coverage.source_revision="fixture1"; coverage.consumer="standalone.test";
    coverage.entries={{"numbering","DofSpace","database","zero-based canonical, explicit permutation","mixed fixture",1,DataDisposition::Stored},
                      {"scratch","thread scratch","consumer","rebuild before batch","consumer test",1,DataDisposition::Reconstructed}};
    std::vector<std::string> keys{"numbering","scratch"}; coverage.validate(keys); check(true,"complete capture coverage");
    auto copy=n::unpack<DataCoverage>(n::pack(coverage)); copy.validate(keys);
    keys.push_back("history"); rejects([&]{coverage.validate(keys);}); keys.pop_back();
    coverage.entries[1].disposition=DataDisposition::Unsupported; rejects([&]{coverage.validate(keys);}); coverage.validate(keys,true);
}
void state_tests(const std::filesystem::path &dir) {
    DofSpace d(definition()); auto s=registry(d);
    auto old=s.get<Field<double>>("contact").checked_read();
    s.get<Field<double>>("contact").write()(0)=12; s.get<Field<double>>("contact").write()(1)=24;
    s.begin_trial();
    auto next=contact_layout(); next.indices={9,12}; next.addresses.erase(next.addresses.begin());
    auto fresh=next.addresses[0]; fresh.contact_generation=2; next.addresses.push_back(fresh);
    s.remap("contact",next);
    check(s.get<Field<double>>("contact").read()(0)==24 && s.get<Field<double>>("contact").read()(1)==0,"identity remap retain/reset");
    rejects([&]{old.at(0);}); rejects([&]{FoundationStore::save(dir/"trial.sdbf",d,s);});
    s.rollback(); check(s.get<Field<double>>("contact").read()(0)==12,"layout and values rollback");
    s.begin_trial(); s.remap("contact",next); s.commit(); check(s.find("contact").addressing()->indices==next.indices,"layout commit");
    s.remap("contact",contact_layout());
    s.get<Field<double>>("temperature").write()(1)=333;
    s.get<Field<double>>("rebuild").fill(99); s.get<Field<double>>("omit").fill(99);
    FoundationStore::save(dir/"foundation.sdbf",d,s);
    auto target=registry(d); auto before=target.get<Field<double>>("temperature").checked_read();
    FoundationStore::restore(dir/"foundation.sdbf",d,target);
    check(target.get<Field<double>>("temperature").read()(1)==333,"foundation restore values");
    check(target.get<Field<double>>("rebuild").read()(0)==4 && target.get<Field<double>>("omit").read()(0)==8,"restart policies");
    check(target.get<Field<std::uint64_t>>("element_node").read()(0)==9007199254740993ull,"exact ragged int64");
    rejects([&]{before.at(0);});
    std::filesystem::copy_file(dir/"foundation.sdbf",dir/"bad.sdbf",std::filesystem::copy_options::overwrite_existing);
    {std::ofstream out(dir/"bad.sdbf",std::ios::binary|std::ios::app); out.put('x');}
    target.get<Field<double>>("temperature").fill(456);
    auto live=target.get<Field<double>>("temperature").checked_read();
    rejects([&]{FoundationStore::restore(dir/"bad.sdbf",d,target);});
    check(live.at(0)==456,"failed restore preserves owner and values");
    rejects([&]{FoundationStore::restore(dir/"foundation.sdbf",d,target,16);});
    auto wrong=definition(); ++wrong.revision; DofSpace wrong_space(wrong); auto wrong_state=registry(wrong_space);
    rejects([&]{FoundationStore::restore(dir/"foundation.sdbf",wrong_space,wrong_state);});
    auto corrupt=contact_layout(); corrupt.indices={3,3}; rejects([&]{s.remap("contact",corrupt);});
    auto layout=node_layout(); layout.addresses[1]=layout.addresses[0]; rejects([&]{layout.validate(descriptor("x"));});
    auto global=descriptor("g",Association::Global); StateLayout gl; gl.domain=1; gl.addresses.resize(1); gl.validate(global);
    check(true,"global result identity is zero");
}
v1::BatchData batch_data(const DofSpace &d) {
    v1::BatchData b; b.model=d.data().model; b.equation_space=d.identity(); b.formulation="thermal-line";
    b.topology={"LINE2",1,2,{0},{},{{0,1}}}; b.elements={0}; b.connectivity={0,1};
    b.dof_offsets={0,2}; b.dof_slots={2,5}; b.sample_offsets={0,1};
    lifecycle::ResultAddress a; a.entity=1; a.integration_point=1; b.samples={a}; b.materials={0}; b.sections={0}; return b;
}
v1::MaterialCatalog materials() {
    lifecycle::MaterialDefinition m; m.material="thermal"; m.schema_id="conductivity";
    lifecycle::MaterialParameter p; p.name="k"; p.units="W/m/K"; p.real={2}; m.parameters={p}; return v1::MaterialCatalog({m});
}
v1::SectionCatalog sections() {
    v1::SectionDefinition s; s.name="bar"; s.schema="area";
    lifecycle::MaterialParameter p; p.name="area"; p.units="m2"; p.real={1}; s.parameters={p};
    return v1::SectionCatalog({s});
}
void consumer_tests(const std::filesystem::path &dir) {
    DofSpace d(definition()); auto b=batch_data(d);
    v1::Batch batch(b,d,materials(),sections()); batch.require("thermal-line",1,b.topology);
    auto s=registry(d,batch.identity());
    auto wire=batch.encode(); auto reloaded=v1::Batch::decode(wire);
    {std::ofstream out(dir/"batch.bin",std::ios::binary); n::write(out,wire);}
    check(reloaded.encode()==wire && reloaded.sections()[0].parameters[0].real[0]==1,"batch metadata roundtrip");
    rejects([&]{v1::Batch::decode(wire,8);});
    batch.require_material(0,"conductivity",1); batch.require_section(0,"area",1);
    rejects([&]{batch.require_section(0,"shell-layers",1);});
    check(batch.nodes(0).size()==2 && batch.slots(0)[1]==5,"generic batch no CpuExplicit");
    auto input=v1::bind_read<Field<double>>(s,batch,requirement(s,"temperature"));
    auto output=v1::bind_write<Field<double>>(s,batch,requirement(s,"flux"));
    rejects([&]{v1::bind_read<Field<double>>(s,d,requirement(s,"temperature"));});
    v1::AccessSet access; access.read(input); access.write(output);
    s.get<Field<double>>("temperature").write()(1)=333; access.validate();
    auto u=input.resolve(); auto f=output.resolve(); double k=batch.materials()[0].parameters[0].real[0];
    f(0)=k*(u(0)-u(1)); f(1)=-f(0);
    check(f(0)==-66 && f(1)==66,"external thermal consumer exact balance");
    std::array<double,4> worker_values{}; std::array<std::exception_ptr,4> failures{};
    std::vector<std::thread> workers;
    for(std::size_t worker=0;worker<worker_values.size();++worker) workers.emplace_back([&,worker] {
        try {
            auto private_state=registry(d,batch.identity());
            auto private_out=v1::bind_write<Field<double>>(private_state,batch,requirement(private_state,"flux"));
            v1::AccessSet work; work.read(input); work.write(private_out); work.validate();
            auto shared=input.resolve(); auto target=private_out.resolve();
            target(0)=k*(shared(0)-shared(1)); worker_values[worker]=target(0);
        } catch(...) { failures[worker]=std::current_exception(); }
    });
    for(auto &worker:workers) worker.join();
    for(std::size_t worker=0;worker<worker_values.size();++worker)
        check(!failures[worker] && worker_values[worker]==-66,"shared reads with private parallel outputs");
    auto owned=v1::snapshot(s,batch,requirement(s,"flux")); s.get<Field<double>>("flux").fill(0);
    rejects([&]{v1::snapshot(s,batch,requirement(s,"flux"),1);});
    check(std::get<std::vector<double>>(owned.values)[0]==-66,"owned consumer output");
    v1::AccessSet alias; alias.read(input); alias.write(v1::bind_write<Field<double>>(s,batch,requirement(s,"temperature")));
    rejects([&]{alias.validate();});
    v1::AccessSet duplicate; duplicate.write(output); duplicate.write(output); rejects([&]{duplicate.validate();});
    auto wrong=requirement(s,"temperature"); wrong.descriptor.units="C"; rejects([&]{v1::bind_read<Field<double>>(s,batch,wrong);});
    auto wrong_layout=requirement(s,"temperature"); std::swap(wrong_layout.addressing->addresses[0],wrong_layout.addressing->addresses[1]);
    rejects([&]{v1::bind_read<Field<double>>(s,batch,wrong_layout);});
    auto changed=b; changed.formulation_version=2; v1::Batch other(changed,d,materials(),sections());
    check(other.identity()!=batch.identity(),"consumer definition identity changes");
    rejects([&]{v1::bind_read<Field<double>>(s,other,requirement(s,"temperature"));});
    FoundationStore::save(dir/"batch-state.sdbf",d,s);
    auto mismatched=registry(d,other.identity());
    rejects([&]{FoundationStore::restore(dir/"batch-state.sdbf",d,mismatched);});
    rejects([&]{batch.require("mechanical-line",1,b.topology);});
    b.dof_slots={2,999}; rejects([&]{v1::Batch bad(b,d,materials(),sections());});
    s.get<Field<double>>("temperature").resize(2); rejects([&]{access.validate();});
}
void integrated(const std::filesystem::path &fixture,const std::filesystem::path &dir) {
    using namespace sdb::lifecycle;
    auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(fixture).model);
    DofMap mechanical(model.mesh); auto bridge=mechanical.contract(model.mesh);
    check(bridge.equations()==mechanical.size(),"legacy mechanical map exports general contract");
    StateDB<double> state(model.mesh);
    auto d=descriptor("local_values",Association::Element,StorageKind::Ragged); d.location=FieldLocation::ElementNode;
    StateLayout layout; layout.domain=1; layout.offsets={0,2};
    ResultAddress a; a.entity=1; a.element_node=1; layout.addresses.push_back(a); a.element_node=2; layout.addresses.push_back(a);
    state.register_addressed_field<Field<double>>(model.mesh,d,layout,"local",1,{7});
    auto contact=descriptor("contact_values",Association::Contact,StorageKind::Sparse);
    state.register_addressed_field<Field<double>>(model.mesh,contact,contact_layout(),"contact",1,{4});
    auto face=descriptor("face_values",Association::Face);
    StateLayout faces; faces.domain=1; ResultAddress fa; fa.entity=1; fa.face=1; fa.side=1; faces.addresses={fa};
    state.register_addressed_field<Field<double>>(model.mesh,face,faces,"face",1,{5});
    state.freeze_registered_fields();
    save_checkpoint(dir/"addressed.chk",model.mesh,state);
    state.registered_field<Field<double>>("local_values").fill(99); load_checkpoint(dir/"addressed.chk",model.mesh,state);
    check(state.registered_field<Field<double>>("local_values").read()(0)==7,"existing checkpoint explicit layout");
    auto frame=ResultPublisher::snapshot_registered(model,state);
    check(frame.fields.size()==3 && frame.fields[0].typed->addresses==layout.addresses,"existing result publisher explicit rows");
    auto path=dir/"addressed.sdbres";
    std::filesystem::remove(path); // only this test's explicitly named output; keep reruns independent
    { NativeResultStorage storage(path,ResultModel::from(model,0),false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3); storage.append(frame); }
    auto loaded=ResultReader(path).frame(0);
    check(loaded.fields.size()==3 && loaded.fields[1].typed->addresses==contact_layout().addresses,"contact sparse result file roundtrip");
    state.data().dt=.01;
    RestartInfo info; info.configuration=41; RestartDB::save(dir/"addressed.sdbrst",model,state,info);
    state.registered_field<Field<double>>("face_values").fill(99);
    RestartDB::load(dir/"addressed.sdbrst",model,state,41);
    check(state.registered_field<Field<double>>("face_values").read()(0)==5,"existing restart restores addressed face");
    StateDB<double> bad(model.mesh); auto invalid=layout; invalid.addresses[1].element_node=999;
    rejects([&]{bad.register_addressed_field<Field<double>>(model.mesh,d,invalid,"bad");});
    check(!bad.registered_fields(),"failed registration no state publication");
}
int main(int argc,char **argv) {
    try {
        if(argc<3) throw std::runtime_error("usage: dbf6_tests write|read directory [fixture]");
        std::filesystem::path dir=argv[2];
        if(std::string(argv[1])=="read") {
            DofSpace expected(definition()); auto s=registry(expected);
            auto actual=FoundationStore::inspect_space(dir/"foundation.sdbf"); check(actual.encode()==expected.encode(),"fresh reader DOFs");
            FoundationStore::restore(dir/"foundation.sdbf",expected,s);
            check(s.get<Field<double>>("temperature").read()(1)==333,"fresh reader state");
            std::ifstream in(dir/"batch.bin",std::ios::binary); n::Bytes bytes{std::istreambuf_iterator<char>(in),{}};
            auto batch=v1::Batch::decode(bytes); check(batch.materials()[0].parameters[0].real[0]==2,"fresh consumer metadata");
            auto consumer_state=registry(expected,batch.identity());
            FoundationStore::restore(dir/"batch-state.sdbf",expected,consumer_state);
            auto input=v1::bind_read<Field<double>>(consumer_state,batch,requirement(consumer_state,"temperature"));
            check(input.resolve()(1)==333,"fresh consumer metadata-state identity");
        } else {
            std::filesystem::create_directories(dir); dof_tests(); state_tests(dir); consumer_tests(dir);
            if(argc==4) integrated(argv[3],dir);
        }
        std::cout<<"DBF6 "<<checks<<" checks PASS\n"; return 0;
    } catch(const std::exception &e) { std::cerr<<"DBF6 failure after "<<checks<<": "<<e.what()<<'\n'; return 1; }
}
