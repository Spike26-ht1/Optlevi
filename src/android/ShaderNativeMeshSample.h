#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <cmath>

namespace modmenu::android::shaderbridge::meshsample {
// All offsets refer to buffer objects, never CPU client pointers.
inline bool attributeRange(std::uint64_t offset,std::uint64_t stride,
                           std::int64_t index,std::uint64_t bytes,
                           std::uint64_t bufferSize,std::uint64_t& start){
    if(index<0||!stride||!bytes||offset>bufferSize)return false;
    auto i=std::uint64_t(index);
    if(i>(bufferSize-offset)/stride)return false;
    auto p=offset+i*stride;
    if(bytes>bufferSize-p)return false;
    start=p;return true;
}
inline float half(std::uint16_t bits){
    unsigned e=(bits>>10)&31,m=bits&1023;
    float value=e==0?std::ldexp(float(m),-24):
        e==31?(m?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()):
        std::ldexp(float(1024+m),int(e)-25);
    return bits&0x8000?-value:value;
}
inline std::uint16_t u16(unsigned char const* p){
    return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);
}
inline std::uint32_t u32(unsigned char const* p){
    return std::uint32_t(u16(p))|(std::uint32_t(u16(p+2))<<16);
}
}
