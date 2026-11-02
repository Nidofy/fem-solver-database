#pragma once
#include "memory.hpp"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace sdb {
// Dedicated scratch pages. A failed large-page request falls back to ordinary pages.
template<class T> class BatchPages {
    static_assert(std::is_trivial_v<T>);
    T* data_ = nullptr;
    std::size_t bytes_ = 0;
    const char* mode_ = "ordinary";
  public:
    BatchPages() = default;
    BatchPages(const BatchPages&) = delete;
    BatchPages& operator=(const BatchPages&) = delete;
    void allocate(std::size_t count) {
        if(data_) throw std::logic_error("scratch already allocated");
        if(!count) return;
        bytes_=product(count,sizeof(T));
#if defined(_WIN32)
        auto page=GetLargePageMinimum();
        if(page) {
            auto rounded=product(sum(bytes_,page-1)/page,page);
            data_=static_cast<T*>(VirtualAlloc(nullptr,rounded,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE));
            if(data_) {bytes_=rounded;mode_="large";}
        }
        if(!data_) data_=static_cast<T*>(VirtualAlloc(nullptr,bytes_,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#elif defined(__linux__)
        void* region=mmap(nullptr,bytes_,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(region!=MAP_FAILED) {
            data_=static_cast<T*>(region);
#ifdef MADV_HUGEPAGE
            if(madvise(region,bytes_,MADV_HUGEPAGE)==0) mode_="thp-requested";
#endif
        }
#else
        data_=static_cast<T*>(::operator new(bytes_,std::align_val_t(64)));
#endif
        if(!data_) throw std::bad_alloc();
        // Start scalar lifetimes without zeroing or assigning physical pages on the serial thread.
        std::uninitialized_default_construct_n(data_,count);
        host_memory.acquired(bytes_);
    }
    ~BatchPages() {
        if(!data_) return;
#if defined(_WIN32)
        VirtualFree(data_,0,MEM_RELEASE);
#elif defined(__linux__)
        munmap(data_,bytes_);
#else
        ::operator delete(data_,std::align_val_t(64));
#endif
        host_memory.released(bytes_);
    }
    T* data() { return data_; }
    const char* mode() const {return mode_;}
};
} // namespace sdb
