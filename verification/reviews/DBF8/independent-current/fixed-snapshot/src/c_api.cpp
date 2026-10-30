#include <sdb/c_api.h>
#include <sdb/results/results.hpp>
#include <cstring>
struct sdb_reader { sdb::lifecycle::ResultReader value; explicit sdb_reader(const std::filesystem::path &p):value(p){} };
namespace {
thread_local char error[1024]{};
template<class F> int call(F f) noexcept {
    try {f();error[0]=0;return 0;}
    catch(const std::exception &e){std::strncpy(error,e.what(),sizeof(error)-1);error[sizeof(error)-1]=0;}
    catch(...){std::strcpy(error,"unknown C++ exception");}
    return -1;
}
void require(bool b){if(!b)throw std::invalid_argument("null/invalid C API argument");}
}
extern "C" {
uint32_t sdb_abi_version(void){return 1;}
const char *sdb_last_error(void){return error;}
int sdb_reader_open(const char *path,sdb_reader **out){return call([&]{require(path && out);auto p=std::make_unique<sdb_reader>(std::filesystem::path(reinterpret_cast<const char8_t*>(path)));*out=p.release();});}
void sdb_reader_close(sdb_reader *reader){delete reader;}
int sdb_reader_frame_count(sdb_reader *r,uint64_t *out){return call([&]{require(r && out);*out=r->value.index.size();});}
int sdb_reader_time(sdb_reader *r,uint64_t frame,double *out){return call([&]{require(r && out);if(frame>=r->value.index.size())throw std::out_of_range("frame");*out=r->value.index[frame].time;});}
int sdb_reader_history_f64(sdb_reader *r,const char *field,uint64_t entity,uint32_t component,double begin,double end,
    double *times,double *values,size_t capacity,size_t *count){return call([&]{
    require(r && field && count && (!capacity || (times && values)));
    auto out=sdb::lifecycle::ResultQuery::history(r->value,field,entity,component,"",false,begin,end,1,capacity);
    for(size_t i=0;i<out.size();++i){times[i]=out[i].first;values[i]=out[i].second;}*count=out.size();
});}
int sdb_reader_entity_u64(sdb_reader *r,uint64_t frame,const char *field,uint64_t entity,uint32_t component,uint64_t *value,uint8_t *valid){return call([&]{
    require(r && field && value && valid);if(frame>=r->value.index.size())throw std::out_of_range("frame");
    sdb::lifecycle::ResultAddress address;address.entity=entity;
    auto f=r->value.typed_field(static_cast<size_t>(frame),field,address);
    if(f.typed->addresses.empty() || component>=f.typed->schema.component_count())throw std::out_of_range("typed entity/component");
    auto v=sdb::lifecycle::typed_values<uint64_t>(f).at(component);
    auto mask=f.typed->validity.empty()?uint8_t(1):f.typed->validity.at(component);
    *value=v;*valid=mask;
});}
}
