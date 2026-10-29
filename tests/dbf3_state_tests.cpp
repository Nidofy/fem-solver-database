#include <sdb/lifecycle.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static_assert(std::is_same_v<decltype(std::declval<const RegisteredState&>().entries()[0]), const RegisteredFieldBase*>);
static unsigned checks = 0;
void check(bool ok, const char *text) { ++checks; if (!ok) throw std::runtime_error(text); }
template<class F> void rejects(F f) {
    ++checks; try { f(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected rejection");
}
FieldDescriptor descriptor(std::string name, ScalarType type, RestartPolicy policy=RestartPolicy::Required) {
    FieldDescriptor d; d.name=std::move(name); d.scalar=type;
    d.association=Association::Node; d.location=FieldLocation::Entity; d.restart=policy;
    if (policy==RestartPolicy::Omit) d.persistence=Persistence::Transient;
    return d;
}
template<class T> void register_fields(StateDB<T> &s, std::string id="synthetic-storage-fixture.state") {
    s.template register_field<Field<std::uint64_t>>(descriptor("exact",ScalarType::UInt64),
        s.data().displacement.size(),id,1,{9007199254740993ull});
    s.template register_field<Field<std::int64_t>>(descriptor("signed",ScalarType::Int64),
        s.data().displacement.size(),id,1,{-9007199254740993ll});
    s.template register_field<Field<T>>(descriptor("rebuild",scalar_type<T>(),RestartPolicy::Reconstruct),
        s.data().displacement.size(),id,1,{T(.25)});
    s.template register_field<Field<std::uint8_t>>(descriptor("transient",ScalarType::UInt8,RestartPolicy::Omit),
        s.data().displacement.size(),id,1,{7});
    auto tensor=descriptor("vector",scalar_type<T>()); tensor.shape={3}; tensor.components={"X","Y","Z"};
    tensor.missing=MissingPolicy::ValidityMask;
    s.template register_field<Field<T,3,AoSoA<4>>>(tensor,s.data().displacement.size(),id,1,{T(1),T(2),T(3)});
    s.freeze_registered_fields();
}
native::Bytes read_file(const std::filesystem::path &p) {
    std::ifstream in(p,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}};
}
void write_file(const std::filesystem::path &p, const native::Bytes &bytes) {
    std::ofstream out(p,std::ios::binary); native::write(out,bytes);
}
template<class T> void contracts(const std::filesystem::path &fixture,const std::filesystem::path &dir) {
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    StateDB<T> s(model.mesh), plain(model.mesh);
    check(!plain.registered_fields(),"default state extensions allocate nothing");
    auto alloc=host_memory.allocations.load(); plain.freeze_registered_fields();
    check(host_memory.allocations.load()==alloc && !plain.registered_fields(),"default freeze allocates nothing");
    register_fields(s);
    auto &exact=s.template registered_field<Field<std::uint64_t>>("exact");
    check(exact.read()(0)==9007199254740993ull,"exact uint64 initializer");
    check(s.registered_fields()->frozen(),"frozen registry");
    rejects([&] { s.template register_field<Field<T>>(descriptor("late",scalar_type<T>()),model.mesh.nodes(),"late"); });
    rejects([&] { s.template registered_field<Field<std::int64_t>>("exact"); });
    auto view=exact.checked_read();
    s.begin_trial(); s.template registered_field<Field<std::uint64_t>>("exact").write()(0)=UINT64_MAX;
    s.template registered_field<Field<T,3,AoSoA<4>>>("vector").write()(0,0)=T(11);
    s.rollback();
    check(s.template registered_field<Field<std::uint64_t>>("exact").read()(0)==9007199254740993ull,"extension deep rollback");
    rejects([&] { view.at(0); });
    s.begin_trial(); s.template registered_field<Field<std::uint64_t>>("exact").write()(0)=UINT64_MAX;
    s.commit(); check(s.template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX,"extension commit");
    s.template registered_field<Field<T>>("rebuild").fill(T(8));
    s.template registered_field<Field<std::uint8_t>>("transient").fill(255);
    s.template registered_field<Field<T,3,AoSoA<4>>>("vector").write()(0,1)=0;
    s.registered_validity("vector").write()(1)=0;
    s.data().dt=.01;
    auto rst=dir/(sizeof(T)==4 ? "float.sdbrst":"double.sdbrst");
    auto chk=dir/(sizeof(T)==4 ? "float.chk":"double.chk");
    RestartInfo info; info.configuration=123;
    RestartDB::save(rst,model,s,info); save_checkpoint(chk,model.mesh,s);
    std::filesystem::copy_file(rst,dir/(sizeof(T)==4 ? "registered-f.sdbrst" : "registered-d.sdbrst"),std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(chk,dir/(sizeof(T)==4 ? "registered-f.chk" : "registered-d.chk"),std::filesystem::copy_options::overwrite_existing);
    StateDB<T> out(model.mesh); register_fields(out);
    out.data().displacement.fill(T(19));
    auto before=out.data().displacement.checked_read();
    RestartDB::load(rst,model,out,123);
    check(out.template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX,"restart exact uint64");
    check(out.template registered_field<Field<std::int64_t>>("signed").read()(0)==-9007199254740993ll,"restart exact signed int64");
    check(out.template registered_field<Field<T>>("rebuild").read()(0)==T(.25),"restart reconstruct initialization");
    check(out.template registered_field<Field<std::uint8_t>>("transient").read()(0)==7,"restart transient initialization");
    check(out.registered_fields()->find("vector").validity().at(1)==0,"restart validity");
    rejects([&] { before.at(0); });
    out.template registered_field<Field<std::uint64_t>>("exact").fill(0);
    load_checkpoint(chk,model.mesh,out);
    check(out.template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX,"checkpoint exact uint64");
    check(out.template registered_field<Field<T>>("rebuild").read()(0)==T(.25),"checkpoint reconstruct initialization");
    StateDB<T> different(model.mesh); register_fields(different,"synthetic-storage-fixture.other");
    rejects([&] { RestartDB::load(rst,model,different,123); });
    rejects([&] { load_checkpoint(chk,model.mesh,different); });
    rejects([&] { RestartDB::load(rst,model,plain,123); });
    rejects([&] { load_checkpoint(chk,model.mesh,plain); });
    // Late corruption after ordinary fields and extension values must publish nothing.
    for (bool lifecycle : {false,true}) {
        auto path=lifecycle?rst:chk; auto bytes=read_file(path); bytes.pop_back();
        auto bad=dir/"bad-state.bin"; write_file(bad,bytes);
        out.data().displacement.fill(T(37));
        out.template registered_field<Field<std::uint64_t>>("exact").fill(123456789123456789ull);
        auto stable=out.template registered_field<Field<std::uint64_t>>("exact").checked_read();
        rejects([&] { if(lifecycle) RestartDB::load(bad,model,out,123); else load_checkpoint(bad,model.mesh,out); });
        check(out.data().displacement.read()(0)==T(37) && stable.at(0)==123456789123456789ull,"failed restore atomic extension and mechanics");
        rejects([&] { if(lifecycle) RestartDB::load(path,model,out,123,8); else load_checkpoint(path,model.mesh,out,nullptr,8); });
    }
    // Existing schema bytes and readers stay unchanged without extensions.
    plain.data().dt=.01; RestartDB::save(rst,model,plain,info); save_checkpoint(chk,model.mesh,plain);
    std::ifstream in(rst,std::ios::binary); native::prefix(in,restart_magic);
    check(native::unpack<RestartInfo>(native::record(in,0)).state_schema==3,"legacy restart schema retained");
    auto raw=read_file(chk); checkpoint_detail::Reader reader{raw}; reader.integer<std::uint64_t>();
    auto h=reader.chunk(0); check(h.integer<std::uint32_t>()==2,"legacy checkpoint schema retained");
    StateDB<T> legacy(model.mesh); RestartDB::load(rst,model,legacy,123); load_checkpoint(chk,model.mesh,legacy);
    // Dense addressing is explicit; no count-based reinterpretation of unsupported locations.
    StateDB<T> bad(model.mesh);
    auto d=descriptor("bad",scalar_type<T>()); d.association=Association::Face;
    rejects([&] { bad.template register_field<Field<T>>(d,1,"bad"); });
    d=descriptor("bad",scalar_type<T>());
    rejects([&] { bad.template register_field<Field<T>>(d,model.mesh.nodes()+1,"bad"); });
    d.scalar=ScalarType::UInt64;
    rejects([&] { bad.template register_field<Field<T>>(d,model.mesh.nodes(),"bad"); });
    check(!bad.registered_fields(),"failed first registration preserves default state");
    bad.validate_shape();
    s.registered_validity("vector").write()(1)=2;
    rejects([&] { s.registered_fields()->validate(); });
}
template<class T> void cpu_run_contracts(const std::filesystem::path &fixture,const std::filesystem::path &dir) {
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    CpuRun<T> source(model), restored(model);
    register_fields(source.state()); register_fields(restored.state());
    for(int i=0;i<3;++i) source.advance();
    source.state().template registered_field<Field<std::uint64_t>>("exact").fill(UINT64_MAX-1);
    auto path=dir/(sizeof(T)==4?"run-f.sdbrst":"run-d.sdbrst");
    source.save(path); restored.restore(path);
    check(restored.increment()==source.increment(),"CpuRun cursor restored");
    check(restored.state().template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX-1,"CpuRun extension restored");
    source.advance(); restored.advance();
    for(Index n=0;n<model.mesh.nodes();++n) for(unsigned c=0;c<3;++c)
        check(source.state().data().displacement.read()(n,c)==restored.state().data().displacement.read()(n,c),"CpuRun segmented displacement");
    check(restored.state().template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX-1,"CpuRun forces preserve extension");
    // Valid checksummed file, invalid cursor: complete decode precedes this failure.
    std::ifstream in(path,std::ios::binary); native::prefix(in,restart_magic);
    auto info=native::unpack<RestartInfo>(native::record(in,0)); info.step=9999;
    native::Bytes tail{std::istreambuf_iterator<char>(in),{}};
    auto bad=dir/"bad-cursor.sdbrst";
    native::atomic_stream(bad,[&](auto &out) { native::prefix(out,restart_magic); native::record(out,0,native::pack(info)); native::write(out,tail); });
    restored.state().template registered_field<Field<std::uint64_t>>("exact").fill(987654321987654321ull);
    auto view=restored.state().template registered_field<Field<std::uint64_t>>("exact").checked_read();
    const auto increment=restored.increment();
    rejects([&] { restored.restore(bad); });
    check(view.at(0)==987654321987654321ull && restored.increment()==increment,"CpuRun cursor rejection atomicity");
    CpuRun<T> legacy(model); for(int i=0;i<3;++i) legacy.advance();
    auto v2=dir/"legacy-run.chk"; save_checkpoint(v2,model.mesh,legacy.state());
    rejects([&] { restored.import_v2(v2); });
    check(view.at(0)==987654321987654321ull,"v2 import preserves required extensions");
    CpuRun<T> optional(model);
    optional.state().template register_field<Field<T>>(descriptor("cache",scalar_type<T>(),RestartPolicy::Reconstruct),
        model.mesh.nodes(),"cache",1,{T(.5)});
    optional.state().freeze_registered_fields();
    optional.state().template registered_field<Field<T>>("cache").fill(T(9));
    optional.import_v2(v2);
    check(optional.state().template registered_field<Field<T>>("cache").read()(0)==T(.5),"v2 import reconstructs optional extensions");
}
template<class T> void read_registered(const std::filesystem::path &fixture,const std::filesystem::path &dir) {
    auto model=ModelCompiler{}.compile<T>(ReaderRegistry{}.read(fixture).model);
    for(bool checkpoint:{false,true}) {
        StateDB<T> state(model.mesh); register_fields(state);
        auto stem=sizeof(T)==4 ? "registered-f" : "registered-d";
        if(checkpoint) load_checkpoint(dir/(std::string(stem)+".chk"),model.mesh,state);
        else RestartDB::load(dir/(std::string(stem)+".sdbrst"),model,state,123);
        check(state.template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX,"fresh process uint64");
        check(state.template registered_field<Field<std::int64_t>>("signed").read()(0)==-9007199254740993ll,"fresh process int64");
        check(state.template registered_field<Field<T>>("rebuild").read()(0)==T(.25),"fresh process reconstruction");
        check(state.template registered_field<Field<std::uint8_t>>("transient").read()(0)==7,"fresh process omitted reset");
        check(state.registered_fields()->find("vector").validity().at(1)==0,"fresh process validity");
        check(state.template registered_field<Field<T,3,AoSoA<4>>>("vector").read()(0,2)==T(3),"fresh process typed layout");
    }
    CpuRun<T> run(model); register_fields(run.state());
    run.restore(dir/(sizeof(T)==4?"run-f.sdbrst":"run-d.sdbrst"));
    check(run.state().template registered_field<Field<std::uint64_t>>("exact").read()(0)==UINT64_MAX-1,"fresh process CpuRun restore");
    run.advance();
}
int main(int argc,char **argv) {
    try {
        if(argc==4 && std::string(argv[1])=="--read") {
            read_registered<float>(argv[2],argv[3]); read_registered<double>(argv[2],argv[3]);
            std::cout<<checks<<" DBF3 fresh process state checks passed\n"; return 0;
        }
        if(argc!=3) throw std::runtime_error("fixture scratch expected");
        std::filesystem::create_directories(argv[2]);
        contracts<float>(argv[1],argv[2]); contracts<double>(argv[1],argv[2]);
        cpu_run_contracts<float>(argv[1],argv[2]); cpu_run_contracts<double>(argv[1],argv[2]);
        std::cout<<checks<<" DBF3 registered state checks passed\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
