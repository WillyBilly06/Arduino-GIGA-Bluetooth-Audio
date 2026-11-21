#pragma once
#include "../GigaBredr.h"
#include "BufferSizing.h"
namespace giga_bredr { namespace detail {
constexpr bool validBufferSettings(const AudioBufferConfig& b) {
    if(b.maximum_packet_bytes && (b.maximum_packet_bytes<64 || b.maximum_packet_bytes>65535)) return false;
    if(b.maximum_access_unit_bytes && (b.maximum_access_unit_bytes<64 || b.maximum_access_unit_bytes>65535)) return false;
    if(b.queue_packets>64 || b.startup_packets>64) return false;
    if(b.pcm_buffer_ms && (b.pcm_buffer_ms<2 || b.pcm_buffer_ms>MaximumPcmBufferMs)) return false;
    if(b.startup_buffer_ms>MaximumPcmBufferMs) return false;
    return !(b.startup_packets && b.startup_buffer_ms);
}
// Largest pcm_buffer_ms whose I2S arena (2 x 1 ms DMA halves, ring object,
// 32-byte alignment, 8-byte frames) fits the 256 KB D2 SRAM window.
constexpr uint32_t D2RingWindowBytes=0x40000u;
constexpr uint32_t d2RingPcmMs(uint32_t rate) {
    if(rate<8000 || rate>192000) return 0;
    const uint32_t half=((rate+999u)/1000u+3u)&~3u;
    const uint32_t overhead=half*16u+96u;
    const uint32_t frames=(D2RingWindowBytes-overhead)/8u;
    const uint32_t ms=uint32_t((uint64_t(frames-8u)*1000u)/rate);
    return ms>MaximumPcmBufferMs?MaximumPcmBufferMs:ms;
}
constexpr AudioBufferConfig globalBufferSettings(const ReceiverConfig& r) {
    return {r.maximum_packet_bytes,r.maximum_access_unit_bytes,r.queue_packets,
            r.startup_packets,r.pcm_buffer_ms,r.startup_buffer_ms};
}
// No allocation. Caller passes the actual selected decoder block capacity.
// Explicit fields remain authoritative; capacity is not a latency guarantee.
// coexistence=false resolves without BleConfig::audio_coexistence_reserve_ms
// (the receiver's fallback when the wider ring cannot be allocated).
constexpr bool resolveAudioBuffers(const ReceiverConfig& r,const CodecConfig& c,
                                   uint32_t block_frames,AudioBufferConfig& out,bool coexistence=true) {
    out={};
    if(static_cast<unsigned>(c.codec)>=static_cast<unsigned>(Codec::Count)) return false;
    const auto& p=r.codec_buffers[static_cast<unsigned>(c.codec)];
    const auto g=globalBufferSettings(r);
    if(!validBufferSettings(g) || !validBufferSettings(p)) return false;
    out.maximum_packet_bytes=p.maximum_packet_bytes?p.maximum_packet_bytes:
        g.maximum_packet_bytes?g.maximum_packet_bytes:AutomaticPacketBytes;
    out.maximum_access_unit_bytes=p.maximum_access_unit_bytes?p.maximum_access_unit_bytes:
        g.maximum_access_unit_bytes?g.maximum_access_unit_bytes:AutomaticAccessUnitBytes;
    const auto automatic=defaultBufferSizing(c.codec,c.sample_rate,block_frames,out.maximum_packet_bytes);
    if(!automatic.valid) return false;
    out.queue_packets=p.queue_packets?p.queue_packets:g.queue_packets?g.queue_packets:automatic.queue_packets;
    // Choose the startup MODE from the highest explicit layer, not two
    // independently inherited fields which could contradict each other.
    if(p.startup_packets) out.startup_packets=p.startup_packets;
    else if(p.startup_buffer_ms) out.startup_buffer_ms=p.startup_buffer_ms;
    else if(g.startup_packets) out.startup_packets=g.startup_packets;
    else if(g.startup_buffer_ms) out.startup_buffer_ms=g.startup_buffer_ms;
    else {
        // Automatic only: widen the jitter reserve where BLE shares the radio
        // with a high-rate stream (see BleConfig::audio_coexistence_reserve_ms).
        uint32_t startup=automatic.startup_ms;
        const uint32_t margin=r.ble.audio_coexistence_reserve_ms>200?200u:r.ble.audio_coexistence_reserve_ms;
        uint32_t extra=margin;
        if(r.pcm_ring_d2_sram) {
            // Keep the widened ring inside the fixed D2 window so it is
            // actually placed there instead of falling back to the heap.
            const uint32_t cap=d2RingPcmMs(c.sample_rate);
            extra=startup+10u>=cap?0u:(startup+extra+10u>cap?cap-startup-10u:extra);
        }
        if(coexistence && r.ble.enabled && c.sample_rate>=88200 && startup+extra+10u<=MaximumPcmBufferMs) startup+=extra;
        out.startup_buffer_ms=uint16_t(startup);
    }
    if(!r.enable_i2s) { out.startup_packets=1;out.startup_buffer_ms=0; }
    if(out.startup_packets>out.queue_packets) out.startup_packets=out.queue_packets;
    const uint32_t requested_pcm=out.startup_buffer_ms?uint32_t(out.startup_buffer_ms)+10u:20u;
    const uint32_t pcm=p.pcm_buffer_ms?p.pcm_buffer_ms:g.pcm_buffer_ms?g.pcm_buffer_ms:requested_pcm;
    if(pcm<2 || pcm>MaximumPcmBufferMs || (out.startup_buffer_ms && pcm<out.startup_buffer_ms)) return false;
    out.pcm_buffer_ms=uint16_t(pcm);
    return true;
}
}}
