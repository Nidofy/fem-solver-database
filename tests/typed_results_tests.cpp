#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <sdb/results/export.hpp>
#include <iostream>
using namespace sdb;
using namespace sdb::lifecycle;
static unsigned checks = 0;
void check(bool b, const char *s) { ++checks; if (!b) throw std::runtime_error(s); }
template<class F> void rejects(F f) {
    ++checks; try { f(); } catch (const std::exception &) { return; }
    throw std::runtime_error("expected rejection");
}
template<class T> FieldDescriptor schema(std::string name, Association a=Association::Element,
    FieldLocation l=FieldLocation::IntegrationPoint) {
    FieldDescriptor d; d.name=std::move(name); d.scalar=scalar_type<T>(); d.association=a; d.location=l;
    return d;
}
template<class T> void scalar_field(ResultFrame &f,std::string name,ResultAddress a,T value) {
    f.fields.push_back(make_typed_field<T>(schema<T>(name),{a},{value}));
}
template<class T> void history_snapshot(const std::filesystem::path &fixture) {
    auto ir=ReaderRegistry{}.read(fixture).model;
    ir.schema=2;
    HistorySchema h; h.id="typed-test.history";
    HistoryVariable v; v.field=schema<T>("damage"); v.initial={.25}; h.variables={v};
    ir.history_bindings.push_back({ir.materials.at(0).name,1,h});
    auto model=ModelCompiler{}.compile<T>(ir);
    StateDB<T> state(model.mesh);
    auto before=state.data().history.read()(0);
    auto frame=ResultPublisher::snapshot_history(model,state);
    check(typed_values<T>(frame.fields.at(0)).at(0)==T(.25),"history scalar preserved");
    check(frame.fields[0].typed->schema_id==h.id,"history schema preserved");
    check(state.data().history.read()(0)==before,"history snapshot read only");
    state.data().step=8; state.data().time=.75;
    auto timed=ResultPublisher::snapshot_history(model,state);
    check(timed.info.id==8 && timed.info.increment==8 && timed.info.time==.75,"default committed snapshot clock");
    state.begin_trial();
    rejects([&]{ResultPublisher::snapshot_history(model,state);});
    state.rollback();
    rejects([&]{ResultPublisher::snapshot_history(model,state,{}, {"missing"});});
    rejects([&]{ResultPublisher::snapshot_history(model,state,{}, {},1);});
    ir.history_bindings[0].history_schema.variables[0].field.basis="LOCAL FIBER";
    auto local=ModelCompiler{}.compile<T>(ir); StateDB<T> local_state(local.mesh);
    rejects([&]{ResultPublisher::snapshot_history(local,local_state);});
}
int main(int argc,char **argv) {
    try {
        check(argc==3,"fixture scratch arguments");
        std::filesystem::path dir=argv[2]; std::filesystem::create_directories(dir);
        auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
        CpuRun<double> run(model);
        auto legacy=ResultModel::from(model,run.configuration());
        auto rm=legacy;
        rm.samples.resize(rm.elements.size());
        for (std::size_t e=0;e<rm.samples.size();++e)
            for (unsigned layer=1;layer<=300;++layer) rm.samples[e].push_back({1,layer,1,0});
        rm.bases.push_back({7,{0,-1,0,1,0,0,0,0,1}});
        ResultFrame f; f.info.id=1; bind_result_frame(f,rm);
        ResultAddress a; a.entity=1; a.integration_point=1; a.layer=1; a.section_point=1;
        scalar_field<float>(f,"F32",a,1.25f);
        scalar_field<double>(f,"F64",a,1.0/3.0);
        scalar_field<std::int32_t>(f,"I32",a,INT32_MIN);
        scalar_field<std::uint32_t>(f,"U32",a,UINT32_MAX);
        scalar_field<std::int64_t>(f,"I64",a,INT64_MIN);
        scalar_field<std::uint64_t>(f,"U64",a,UINT64_MAX);
        scalar_field<std::uint8_t>(f,"U8",a,255);
        auto sd=schema<std::uint64_t>("MASK"); sd.shape={2}; sd.components={"A","B"}; sd.missing=MissingPolicy::ValidityMask;
        auto mask=make_typed_field<std::uint64_t>(sd,{a},{0,UINT64_MAX},{0,1});
        mask.typed->basis_id=7; f.fields.push_back(mask);
        std::vector<ResultAddress> addresses; std::vector<std::uint64_t> values;
        for(unsigned layer=300;layer;--layer) {
            auto b=a; b.layer=layer; addresses.push_back(b); values.push_back(UINT64_MAX-layer);
        }
        f.fields.push_back(make_typed_field<std::uint64_t>(schema<std::uint64_t>("LAYERS"),addresses,values));
        ResultAddress contact; contact.domain=1; contact.contact_generation=5; contact.interaction=1;
        contact.contact_point=1; contact.endpoint_a={0,1,0,0}; contact.endpoint_b={2,1,1,1};
        f.fields.push_back(make_typed_field<std::uint64_t>(schema<std::uint64_t>("CONTACT",Association::Contact,FieldLocation::Entity),{contact},{UINT64_MAX}));
        auto h1=make_typed_field<double>(schema<double>("SHARED"),{a},{2.0});
        auto h2=make_typed_field<double>(schema<double>("SHARED"),{a},{3.0});
        h1.typed->schema_id="material-A"; h2.typed->schema_id="material-B";
        f.fields.push_back(h1); f.fields.push_back(h2);
        validate_frame(f,rm,{});
        rejects([&]{native::pack(f.fields[0]);});
        rejects([&]{make_typed_field<std::uint64_t>(sd,{a},{1,2},{0,1});});
        rejects([&]{make_typed_field<std::uint64_t>(sd,{a},{0,2},{2,1});});
        rejects([&]{make_typed_field<std::uint64_t>(schema<std::uint64_t>("D"),{a,a},{1,2});});
        auto bad=f; bad.fields[0].typed->addresses[0].layer=301; rejects([&]{validate_frame(bad,rm,{});});
        bad=f; bad.fields[0].typed->schema.basis="LOCAL FIBER"; bad.fields[0].descriptor.basis="LOCAL FIBER";
        rejects([&]{validate_frame(bad,rm,{});});
        bad.fields[0].typed->basis_id=7; validate_frame(bad,rm,{});
        bad=f; bad.fields[0].typed->basis_id=999; rejects([&]{validate_frame(bad,rm,{});});
        bad=f; bad.fields[9].typed->addresses[0].contact_generation=0; rejects([&]{validate_frame(bad,rm,{});});
        auto path=dir/"typed.sdbres"; std::filesystem::remove(path);
        {
            NativeResultStorage w(path,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
            auto before=std::filesystem::file_size(path);
            bad=f; bad.binding->definition^=1; rejects([&]{w.append(bad);});
            check(std::filesystem::file_size(path)==before,"binding rejection before frame bytes");
            w.append(f);
        }
        auto mismatch=*f.fields[0].typed;
        mismatch.schema.scalar=ScalarType::UInt64;
        rejects([&]{native::pack(mismatch);});
        check(mismatch.values.index()==0 && std::get<std::vector<float>>(mismatch.values)[0]==1.25f,
            "rejected encode cannot mutate variant");
        ResultReader reader(path);
        check(reader.format()==ResultFormat::TypedIndexed3,"format dispatch");
        check(reader.read_stats().decoded_value_bytes==0,"open never decodes values");
        check(reader.model.samples[0].size()==300,"sample model roundtrip");
        check(typed_values<std::uint64_t>(reader.typed_field(0,"U64"))[0]==UINT64_MAX,"u64 exact");
        check(typed_values<std::int64_t>(reader.typed_field(0,"I64"))[0]==INT64_MIN,"i64 exact");
        check(typed_values<std::uint32_t>(reader.typed_field(0,"U32"))[0]==UINT32_MAX,"u32 exact");
        check(typed_values<std::int32_t>(reader.typed_field(0,"I32"))[0]==INT32_MIN,"i32 exact");
        check(typed_values<std::uint8_t>(reader.typed_field(0,"U8"))[0]==255,"u8 exact");
        check(typed_values<float>(reader.typed_field(0,"F32"))[0]==1.25f,"f32 exact");
        check(typed_values<double>(reader.typed_field(0,"F64"))[0]==1.0/3.0,"f64 exact");
        rejects([&]{reader.typed_field(0,"SHARED");});
        check(typed_values<double>(reader.typed_field(0,"SHARED",a,{},"material-B"))[0]==3.0,
            "exact material schema selective query");
        auto got=reader.typed_field(0,"MASK");
        check(got.typed->validity==std::vector<std::uint8_t>({0,1}) && got.typed->basis_id==7,"mask/basis roundtrip");
        auto one=reader.typed_field(0,"LAYERS",a);
        check(one.typed->addresses.size()==1 && typed_values<std::uint64_t>(one)[0]==UINT64_MAX-1,"full address selective");
        ResultReader bounded(path,false,60000);
        rejects([&]{bounded.typed_field(0,"LAYERS",{},Id(1));});
        auto last=a; last.layer=300;
        check(bounded.typed_field(0,"LAYERS",last).typed->addresses.size()==1,"bounded selective last block");
        auto all=reader.typed_field(0,"LAYERS",{},Id(1));
        check(all.typed->addresses.size()==300,"entity query includes every layer/block");
        for(unsigned i=0;i<300;++i) check(all.typed->addresses[i].layer==i+1,"full address ordering");
        auto invalid=ResultQuery::typed_history<std::uint64_t>(reader,"MASK",a);
        check(invalid.size()==1 && !invalid[0].second,"missing distinct from zero");
        check(ResultQuery::typed_history<std::uint64_t>(reader,"U64",a)[0].second==UINT64_MAX,"history no double conversion");
        auto next=f; next.info.id=2; next.info.increment=1; next.info.time=1; bind_result_frame(next,rm);
        next.fields[9].typed->addresses[0].contact_generation=6;
        {
            NativeResultStorage w(path,rm,true,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
            auto budget=AsyncResultWriter::required_bytes(next);
            AsyncResultWriter async(w,budget);
            async.write(std::move(next)); async.close();
            check(async.stats().committed==1 && async.stats().peak_bytes<=budget,"async typed bounded ownership");
        }
        ResultReader r2(path);
        check(r2.typed_field(1,"CONTACT",contact).typed->addresses.empty(),"stale generation no alias");
        check(ResultQuery::typed_history<std::uint64_t>(r2,"CONTACT",contact).size()==1,"stable contact history generation");
        rejects([&]{NativeResultStorage w(path,rm,true,false,native::default_limit,{},false,ResultFormat::Indexed2);});
        auto exportdir=dir/"refused-export";
        rejects([&]{ResultExporter::vtk(r2,exportdir);});
        check(!std::filesystem::exists(exportdir),"export fails before any output");
        rejects([&]{ResultExporter::xdmf(r2,dir/"refused.xdmf");});
        check(!std::filesystem::exists(dir/"refused.xdmf"),"xdmf preflight");
        auto broken=dir/"truncated.sdbres";
        std::filesystem::copy_file(path,broken,std::filesystem::copy_options::overwrite_existing);
        std::filesystem::resize_file(broken,std::filesystem::file_size(broken)-1);
        rejects([&]{ResultReader badreader(broken);});
        check(ResultReader(broken,true).index.size()==1,"typed committed recovery prefix");
        auto corrupt=dir/"corrupt.sdbres";
        std::filesystem::copy_file(path,corrupt,std::filesystem::copy_options::overwrite_existing);
        {std::fstream io(corrupt,std::ios::in|std::ios::out|std::ios::binary); io.seekg(-20,std::ios::end); char c; io.get(c); io.seekp(-20,std::ios::end); io.put(char(c^1));}
        rejects([&]{ResultReader badreader(corrupt,true);});
        for(bool indexed : {false,true}) {
            auto lp=dir/(indexed?"legacy2.sdbres":"legacy1.sdbres"); std::filesystem::remove(lp);
            NativeResultStorage w(lp,legacy,false,false,native::default_limit,{},indexed);
            auto size=std::filesystem::file_size(lp);
            rejects([&]{w.append(f);}); check(std::filesystem::file_size(lp)==size,"legacy refusal before bytes");
            w.append(run.snapshot());
        }
        history_snapshot<float>(argv[1]); history_snapshot<double>(argv[1]);
        std::cout<<"typed results checks="<<checks<<" PASS\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
