#pragma once
#include "durability.hpp"
#include <span>
#include <limits>
#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#endif

namespace sdb {
// Immutable published file only. A mapping reserves address space; the OS faults
// in pages touched by selected footer, directory and data records.
class MappedInput {
    const std::uint8_t *bytes_ = nullptr;
    std::size_t size_ = 0;
public:
    explicit MappedInput(const std::filesystem::path& path) {
#ifdef _WIN32
        HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file == INVALID_HANDLE_VALUE) throw std::runtime_error("mapped input open failed");
        LARGE_INTEGER size{};
        HANDLE mapping = nullptr;
        if(GetFileSizeEx(file,&size) && size.QuadPart>0 &&
           static_cast<std::uint64_t>(size.QuadPart)<=std::numeric_limits<std::size_t>::max()) {
            size_=static_cast<std::size_t>(size.QuadPart);
            mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
        }
        CloseHandle(file);
        if(mapping) {
            bytes_=static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
            CloseHandle(mapping);
        }
#else
        int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);
        if(fd<0) throw std::runtime_error("mapped input open failed");
        struct stat st{};
        if(fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>0 &&
           static_cast<std::uint64_t>(st.st_size)<=std::numeric_limits<std::size_t>::max()) {
            size_=static_cast<std::size_t>(st.st_size);
            void *p=mmap(nullptr,size_,PROT_READ,MAP_PRIVATE,fd,0);
            if(p!=MAP_FAILED) bytes_=static_cast<const std::uint8_t*>(p);
        }
        close(fd);
#endif
        if(!bytes_) throw std::runtime_error("cannot map result file");
    }
    ~MappedInput() {
#ifdef _WIN32
        UnmapViewOfFile(bytes_);
#else
        munmap(const_cast<std::uint8_t*>(bytes_),size_);
#endif
    }
    MappedInput(const MappedInput&)=delete;
    MappedInput& operator=(const MappedInput&)=delete;
    std::span<const std::uint8_t> slice(std::uint64_t offset,std::uint64_t count) const {
        if(offset>size_ || count>size_-offset) throw std::runtime_error("mapped record outside file");
        return {bytes_+static_cast<std::size_t>(offset),static_cast<std::size_t>(count)};
    }
};
}
