#include <sdb/lifecycle.hpp>
#include <iostream>
#include <chrono>
using namespace sdb;
using namespace sdb::lifecycle;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F> void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected rejection");}
int main(int argc,char **argv){try{
    if(argc!=4)throw std::invalid_argument("fixture directory frames");
    std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    auto count=std::stoull(argv[3]);if(count<20)throw std::invalid_argument("frames >=20");
    auto model=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model);
    CpuRun<double> run(model);auto rm=ResultModel::from(model,run.configuration());
    auto f=run.snapshot();f.fields.resize(1);f.fields[0].ids={1};f.fields[0].values={0,0,0};
    auto path=dir/"range.sdbres";std::filesystem::remove(path);
    auto start=std::chrono::steady_clock::now();
    {NativeResultStorage w(path,rm,false,false,native::default_limit,{},true);
     for(std::uint64_t i=0;i<count;++i){f.info.id=i;f.info.increment=i;f.info.time=double(i);f.fields[0].values[0]=double(i);w.append(f);}}
    auto written=std::chrono::steady_clock::now();ResultReader reader(path);auto open=reader.read_stats();
    check(open.bytes<16384,"bounded indexed open");
    auto bounds=reader.time_interval(double(count-10),double(count-1));
    check(bounds.first==count-10 && bounds.second==count,"tail bounds");
    check(reader.time_interval(-10,-1)==std::pair<std::size_t,std::size_t>{0,0},"empty interval");
    check(reader.time_interval(1,1)==std::pair<std::size_t,std::size_t>{1,2},"closed endpoint");
    rejects([&]{reader.time_interval(2,1);});rejects([&]{reader.time_interval(0,NAN);});
    std::size_t seen=0;
    auto visited=ResultQuery::visit_history(reader,f.fields[0].descriptor.name,1,[&](double t,double v){check(t==v,"history value");return ++seen<3;},0,"",false,double(count-10),double(count-1));
    check(visited==3,"stream cancellation");
    auto values=ResultQuery::history(reader,f.fields[0].descriptor.name,1,0,"",false,double(count-10),double(count-1),1,10);
    check(values.size()==10 && values.front().second==double(count-10),"tail values");
    rejects([&]{ResultQuery::history(reader,f.fields[0].descriptor.name,1,0,"",false,0,10,1,2);});
    auto stats=reader.read_stats();check(stats.bytes-open.bytes<200000,"tail query bounded I/O independent of prefix length");
    std::cout<<"frames="<<count<<" file_bytes="<<std::filesystem::file_size(path)
             <<" write_ms="<<std::chrono::duration<double,std::milli>(written-start).count()
             <<" open_bytes="<<open.bytes<<" query_bytes="<<stats.bytes-open.bytes<<" PASS\n";
    auto typed_path=dir/"typed.sdbres";std::filesystem::remove(typed_path);
    FieldDescriptor schema;schema.name="EXACT";schema.scalar=ScalarType::UInt64;
    schema.association=Association::Node;schema.location=FieldLocation::Entity;
    ResultAddress address;address.entity=1;
    ResultFrame typed;typed.fields.push_back(make_typed_field<std::uint64_t>(schema,{address},{UINT64_MAX}));
    {NativeResultStorage w(typed_path,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3);
     for(unsigned i=0;i<20;++i){typed.info.id=i;typed.info.increment=i;typed.info.time=i;bind_result_frame(typed,rm);w.append(typed);}}
    ResultReader tr(typed_path);std::size_t exact=0;
    ResultQuery::visit_typed_history<std::uint64_t>(tr,"EXACT",address,[&](double t,std::optional<std::uint64_t> v){
        check(t>=17 && t<=19 && v==UINT64_MAX,"exact typed stream");++exact;return true;},0,17,19);
    check(exact==3,"typed tail interval");
    rejects([&]{ResultQuery::typed_history<std::uint64_t>(tr,"EXACT",address,0,17,19,"",{},2);});
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
