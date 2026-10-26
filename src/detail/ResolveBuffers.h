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
constexpr AudioBufferConfig globalBufferSettings(const ReceiverConfig& r) {
    return {r.maximum_packet_bytes,r.maximum_access_unit_bytes,r.queue_packets,
            r.startup_packets,r.pcm_buffer_ms,r.startup_buffer_ms};
}
// No allocation. Caller passes the actual selected decoder block capacity.
// Explicit fields remain authoritative; capacity is not a latency guarantee.
constexpr bool resolveAudioBuffers(const ReceiverConfig& r,const CodecConfig& c,
                                   uint32_t block_frames,AudioBufferConfig& out) {
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
    else out.startup_buffer_ms=g.startup_buffer_ms?g.startup_buffer_ms:automatic.startup_ms;
    if(!r.enable_i2s) { out.startup_packets=1;out.startup_buffer_ms=0; }
    if(out.startup_packets>out.queue_packets) out.startup_packets=out.queue_packets;
    const uint32_t requested_pcm=out.startup_buffer_ms?uint32_t(out.startup_buffer_ms)+10u:20u;
    const uint32_t pcm=p.pcm_buffer_ms?p.pcm_buffer_ms:g.pcm_buffer_ms?g.pcm_buffer_ms:requested_pcm;
    if(pcm<2 || pcm>MaximumPcmBufferMs || (out.startup_buffer_ms && pcm<out.startup_buffer_ms)) return false;
    out.pcm_buffer_ms=uint16_t(pcm);
    return true;
}
}}
