// Compile assertions against the actual resolver; no fixture execution.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "GigaBredr.h"
#pragma GCC diagnostic pop
#include "detail/ResolveBuffers.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;

static_assert(ReceiverConfig{}.maximum_packet_bytes==0,"default SDU is configuration-aware");
static_assert(ReceiverConfig{}.queue_packets==0,"default queue is configuration-aware");
static_assert(ReceiverConfig{}.startup_packets==0,"default startup uses real PCM");
static_assert(ReceiverConfig{}.pcm_buffer_ms==0,"default PCM capacity follows real reserve");
constexpr bool automatic_lhdc() {
    ReceiverConfig r;CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;
    AudioBufferConfig b;
    if(!resolveAudioBuffers(r,c,960,b))return false;
    if(b.maximum_packet_bytes!=2048 || b.maximum_access_unit_bytes!=8192)return false;
    if(b.queue_packets!=28 || b.startup_packets!=0)return false;
    return b.startup_buffer_ms==60 && b.pcm_buffer_ms==70;
}
static_assert(automatic_lhdc(),"selected LHDC192 actual-PCM policy");
constexpr bool explicit_manual() {
    ReceiverConfig r;r.maximum_packet_bytes=4096;r.maximum_access_unit_bytes=8192;
    r.queue_packets=8;r.startup_packets=2;r.pcm_buffer_ms=20;
    CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;AudioBufferConfig b;
    if(!resolveAudioBuffers(r,c,960,b))return false;
    return b.maximum_packet_bytes==4096 && b.queue_packets==8 && b.startup_packets==2 && b.startup_buffer_ms==0 && b.pcm_buffer_ms==20;
}
static_assert(explicit_manual(),"explicit legacy globals preserved");
constexpr bool overrides() {
    ReceiverConfig r;r.queue_packets=8;r.startup_packets=2;r.pcm_buffer_ms=20;
    auto& p=r.codec_buffers[static_cast<unsigned>(Codec::Ldac)];
    p.maximum_packet_bytes=1024;p.queue_packets=12;p.startup_buffer_ms=50;p.pcm_buffer_ms=60;
    CodecConfig c;c.codec=Codec::Ldac;c.sample_rate=96000;AudioBufferConfig b;
    if(!resolveAudioBuffers(r,c,256,b))return false;
    if(b.queue_packets!=12 || b.maximum_packet_bytes!=1024)return false;
    if(b.startup_packets || b.startup_buffer_ms!=50 || b.pcm_buffer_ms!=60)return false;
    p={};p.queue_packets=64;p.maximum_packet_bytes=4096;
    if(!resolveAudioBuffers(r,c,256,b))return false;
    return b.queue_packets==64 && b.startup_packets==2 && b.pcm_buffer_ms==20;
}
static_assert(overrides(),"codec-layer precedence and explicit slots never byte-capped");
constexpr bool contradictions() {
    ReceiverConfig r;CodecConfig c;c.codec=Codec::Aac;c.sample_rate=44100;AudioBufferConfig b;
    r.pcm_buffer_ms=20;
    if(resolveAudioBuffers(r,c,1024,b))return false;
    r.startup_buffer_ms=10;
    if(!resolveAudioBuffers(r,c,1024,b))return false;
    r.startup_packets=2;
    if(resolveAudioBuffers(r,c,1024,b))return false;
    r.startup_buffer_ms=0;
    if(!resolveAudioBuffers(r,c,1024,b))return false;
    auto& p=r.codec_buffers[static_cast<unsigned>(Codec::Aac)];
    p.startup_packets=3;p.startup_buffer_ms=40;
    return !resolveAudioBuffers(r,c,1024,b);
}
static_assert(contradictions(),"contradictory requests fail, never start underfilled");
constexpr bool no_output() {
    ReceiverConfig r;r.enable_i2s=false;CodecConfig c;AudioBufferConfig b;
    if(!resolveAudioBuffers(r,c,128,b))return false;
    return b.startup_packets==1 && b.startup_buffer_ms==0;
}
static_assert(no_output(),"decode-only mode needs no PCM playback reserve");
constexpr bool all_modes() {
    ReceiverConfig r;AudioBufferConfig b;CodecConfig c;
    const uint32_t frames[]={128,1024,512,512,256,240,960,2048};
    for(unsigned i=0;i<static_cast<unsigned>(Codec::Count);++i) {
        c.codec=static_cast<Codec>(i);c.sample_rate=48000;
        if(!resolveAudioBuffers(r,c,frames[i],b))return false;
        if(!b.queue_packets || b.queue_packets>64 || b.startup_packets)return false;
        if(!b.startup_buffer_ms || b.pcm_buffer_ms<b.startup_buffer_ms)return false;
    }
    c.codec=Codec::AacHe;c.sample_rate=16000;
    if(!resolveAudioBuffers(r,c,2048,b))return false;
    return b.startup_buffer_ms==140 && b.pcm_buffer_ms==150;
}
static_assert(all_modes(),"all codec families resolve only the selected profile");
constexpr bool invalid_requests() {
    ReceiverConfig r;CodecConfig c;AudioBufferConfig b;
    r.maximum_packet_bytes=63;if(resolveAudioBuffers(r,c,128,b))return false;
    r.maximum_packet_bytes=0;r.queue_packets=65;if(resolveAudioBuffers(r,c,128,b))return false;
    r.queue_packets=0;r.pcm_buffer_ms=321;if(resolveAudioBuffers(r,c,128,b))return false;
    r.pcm_buffer_ms=0;r.startup_buffer_ms=321;if(resolveAudioBuffers(r,c,128,b))return false;
    r.startup_buffer_ms=0;c.codec=Codec::Count;if(resolveAudioBuffers(r,c,128,b))return false;
    c.codec=Codec::Sbc;c.sample_rate=0;return !resolveAudioBuffers(r,c,128,b);
}
static_assert(invalid_requests(),"invalid global and selected geometry fail before allocation");
