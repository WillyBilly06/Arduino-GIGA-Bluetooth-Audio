// Actual production-header constexpr checks; no native or target execution.
#include "detail/BufferSizing.h"
#include "platform/I2sBuffer.h"
#include "platform/PcmRecovery.h"
#include <stdint.h>
using namespace giga_bredr;
using namespace giga_bredr::detail;

static_assert(defaultBufferSizing(Codec::Sbc,44100,128).startup_ms==50,"SBC real-frame reserve");
static_assert(defaultBufferSizing(Codec::Sbc,44100,128).pcm_ms==60,"SBC headroom");
static_assert(defaultBufferSizing(Codec::Aac,44100,1024).startup_ms==50,"LC reserve");
static_assert(defaultBufferSizing(Codec::AacHe,44100,2048).startup_ms==59,"HE output quantum and service margin");
static_assert(defaultBufferSizing(Codec::AacHe,48000,2048).pcm_ms==65,"HE48 capacity");
static_assert(defaultBufferSizing(Codec::AacHe,16000,2048).startup_ms==140,"accepted low-rate HE retains its quantum");
static_assert(defaultBufferSizing(Codec::AacHe,16000,2048).pcm_ms==150,"low-rate HE not silently clamped to100ms");
static_assert(defaultBufferSizing(Codec::Aac,8000,1024).pcm_ms==150,"low-rate LC remains configurable");
static_assert(defaultBufferSizing(Codec::Aptx,48000,512).startup_ms==60,"aptX reserve");
static_assert(defaultBufferSizing(Codec::AptxHd,48000,512).startup_ms==55,"HD reserve");
static_assert(defaultBufferSizing(Codec::Ldac,96000,256).startup_ms==50,"LDAC reserve");
static_assert(defaultBufferSizing(Codec::LhdcV5,192000,960).pcm_ms==70,"LHDC192 time-based ring");
static_assert(defaultBufferSizing(Codec::LhdcV5,44100,240).startup_ms==60,"LHDC240-hop rate domain");
static_assert(defaultBufferSizing(Codec::Opus,48000,960).pcm_ms==50,"Opus20 capacity");
static_assert(defaultBufferSizing(Codec::Opus,48000,1920).startup_ms==52,"Opus40 quantum");
static_assert(defaultBufferSizing(Codec::Opus,48000,2880).pcm_ms==82,"Opus60 quantum");
static_assert(defaultBufferSizing(Codec::Opus,12000,120).valid,"accepted Opus12 geometry");
static_assert(!defaultBufferSizing(Codec::Count,48000,128).valid,"invalid codec");
static_assert(!defaultBufferSizing(Codec::Sbc,0,128).valid,"zero rate");
static_assert(!defaultBufferSizing(Codec::Sbc,48000,0).valid,"zero block");
static_assert(!defaultBufferSizing(Codec::Sbc,48000,128,63).valid,"illegal SDU ceiling");
static_assert(!defaultBufferSizing(Codec::Sbc,48000,128,65536).valid,"SDU ceiling overflow");
static_assert(!defaultBufferSizing(Codec::Opus,8000,23040).valid,"impossible reserve is not clamped");
static_assert(automaticQueueSlots(192000,960,2048)==28,"LHDC slot hint");
static_assert(automaticQueueSlots(96000,256,2048)==31,"stride-aware64KiB target");
static_assert(automaticQueueSlots(96000,256,4096)==15,"no universal4096 allocation");
static_assert(automaticQueueSlots(48000,120,2048)==31,"short Opus bounded allocation");
static_assert(automaticQueueSlots(48000,512,65535)==1,"explicit large legal SDU gets one slot");
static_assert(31u*(2048u+2u)<=65536u,"31 strides fit target");
static_assert(32u*(2048u+2u)>65536u,"32 strides exceed target");

static_assert(i2sPrimeFrames(3840,192)==2304,"legacy prime retained");
static_assert(i2sPrimeFrames(13440,192,11520)==11520,"P counts total real frames once");
static_assert(i2sPrimeFrames(13440,192,2)==384,"both DMA halves need real audio");
static_assert(i2sPrimeFrames(3840,192,3841)==0,"undersized ring rejected");
static_assert(i2sPrimeFrames(7,3)==7,"legacy tiny-ring clamp");
static_assert(i2sPrimeFrames(7,3,2)==6,"requested threshold normalizes to2H");
static_assert(i2sPrimeFrames(7,4)==0,"invalid half geometry");
static_assert(i2sPrimeFrames(0,0)==0,"zero geometry");
static_assert(i2sPrimeFrames(UINT32_MAX,UINT32_MAX/2)==UINT32_MAX,"legacy threshold uses wide arithmetic");

constexpr bool all_geometry() {
    const uint32_t rates[]={8000,11025,12000,16000,22050,24000,32000,44100,48000,88200,96000,176400,192000};
    for(uint32_t rate:rates) {
        for(uint16_t ms=2;ms<=320;++ms) {
            I2sBufferGeometry g;
            if(!i2sBufferGeometry(rate,ms,g))return false;
            if(g.half_frames%4 || g.ring_frames%4 || g.ring_frames<2*g.half_frames)return false;
            if(g.dma_bytes!=size_t(g.half_frames)*16 || g.ring_offset%32)return false;
            if(g.total_bytes!=g.ring_offset+size_t(g.ring_frames)*8 || 4*g.half_frames>65535)return false;
            if(g.ring_offset<g.dma_bytes+sizeof(I2sRing))return false;
            const uint32_t p=i2sPrimeFrames(g.ring_frames,g.half_frames);
            if(p<2*g.half_frames || p>g.ring_frames)return false;
        }
    }
    return true;
}
static_assert(all_geometry(),"all supported rates/2..320ms/alignment/transfer words/prime bounds");
constexpr bool geometry_rejections() {
    I2sBufferGeometry g;
    if(i2sBufferGeometry(48000,1,g))return false;
    if(i2sBufferGeometry(48000,321,g))return false;
    if(i2sBufferGeometry(12345,20,g))return false;
    return true;
}
static_assert(geometry_rejections(),"invalid DMA/ring geometry remains rejected");
constexpr bool selected_high_rate() {
    const auto p=defaultBufferSizing(Codec::LhdcV5,192000,960);
    I2sBufferGeometry g;
    return p.valid && i2sBufferGeometry(192000,p.pcm_ms,g) &&
        g.ring_frames==13440 && g.dma_bytes==3072 && g.total_bytes==110624 &&
        i2sPrimeFrames(g.ring_frames,g.half_frames,11520)==11520;
}
static_assert(selected_high_rate(),"selected LHDC192 lazy arena size, not all codec arenas");
constexpr bool recovery_thresholds() {
    PcmRecovery r;
    if(!r.begin(7,3840,192))return false;
    if(r.snapshot().threshold!=2304 || !r.snapshot().active)return false;
    if(!r.begin(8,13440,192,11520))return false;
    if(r.snapshot().threshold!=11520 || r.snapshot().half_frames!=192)return false;
    if(r.begin(8,3840,192,3841))return false;
    if(r.snapshot().active)return false;
    if(r.begin(0,3840,192,2304))return false;
    if(r.snapshot().active)return false;
    return true;
}
static_assert(recovery_thresholds(),"actual recovery class uses the same real-frame watermark and rejects invalid begin");
