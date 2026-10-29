#include <sdb/lifecycle.hpp>
#include <sdb/storage.hpp>
#include <functional>
#include <iostream>
#include <sstream>
using namespace sdb;
using namespace sdb::lifecycle;
static int checks = 0;
void test(bool b, const char *s) { ++checks; if (!b) throw std::runtime_error(s); }
template<class F> void refused(F f) { ++checks; try { f(); } catch(const std::exception&) { return; } throw std::runtime_error("expected refusal"); }
template<class T> native::Bytes snapshot(const StateDB<T>& s) {
    native::Writer w;
    w.scalar(s.data().time); w.scalar(s.data().dt); w.integer(s.data().step);
    s.data().fields([&](auto &f) { auto v=f.read(); w.integer<std::uint64_t>(f.size());
        for(std::size_t i=0;i<f.size();++i) for(std::size_t c=0;c<std::decay_t<decltype(f)>::components;++c) w.scalar(v(i,c)); });
    return w.bytes;
}
template<class T> HistorySchema schema() {
    HistorySchema s; s.id="independent-review";
    HistoryVariable a; a.field.name="alpha"; a.field.scalar=scalar_type<T>(); a.initial={.125};
    HistoryVariable b; b.field.name="tensor"; b.field.scalar=scalar_type<T>(); b.field.shape={3};
    b.field.components={"11","22","12"}; b.field.shear=ShearConvention::Tensor; b.initial={-1,2,3};
    s.variables={a,b}; return s;
}
template<class T> void model_tests(const std::filesystem::path& fixture, const std::filesystem::path& scratch) {
    auto ir=ReaderRegistry{}.read(fixture).model; ir.schema=2;
    ir.history_bindings.push_back({ir.materials[0].name,1,schema<T>()});
    auto m=ModelCompiler{}.compile<T>(ir); StateDB<T> s(m.mesh);
    s.data().dt=.001; s.data().time=.25; s.data().step=7;
    s.data().fields([](auto& f) { using V=typename std::decay_t<decltype(f)>::value_type;
        f.fill(V(1)); });
    s.data().history.fill(T(4));
    auto before=snapshot(s); auto mass=s.data().mass.checked_read();
    refused([&]{ CpuExplicit<T> cpu(m.mesh,s); });
    test(snapshot(s)==before && mass.at(0)==T(1),"execution refusal modified state/view");
    std::vector<std::function<void(HistorySchema&)>> mutate={
        [](auto& h){h.id+="changed";}, [](auto& h){++h.version;},
        [](auto& h){h.variables[0].field.name+="changed";},
        [](auto& h){h.variables[1].field.shape={1,3};},
        [](auto& h){h.variables[1].field.components[2]="21";},
        [](auto& h){h.variables[1].field.shear=ShearConvention::Engineering;},
        [](auto& h){h.variables[0].field.units="Pa";},
        [](auto& h){h.variables[0].field.basis="material-A";},
        [](auto& h){h.variables[0].field.configuration=Configuration::Reference;},
        [](auto& h){h.variables[0].initial[0]=.5;},
        [](auto& h){std::swap(h.variables[0],h.variables[1]);}
    };
    auto path=scratch/(sizeof(T)==4?"float.sdbrst":"double.sdbrst");
    RestartInfo info; info.configuration=42; RestartDB::save(path,m,s,info);
    auto low=scratch/(sizeof(T)==4?"float.chk":"double.chk"); save_checkpoint(low,m.mesh,s);
    for(auto& change:mutate) {
        auto otherir=ir; change(otherir.history_bindings[0].history_schema);
        auto other=ModelCompiler{}.compile<T>(otherir);
        test(other.mesh.fingerprint()!=m.mesh.fingerprint(),"metadata omitted from fingerprint");
        test(ModelStore::identity(other)!=ModelStore::identity(m),"metadata omitted from definition");
        StateDB<T> target(other.mesh); target.data().history.fill(T(19)); auto old=snapshot(target);
        refused([&]{RestartDB::load(path,other,target,42);});
        refused([&]{load_checkpoint(low,other.mesh,target);});
        test(snapshot(target)==old,"semantic mismatch changed state");
    }
    // Failure after all state fields were read, and failure in late history data.
    std::ifstream input(path,std::ios::binary); native::Bytes valid((std::istreambuf_iterator<char>(input)),{});
    for(int mode=0;mode<3;++mode) {
        auto corrupt=valid;
        if(mode==0) corrupt.pop_back();
        if(mode==1) corrupt[corrupt.size()-40]^=1;
        if(mode==2) corrupt.push_back(0);
        auto bad=scratch/(sizeof(T)==4?"float-corrupt.sdbrst":"double-corrupt.sdbrst");
        {std::ofstream out(bad,std::ios::binary); native::write(out,corrupt);}
        refused([&]{RestartDB::load(bad,m,s,42);});
        test(snapshot(s)==before && mass.at(0)==T(1),"late failure changed target/value view");
    }
    StateDB<T> restored(m.mesh); RestartDB::load(path,m,restored,42);
    test(snapshot(restored)==before,"successful restart full-field mismatch");
    auto modelpath=scratch/(sizeof(T)==4?"float.sdbmodel":"double.sdbmodel"); ModelStore::save(modelpath,m);
    auto reloaded=ModelStore::load<T>(modelpath); test(native::pack(reloaded.definition)==native::pack(ir),"model2 roundtrip");
    // Recompute envelope CRCs so manifest agreement, not CRC, must detect the mismatch.
    std::ifstream mi(modelpath,std::ios::binary); native::prefix(mi,model_magic);
    auto manifest=native::unpack<ModelManifest>(native::record(mi,0)); auto body=native::record(mi,1); manifest.schema=1;
    auto mismatch=scratch/(sizeof(T)==4?"float-manifest.sdbmodel":"double-manifest.sdbmodel");
    {std::ofstream out(mismatch,std::ios::binary); native::prefix(out,model_magic); native::record(out,0,native::pack(manifest)); native::record(out,1,body);}
    refused([&]{ModelStore::load<T>(mismatch);});
    ir.history_bindings[0].model=99001;
    auto unknown=ModelCompiler{}.compile<T>(ir); refused([&]{unknown.require_cpu();});
    StateDB<T> us(unknown.mesh); refused([&]{CpuExplicit<T> cpu(unknown.mesh,us);});
}
void wire_and_storage() {
    // Independent expected LE wire, without using the product Writer.
    native::Bytes bytes;
    auto put=[&](std::uint64_t x,int n){for(int i=0;i<n;++i)bytes.push_back(std::uint8_t(x>>(8*i)));};
    put(1,4); put(4,4); put(1,8); put(4,8); put(2,8); put(0,4); put(3,4);
    put(2,8); put(0,8); put(0x8000000000000001ull,8); put(2,8); put(1,1); put(0,1);
    using S=SparseField<std::int64_t,1,AoSoA<8>>;
    auto s=S::decode(bytes); test(s.encode()==bytes,"independent int64 sparse wire oracle");
    test(s.present(0)&&!s.present(1)&&!s.present(3),"zero/absent/invalid conflated");
    test(s.checked_read().at(1)==std::numeric_limits<std::int64_t>::min()+1,"int64 bits lost");
    for(std::uint64_t budget: {0ull,1ull,15ull}) refused([&]{S::decode(bytes,budget);});
    auto forge=bytes; for(int k=0;k<8;++k)forge[24+k]=255;
    auto alloc=host_memory.allocations.load(); refused([&]{S::decode(forge,64);});
    test(host_memory.allocations.load()==alloc,"invalid decode allocated field");
    refused([&]{s.encode(s.encoded_bytes()-1);});
    auto copy=s; auto source=s.checked_read(); auto target=copy.checked_read();
    copy=s; refused([&]{target.at(0);}); test(source.at(0)==0,"copy invalidates source");
    auto newtarget=copy.checked_read(); copy=std::move(s);
    refused([&]{source.at(0);}); refused([&]{newtarget.at(0);});
    test(copy.encode()==bytes,"move corrupted owner");
    RaggedData<std::uint64_t,2> r; r.offsets={0,0,1,1}; r.values={{{UINT64_MAX,0}}};
    using R=RaggedField<std::uint64_t,2,AoSoA<8>>;
    auto rag=R::from_data(r); auto seg=rag.segment(1); auto ragcopy=rag;
    test(ragcopy.segment(1).at(0)==UINT64_MAX,"unsigned value lost");
    rag=ragcopy; refused([&]{seg.at(0);});
    test(R::decode(rag.encode()).data().values==r.values,"unsigned ragged roundtrip");
    for(unsigned k=0;k<9;++k) {
        auto bad=bytes; bad.resize(k); refused([&]{S::decode(bad);});
    }
    auto bad=bytes; bad[0]=99; refused([&]{S::decode(bad);});
    bad=bytes; bad[5]=1; refused([&]{S::decode(bad);});
    native::Bytes unknown{99,0,0,0}; refused([&]{native::unpack<ModelIR>(unknown,0);});
    // Every descriptor enum has an explicit invalid-value gate.
    FieldDescriptor d; d.name="d";
    std::vector<std::function<void(FieldDescriptor&)>> corrupt={
       [](auto&x){x.scalar=ScalarType(255);},[](auto&x){x.association=Association(255);},
       [](auto&x){x.location=FieldLocation(255);},[](auto&x){x.shear=ShearConvention(255);},
       [](auto&x){x.configuration=Configuration(255);},[](auto&x){x.time=PhysicalTime(255);},
       [](auto&x){x.persistence=Persistence(255);},[](auto&x){x.restart=RestartPolicy(255);},
       [](auto&x){x.missing=MissingPolicy(255);},[](auto&x){x.storage=StorageKind(255);}};
    for(auto& f:corrupt){auto x=d;f(x);refused([&]{x.validate();});}
}
int main(int argc,char**argv) {
    try { if(argc!=3)throw std::runtime_error("fixture scratch required");
        std::filesystem::create_directories(argv[2]); wire_and_storage();
        model_tests<float>(argv[1],argv[2]); model_tests<double>(argv[1],argv[2]);
        std::cout<<"independent checks="<<checks<<" PASS\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
