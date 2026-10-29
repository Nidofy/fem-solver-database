#include <sdb/lifecycle.hpp>
#include <sdb/results/async_writer.hpp>
#include <atomic>
#include <cstdlib>
#include <iostream>
using namespace sdb; using namespace sdb::lifecycle;
static std::atomic<bool> measuring=false;
static std::atomic<std::size_t> allocated=0;
void* operator new(std::size_t n) {if(measuring) allocated+=n; if(auto p=std::malloc(n))return p; throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
struct Sink:IResultStorage {void append(const ResultFrame&)override{} void flush()override{} std::optional<FrameInfo> last()const override{return {};}};
FieldDescriptor desc(){FieldDescriptor d;d.name="STRESS";d.shape={2};d.components={"XX","YY"};d.units="Pa";return d;}
int main(int argc,char**argv){try{
 auto m=ModelCompiler{}.compile<double>(ReaderRegistry{}.read(argv[1]).model); auto rm=ResultModel::from(m,42);
 ResultAddress a;a.entity=1;a.integration_point=1;
 auto d=desc(); auto field=make_typed_field<double>(d,{a},{10,20});field.typed->schema_id="material.stress";
 ResultFrame f;f.fields={field};bind_result_frame(f,rm);
 auto conflict=field;conflict.typed->schema.units="MPa";f.fields.push_back(conflict);
 bool accepted=true;try{validate_frame(f,rm,{});}catch(...){accepted=false;}
 std::cout<<"same-frame contradictory same-schema descriptors accepted="<<accepted<<'\n';
 f.fields={field};
 auto path=std::filesystem::path(argv[2])/"schema-drift.sdbres";
 std::filesystem::remove(path);
 {NativeResultStorage w(path,rm,false,false,native::default_limit,{},false,ResultFormat::TypedIndexed3); w.append(f);
 f.info.id=1;f.info.increment=1;f.info.time=1;bind_result_frame(f,rm);
 f.fields[0].typed->schema.components={"YY","XX"};f.fields[0].descriptor.components={"YY","XX"};
 f.fields[0].typed->values=std::vector<double>{200,100}; w.append(f);}
 ResultReader r(path);auto h=ResultQuery::typed_history<double>(r,"STRESS",a,0,0,2,"material.stress");
 std::cout<<"component0 history mixes XX then YY: "<<h.at(0).second.value()<<','<<h.at(1).second.value()<<'\n';
 ResultFrame big;auto one=d;one.shape={1};one.components={"VALUE"};
 std::vector<ResultAddress> addresses(100000);for(std::size_t k=0;k<addresses.size();++k)addresses[k].entity=k+1;
 big.fields.push_back(make_typed_field<double>(one,std::move(addresses),std::vector<double>(100000)));
 Sink sink;AsyncResultWriter async(sink,1024);allocated=0;measuring=true;std::string diagnostic;
 try{async.write(std::move(big));}catch(const std::exception&e){diagnostic=e.what();}
 measuring=false;std::cout<<"1024-byte admission rejection allocated="<<allocated<<" diagnostic="<<diagnostic<<'\n';async.close();
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
