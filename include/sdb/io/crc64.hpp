#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace sdb::io {
// CRC-64/ECMA-182, init=0, xorout=0, non-reflected.
inline std::uint64_t crc64(std::span<const std::uint8_t> bytes,std::uint64_t crc=0) {
    static constexpr auto table=[] {
        std::array<std::uint64_t,256> t{};
        for(std::uint64_t i=0;i<t.size();++i) {
            auto v=i<<56;
            for(int j=0;j<8;++j) v=(v<<1)^((v>>63)?UINT64_C(0x42f0e1eba9ea3693):0);
            t[i]=v;
        }
        return t;
    }();
    for(auto b:bytes) crc=(crc<<8)^table[(crc>>56)^b];
    return crc;
}
} // namespace sdb::io
