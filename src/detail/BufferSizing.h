#pragma once
#include "../Types.h"

namespace giga_bredr { namespace detail {
constexpr uint16_t MaximumPcmBufferMs = 320;
constexpr size_t AutomaticPacketBytes = 2048;
constexpr size_t AutomaticAccessUnitBytes = 8192;
constexpr size_t AutomaticQueueTargetBytes = 65536;

// Capacity hint, not packet-duration accounting. Packet aggregation and
// fragmentation do not determine startup; actual decoded PCM does that.
constexpr uint8_t automaticQueueSlots(uint32_t rate,uint32_t block_frames,size_t packet_bytes) {
    if(rate<8000 || rate>192000 || !block_frames || packet_bytes<64 || packet_bytes>65535) return 0;
    const uint64_t frames=(uint64_t(rate)*120u+999u)/1000u;
    uint64_t slots=(frames+block_frames-1u)/block_frames+4u;
    if(slots<8u) slots=8u;
    if(slots>64u) slots=64u;
    size_t byte_slots=AutomaticQueueTargetBytes/(packet_bytes+sizeof(uint16_t));
    // An explicitly larger legal packet still needs one complete slot.
    if(!byte_slots) byte_slots=1;
    if(slots>byte_slots) slots=byte_slots;
    return uint8_t(slots);
}

struct BufferDefaults {
    uint16_t startup_ms=0,pcm_ms=0;
    uint8_t queue_packets=0;
    bool valid=false;
};

// Selected output-frame geometry only; decoder.open remains authoritative
// for accepted codec/rate/profile combinations. No allocation or runtime IO.
constexpr BufferDefaults defaultBufferSizing(Codec codec,uint32_t rate,uint32_t block_frames,
                                             size_t packet_bytes=AutomaticPacketBytes) {
    if(rate<8000 || rate>192000 || !block_frames || block_frames>23040) return {};
    uint32_t base=0;
    switch(codec) {
        case Codec::Sbc: case Codec::Aac: case Codec::Ldac: base=50;break;
        case Codec::Aptx: base=60;break;
        case Codec::LhdcV5: base=80;break;
        case Codec::AptxHd: base=55;break;
        case Codec::AacHe: case Codec::Opus: base=40;break;
        default:return {};
    }
    const uint64_t quantum_ms=(uint64_t(block_frames)*1000u+rate-1u)/rate;
    const uint64_t startup=quantum_ms+12u>base?quantum_ms+12u:base;
    const uint8_t slots=automaticQueueSlots(rate,block_frames,packet_bytes);
    if(!slots || startup+10u>MaximumPcmBufferMs) return {};
    return {uint16_t(startup),uint16_t(startup+10u),slots,true};
}
}}
