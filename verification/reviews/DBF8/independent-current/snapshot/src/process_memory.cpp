#include <sdb/process_memory.hpp>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <new>
#include <stdexcept>

namespace {
std::atomic_flag gate=ATOMIC_FLAG_INIT;
sdb::process_memory::Stats counters{std::numeric_limits<std::size_t>::max(),0,0,0,0};
struct Lock {
    Lock() noexcept {while(gate.test_and_set(std::memory_order_acquire)) {}}
    ~Lock(){gate.clear(std::memory_order_release);}
};
struct Header {void *base;std::size_t charged;};
void release(std::size_t n) noexcept {Lock lock;counters.live-=n;}
void *attempt(std::size_t bytes,std::size_t alignment) noexcept {
    if(!bytes) bytes=1;
    if(alignment<alignof(Header)) alignment=alignof(Header);
    if((alignment&(alignment-1)) || bytes>std::numeric_limits<std::size_t>::max()-sizeof(Header)-(alignment-1))return nullptr;
    auto charged=bytes+sizeof(Header)+alignment-1;
    {Lock lock;
     if(charged>counters.limit-counters.live){++counters.rejected;return nullptr;}
     counters.live+=charged;counters.peak=std::max(counters.peak,counters.live);++counters.allocations;}
    auto *base=std::malloc(charged);
    if(!base){release(charged);return nullptr;}
    auto start=reinterpret_cast<std::uintptr_t>(base)+sizeof(Header);
    auto address=(start+alignment-1)&~(alignment-1);
    auto *h=reinterpret_cast<Header*>(address)-1;h->base=base;h->charged=charged;
    return reinterpret_cast<void*>(address);
}
void *allocate(std::size_t bytes,std::size_t alignment) {
    for(;;) {
        if(auto *p=attempt(bytes,alignment))return p;
        auto handler=std::get_new_handler();if(!handler)throw std::bad_alloc();handler();
    }
}
void deallocate(void *p) noexcept {
    if(!p)return;
    auto *h=static_cast<Header*>(p)-1;auto bytes=h->charged;auto base=h->base;
    std::free(base);release(bytes);
}
}
namespace sdb::process_memory {
void set_limit(std::size_t bytes) {
    bool below=false;
    {Lock lock;if(bytes<counters.live)below=true;else counters.limit=bytes;}
    if(below)throw std::invalid_argument("memory limit below live intercepted allocations");
}
Stats stats() noexcept {Lock lock;return counters;}
}
void *operator new(std::size_t n){return allocate(n,alignof(std::max_align_t));}
void *operator new[](std::size_t n){return allocate(n,alignof(std::max_align_t));}
void *operator new(std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void *operator new[](std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void *operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void *operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void *operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(n,a);}catch(...){return nullptr;}}
void *operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void *p) noexcept {deallocate(p);}
void operator delete[](void *p) noexcept {deallocate(p);}
void operator delete(void *p,std::size_t) noexcept {deallocate(p);}
void operator delete[](void *p,std::size_t) noexcept {deallocate(p);}
void operator delete(void *p,std::align_val_t) noexcept {deallocate(p);}
void operator delete[](void *p,std::align_val_t) noexcept {deallocate(p);}
void operator delete(void *p,std::size_t,std::align_val_t) noexcept {deallocate(p);}
void operator delete[](void *p,std::size_t,std::align_val_t) noexcept {deallocate(p);}
void operator delete(void *p,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete[](void *p,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete(void *p,std::align_val_t,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete[](void *p,std::align_val_t,const std::nothrow_t&) noexcept {deallocate(p);}
