#pragma once
#include "detail/AudioTiming.h"
namespace giga_bredr {
// Bounded snapshots. Stream and output epochs are independent counters; compare
// samples only inside one unchanged epoch. Snapshot acquisition is serialized
// by the receiver and DMA owners, never by the caller.
struct AudioDiagnostics {
    detail::RtpTimingSnapshot rtp;
    detail::PacketQueueTimingSnapshot queue;
    detail::PacketWorkTimingSnapshot work;
    detail::OutputTimingSnapshot output;
};
}
