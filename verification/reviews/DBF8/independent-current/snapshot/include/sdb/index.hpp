#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <cstddef>

#ifndef SDB_INDEX_BITS
#define SDB_INDEX_BITS 32
#endif
#if SDB_INDEX_BITS != 32 && SDB_INDEX_BITS != 64
#error SDB_INDEX_BITS must be 32 or 64 for the entire consumer program
#endif
namespace sdb {
#if SDB_INDEX_BITS == 64
using Index=std::uint64_t;
#else
using Index=std::uint32_t;
#endif
inline constexpr Index invalid_index=std::numeric_limits<Index>::max();
inline constexpr std::uint32_t index_bits=SDB_INDEX_BITS;
// ABI2 tags every native file written by an Index64 program. Never decode a
// width-dependent metadata vector using the other build's interpretation.
inline constexpr std::uint32_t native_index_epoch=SDB_INDEX_BITS==64?2:1;
inline Index index_of(std::size_t n) {
    if(n>=invalid_index) throw std::length_error("configured index width exhausted");
    return static_cast<Index>(n);
}
} // namespace sdb
