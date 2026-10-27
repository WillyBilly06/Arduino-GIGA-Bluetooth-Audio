#pragma once
#include <stdint.h>
#include <stddef.h>

namespace giga_bredr { namespace detail {
// Pure bounded telemetry. Each object has one externally serialized owner.
// snapshot() returns an independent copy; it is not a concurrent-reader barrier.
// Epoch0 is disconnected. Caller epochs must change on stream/format restart.
// Timestamps are uint32 microseconds. Consecutive timed intervals must be less
// than2^31us; longer/backward intervals are counted as clock discontinuities.
struct DurationHistogram {
    // Inclusive upper bounds:1,2,5,10,20,50ms; bucket6 is greater than50ms.
    uint64_t buckets[7] = {};
    uint64_t samples = 0, total_us = 0;
    uint32_t maximum_us = 0;
};

enum class RtpObservation : uint8_t { Rejected, First, Forward, Duplicate, Backward, SourceChange };
enum RtpPayloadFlag : uint8_t {
    RtpLhdcPayload=1, RtpInnerSequenceValid=2, RtpDeclaredFramesValid=4,
    RtpScannedFramesValid=8, RtpSamplesValid=16, RtpPayloadFragmented=32,
    RtpPayloadMalformed=64
};
struct RtpPacketDetail {
    uint32_t sample_frames=0;
    uint16_t sample_hop=0, payload_bytes=0;
    uint8_t header=0, inner_sequence=0, declared_frames=0, scanned_frames=0, flags=0;
};
// Bounded framing inspection only: no entropy decode, allocation or retained
// payload pointer. Unknown/fragmented/malformed packets have no sample span.
RtpPacketDetail lhdcRtpDetail(const uint8_t* payload, size_t bytes, uint32_t sample_hop);
enum RtpDiscontinuityReason : uint16_t {
    RtpSequenceGap=1, RtpDuplicatePacket=2, RtpBackwardPacket=4,
    RtpSourceChanged=8, RtpArrivalClock=16, RtpTimestampStep=32,
    RtpInnerSequenceStep=64, RtpPayloadUnknown=128
};
struct RtpDiscontinuityRecord {
    uint64_t packet_number=0;
    uint32_t previous_ssrc=0, ssrc=0, previous_timestamp=0, timestamp=0;
    uint32_t previous_arrival_us=0, arrival_us=0, arrival_delta_us=0;
    uint32_t timestamp_delta=0, expected_timestamp_delta=0;
    uint16_t previous_sequence=0, sequence=0, reasons=0;
    bool arrival_delta_valid=false, timestamp_delta_valid=false, expected_delta_valid=false;
    RtpPacketDetail previous_packet, packet;
};
struct RtpTimingSnapshot {
    uint32_t epoch = 0;
    bool active = false;
    uint64_t packets = 0, forward_gaps = 0, missing_sequences = 0;
    uint64_t duplicates = 0, backward = 0, source_changes = 0, rejected_epoch = 0;
    uint64_t clock_discontinuities = 0;
    DurationHistogram arrival_gaps;
    uint32_t ssrc = 0;
    uint16_t high_sequence = 0;
    uint64_t source_packets = 0;
    uint32_t source_first_timestamp = 0, source_last_timestamp = 0;
    uint32_t source_first_arrival_us = 0, source_last_arrival_us = 0;
    uint64_t discontinuities=0, timestamp_discontinuities=0, inner_sequence_discontinuities=0;
    uint64_t unknown_payloads=0, malformed_payloads=0, records_not_retained=0;
    uint32_t record_count=0;
    RtpDiscontinuityRecord records[16]; // First16 frozen; counters keep growing.
};
class RtpTiming {
public:
    void beginEpoch(uint32_t epoch);
    void endEpoch(uint32_t epoch);
    RtpObservation observe(uint32_t epoch, uint32_t ssrc, uint16_t sequence,
                           uint32_t timestamp, uint32_t arrival_us,
                           RtpPacketDetail packet=RtpPacketDetail{});
    RtpTimingSnapshot snapshot() const { return stats_; }
private:
    RtpTimingSnapshot stats_;
    uint32_t previous_arrival_ = 0;
    bool have_arrival_ = false, have_source_ = false;
    RtpPacketDetail previous_packet_;
    void record(uint32_t ssrc, uint16_t sequence, uint32_t timestamp, uint32_t arrival,
                RtpPacketDetail packet, RtpObservation observation, bool clock_bad);
};

struct QueueToken { uint32_t epoch = 0; uint64_t serial = 0; };
struct PacketQueueTimingSnapshot {
    uint32_t epoch = 0, capacity = 0, depth = 0, high_water = 0;
    bool active = false;
    uint64_t enqueued = 0, dequeued = 0, full = 0, invalid_token = 0, rejected_epoch = 0;
    uint64_t discarded_on_end = 0, clock_discontinuities = 0;
    DurationHistogram residence;
};
class PacketQueueTiming {
public:
    // Call under the actual queue's serialization. Only call enqueued after an
    // actual push succeeds (or to measure the same full condition). Store the
    // returned scalar token with that queue entry, then dequeue FIFO by token.
    bool beginEpoch(uint32_t epoch, uint32_t capacity);
    void endEpoch(uint32_t epoch);
    bool enqueued(uint32_t epoch, uint32_t arrival_us, QueueToken& token);
    bool dequeued(QueueToken token, uint32_t now_us);
    PacketQueueTimingSnapshot snapshot() const { return stats_; }
private:
    struct Entry { uint64_t serial = 0; uint32_t birth_us = 0; };
    Entry entries_[64];
    PacketQueueTimingSnapshot stats_;
    uint32_t read_ = 0, write_ = 0;
    uint64_t serial_ = 0; // Preserved across epochs;0 is never an issued token.
};

struct PacketWorkSample {
    // Non-overlapping accumulated intervals within one packet. Decode work
    // excludes waiting for output capacity; elapsed also includes other work.
    uint32_t decode_work_us = 0, write_wait_us = 0, elapsed_us = 0;
};
struct PacketWorkTimingSnapshot {
    uint32_t epoch = 0;
    bool active = false;
    uint64_t packets = 0, invalid_sample = 0, rejected_epoch = 0;
    DurationHistogram decode_work, write_wait, elapsed;
};
class PacketWorkTiming {
public:
    void beginEpoch(uint32_t epoch);
    void endEpoch(uint32_t epoch);
    bool record(uint32_t epoch, PacketWorkSample sample);
    PacketWorkTimingSnapshot snapshot() const { return stats_; }
private:
    PacketWorkTimingSnapshot stats_;
};

struct UnderrunRecord {
    uint64_t refill_number = 0; // Monotonic within this output epoch.
    // silent_frames counts absent input, including nonzero starvation decay.
    uint32_t at_us = 0, queued_before = 0, real_frames = 0, silent_frames = 0;
};
struct OutputTimingSnapshot {
    uint32_t epoch = 0;
    bool active = false;
    // Legacy silent_frames counts absent input; it is not a literal-zero count.
    uint64_t refills = 0, real_frames = 0, silent_frames = 0, underruns = 0;
    uint64_t invalid_sample = 0, rejected_epoch = 0, records_not_retained = 0;
    uint32_t minimum_queued = 0, record_count = 0;
    UnderrunRecord records[16]; // First16, frozen once full; totals keep growing.
};
class OutputTiming {
public:
    // begin/end run while the IRQ owner is stopped. refill is constant-time,
    // allocation/logging-free and suitable for the existing DMA refill IRQ.
    void beginEpoch(uint32_t epoch);
    void endEpoch(uint32_t epoch);
    bool refill(uint32_t epoch, uint32_t at_us, uint32_t requested_frames,
                uint32_t real_frames, uint32_t queued_before);
    OutputTimingSnapshot snapshot() const { return stats_; }
private:
    OutputTimingSnapshot stats_;
};
}}
