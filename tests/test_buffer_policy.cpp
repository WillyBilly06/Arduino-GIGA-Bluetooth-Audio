// Compile assertions against the actual resolver; no fixture execution.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "GigaBredr.h"
#pragma GCC diagnostic pop
#include "detail/ResolveBuffers.h"
#include "platform/I2sBuffer.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;

static_assert(ReceiverConfig{}.maximum_packet_bytes==0,"default SDU is configuration-aware");
static_assert(ReceiverConfig{}.queue_packets==0,"default queue is configuration-aware");
static_assert(ReceiverConfig{}.startup_packets==0,"default startup uses real PCM");
static_assert(ReceiverConfig{}.pcm_buffer_ms==0,"default PCM capacity follows real reserve");
constexpr bool automatic_lhdc() {
    ReceiverConfig r;r.ble.enabled=false;CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;
    AudioBufferConfig b;
    if(!resolveAudioBuffers(r,c,960,b))return false;
    if(b.maximum_packet_bytes!=2048 || b.maximum_access_unit_bytes!=8192)return false;
    if(b.queue_packets!=28 || b.startup_packets!=0)return false;
    return b.startup_buffer_ms==80 && b.pcm_buffer_ms==90;
}
static_assert(automatic_lhdc(),"selected LHDC192 actual-PCM policy");
constexpr bool ble_coexistence_reserve() {
    ReceiverConfig r;CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;AudioBufferConfig b;
    if(!r.ble.enabled || r.ble.audio_coexistence_reserve_ms!=100)return false;
    if(!resolveAudioBuffers(r,c,960,b) || b.startup_buffer_ms!=180 || b.pcm_buffer_ms!=190)return false;
    if(!resolveAudioBuffers(r,c,960,b,false) || b.startup_buffer_ms!=80 || b.pcm_buffer_ms!=90)return false; // allocation fallback
    c.codec=Codec::Ldac;c.sample_rate=96000;
    if(!resolveAudioBuffers(r,c,256,b) || b.startup_buffer_ms!=150 || b.pcm_buffer_ms!=160)return false;
    c.codec=Codec::LhdcV5;c.sample_rate=48000; // Normal-rate streams keep their reserve.
    if(!resolveAudioBuffers(r,c,240,b) || b.startup_buffer_ms!=80)return false;
    c.sample_rate=192000;r.ble.audio_coexistence_reserve_ms=0;
    if(!resolveAudioBuffers(r,c,960,b) || b.startup_buffer_ms!=80)return false;
    r.ble.audio_coexistence_reserve_ms=100; // Explicit settings stay authoritative.
    r.codec_buffers[static_cast<unsigned>(Codec::LhdcV5)].startup_buffer_ms=60;
    return resolveAudioBuffers(r,c,960,b) && b.startup_buffer_ms==60 && b.pcm_buffer_ms==70;
}
static_assert(ble_coexistence_reserve(),"BLE adds automatic reserve only for >=88.2 kHz automatic profiles");
static_assert(ReceiverConfig{}.pcm_ring_d2_sram==false,"D2 placement is opt-in");
static_assert(d2RingPcmMs(192000)==224 && d2RingPcmMs(176400)==244 && d2RingPcmMs(96000)==320,"D2 window ring caps (packed 6-byte frames)");
constexpr bool d2_capped_reserve() {
    ReceiverConfig r;r.pcm_ring_d2_sram=true;CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;AudioBufferConfig b;I2sBufferGeometry g;
    // Default 100 ms BLE margin fits: 80 + 100 reserve, +40 ms headroom (packed ring ~256 KB).
    if(!resolveAudioBuffers(r,c,960,b) || b.startup_buffer_ms!=180 || b.pcm_buffer_ms!=220)return false;
    if(!i2sBufferGeometry(192000,b.pcm_buffer_ms,g,true) || g.total_bytes>D2ArenaBytes)return false;
    // A larger request is capped to the window, keeping 40 ms headroom: 80 + 104 / 224.
    r.ble.audio_coexistence_reserve_ms=200;
    if(!resolveAudioBuffers(r,c,960,b) || b.startup_buffer_ms!=184 || b.pcm_buffer_ms!=224)return false;
    if(!i2sBufferGeometry(192000,224,g,true) || g.total_bytes>D2ArenaBytes || !i2sBufferGeometry(192000,225,g,true) || g.total_bytes<=D2ArenaBytes-3000u)return false;
    c.sample_rate=176400;
    if(!resolveAudioBuffers(r,c,882,b) || b.startup_buffer_ms!=204 || b.pcm_buffer_ms!=244 || !i2sBufferGeometry(176400,244,g,true) || g.total_bytes>D2ArenaBytes)return false;
    c.codec=Codec::Ldac;c.sample_rate=96000; // Fits uncapped: 50 + 200, +40 headroom.
    if(!resolveAudioBuffers(r,c,256,b) || b.startup_buffer_ms!=250 || b.pcm_buffer_ms!=290)return false;
    return resolveAudioBuffers(r,c,256,b,false) && b.startup_buffer_ms==50;
}
constexpr bool d2_all_rates() {
    ReceiverConfig r;r.pcm_ring_d2_sram=true;CodecConfig c;c.codec=Codec::Sbc;c.sample_rate=48000;AudioBufferConfig b;I2sBufferGeometry g;
    if(!resolveAudioBuffers(r,c,128,b) || b.startup_buffer_ms!=150 || b.pcm_buffer_ms!=190)return false; // 50 + 100, +40 headroom
    if(!i2sBufferGeometry(48000,b.pcm_buffer_ms,g,true) || g.total_bytes>D2ArenaBytes)return false;
    r.ble.audio_coexistence_reserve_ms=200;c.codec=Codec::Aptx;
    if(!resolveAudioBuffers(r,c,512,b) || b.startup_buffer_ms!=260 || b.pcm_buffer_ms!=300)return false;   // 60 + 200
    r.pcm_ring_d2_sram=false;c.codec=Codec::Sbc;                                                          // heap: unchanged
    return resolveAudioBuffers(r,c,128,b) && b.startup_buffer_ms==50 && b.pcm_buffer_ms==60;
}
static_assert(d2_all_rates(),"D2 ring gives every rate the BLE coexistence margin");
static_assert(d2_capped_reserve(),"D2 placement caps the BLE coexistence margin to the 256 KB packed window");
constexpr bool lhdc_explicit_reserve_compatibility() {
    ReceiverConfig r;CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;
    AudioBufferConfig b;
    auto& p=r.codec_buffers[static_cast<unsigned>(Codec::LhdcV5)];
    p.pcm_buffer_ms=70;
    if(resolveAudioBuffers(r,c,960,b))return false; // Inherited80 cannot fit70.
    p.startup_buffer_ms=60;
    return resolveAudioBuffers(r,c,960,b) && b.startup_buffer_ms==60 && b.pcm_buffer_ms==70;
}
static_assert(lhdc_explicit_reserve_compatibility(),"explicit legacy LHDC60/70 remains valid; an implicit undersized ring is rejected");
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
    const uint32_t frames[]={128,1024,512,512,256,240,960,2048,864};
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
