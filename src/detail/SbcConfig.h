#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
struct SbcGeometry {
    uint8_t blocks = 16, subbands = 8;
    size_t frames() const { return size_t(blocks) * subbands; }
};
// An empty codec_info retains the direct API's maximum-geometry SBC stream.
// A nonempty SBC configuration is the four-byte selected A2DP information,
// including one choice per field and a bounded variable bitpool range.
inline bool sbcGeometry(const CodecConfig& c, SbcGeometry& geometry) {
    unsigned frequency;
    switch(c.sample_rate) {
        case 16000: frequency=0x80;break;case 32000: frequency=0x40;break;
        case 44100: frequency=0x20;break;case 48000: frequency=0x10;break;
        default:return false;
    }
    if(c.channels<1 || c.channels>2)return false;
    geometry = {};
    if(!c.codec_info_size)return true;
    if(c.codec_info_size!=4)return false;
    const uint8_t* p=c.codec_info;
    unsigned mode=p[0]&15,blocks=p[1]&0xf0,subbands=p[1]&12,allocation=p[1]&3;
    if((p[0]&0xf0)!=frequency || !mode || (mode&(mode-1)) ||
       !blocks || (blocks&(blocks-1)) || !subbands || (subbands&(subbands-1)) ||
       !allocation || (allocation&(allocation-1)) || ((mode==8)?1:2)!=c.channels)return false;
    geometry.blocks=blocks==0x80?4:blocks==0x40?8:blocks==0x20?12:16;
    geometry.subbands=subbands==8?4:8;
    unsigned maximum=geometry.subbands*((mode&12)?16:32);
    return p[2]>=2 && p[2]<=p[3] && p[3]<=250 && p[3]<=maximum;
}
}}
