#include "detail/AudioTiming.h"
#include "platform/I2sBuffer.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace giga_bredr::detail;
#define CHECK(expression) do { if (!(expression)) { std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #expression); return false; } } while (0)

// Catch sequence wrap being classified as loss, backward packets advancing the
// high-water sequence, and old-epoch packets contaminating a new stream.
static bool rtpContinuity() {
    RtpTiming timing; timing.beginEpoch(7);
    CHECK(timing.observe(7, 1, 65534, 0xfffffc00u, 0xfffffff0u) == RtpObservation::First);
    CHECK(timing.observe(7, 1, 65535, 0xffffffc0u, 0x20) == RtpObservation::Forward);
    CHECK(timing.observe(7, 1, 0, 0x380, 0x100) == RtpObservation::Forward);
    CHECK(timing.observe(7, 1, 3, 0xec0, 0x400) == RtpObservation::Forward);
    CHECK(timing.observe(7, 1, 3, 0xec0, 0x500) == RtpObservation::Duplicate);
    CHECK(timing.observe(7, 1, 2, 0xb00, 0x600) == RtpObservation::Backward);
    CHECK(timing.observe(7, 1, 4, 0x1280, 0x700) == RtpObservation::Forward);
    CHECK(timing.snapshot().source_first_timestamp == 0xfffffc00u);
    CHECK(timing.snapshot().source_last_timestamp == 0x1280);
    CHECK(timing.observe(7, 2, 100, 900, 0x800) == RtpObservation::SourceChange);
    auto s = timing.snapshot();
    CHECK(s.packets == 8 && s.forward_gaps == 1 && s.missing_sequences == 2);
    CHECK(s.duplicates == 1 && s.backward == 1 && s.source_changes == 1);
    CHECK(s.arrival_gaps.samples == 7 && s.arrival_gaps.total_us == 2064);
    CHECK(s.arrival_gaps.maximum_us == 768 && s.arrival_gaps.buckets[0] == 7);
    CHECK(s.source_packets == 1 && s.ssrc == 2 && s.high_sequence == 100);
    CHECK(s.source_first_timestamp == 900 && s.source_last_timestamp == 900);
    CHECK(timing.observe(6, 2, 101, 1000, 0x900) == RtpObservation::Rejected);
    timing.endEpoch(7);
    CHECK(timing.observe(7, 2, 101, 1000, 0x900) == RtpObservation::Rejected);
    CHECK(timing.snapshot().packets == 8 && timing.snapshot().rejected_epoch == 2);
    timing.beginEpoch(8); timing.endEpoch(7);
    CHECK(timing.snapshot().active && timing.snapshot().packets == 0);
    CHECK(timing.observe(8, 4, 10, 0, 0) == RtpObservation::First);
    CHECK(timing.observe(8, 4, 0x800a, 10, 1) == RtpObservation::Backward);
    CHECK(timing.observe(8, 4, 11, 20, 2) == RtpObservation::Forward);
    CHECK(timing.snapshot().forward_gaps == 0);
    timing.beginEpoch(0);
    CHECK(timing.observe(0, 1, 1, 1, 1) == RtpObservation::Rejected);
    CHECK(s.packets == 8); // Earlier snapshots are independent values.
    return true;
}

// Catch histogram boundary mistakes and a timer reset being treated as a
// multi-billion-microsecond transport gap. A normal uint32 timer wrap is valid.
static bool arrivalHistogram() {
    RtpTiming timing; timing.beginEpoch(1);
    timing.observe(1, 9, 1, 0, 0);
    const uint32_t gaps[] = {1000,1001,2000,2001,5000,5001,10000,10001,20000,20001,50000,50001};
    uint32_t now = 0; uint16_t sequence = 1;
    for (auto gap : gaps) { now += gap; timing.observe(1, 9, ++sequence, 0, now); }
    auto s = timing.snapshot(); const uint64_t expected[] = {1,2,2,2,2,2,1};
    for (unsigned i = 0; i < 7; ++i) CHECK(s.arrival_gaps.buckets[i] == expected[i]);
    CHECK(s.arrival_gaps.maximum_us == 50001 && s.arrival_gaps.samples == 12);
    timing.beginEpoch(2); timing.observe(2, 9, 1, 0, 1000);
    timing.observe(2, 9, 2, 1, 900); timing.observe(2, 9, 3, 2, 1100);
    s = timing.snapshot();
    CHECK(s.clock_discontinuities == 1 && s.arrival_gaps.samples == 1);
    CHECK(s.arrival_gaps.maximum_us == 200);
    return true;
}

// Catch dequeue/token mismatch corrupting FIFO ownership, incorrect wrap-safe
// residence timing, and queue saturation becoming an unreported lost packet.
static bool queueOwnership() {
    PacketQueueTiming timing; CHECK(timing.beginEpoch(7, 2));
    QueueToken a, b, rejected;
    CHECK(timing.enqueued(7, 0xfffffff0u, a)); CHECK(timing.enqueued(7, 0x20, b));
    CHECK(!timing.enqueued(7, 0x30, rejected) && !rejected.serial);
    CHECK(!timing.dequeued(b, 0x40)); CHECK(timing.snapshot().depth == 2);
    CHECK(timing.dequeued(a, 0x50)); CHECK(timing.dequeued(b, 0x80));
    auto s = timing.snapshot();
    CHECK(s.enqueued == 2 && s.dequeued == 2 && s.full == 1 && s.invalid_token == 1);
    CHECK(s.depth == 0 && s.high_water == 2);
    CHECK(s.residence.samples == 2 && s.residence.total_us == 192 && s.residence.maximum_us == 96);
    uint64_t previous = b.serial;
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(timing.enqueued(7, i * 1000, a)); CHECK(a.serial > previous);
        previous = a.serial; CHECK(timing.dequeued(a, i * 1000 + 30));
    }
    CHECK(timing.snapshot().residence.total_us == 3192);
    CHECK(timing.enqueued(7, 1000, a)); timing.endEpoch(7);
    CHECK(!timing.dequeued(a, 1100) && timing.snapshot().depth == 0);
    CHECK(timing.beginEpoch(8, 1)); timing.endEpoch(7);
    CHECK(timing.snapshot().active); CHECK(!timing.dequeued(a, 1200));
    CHECK(timing.enqueued(8, 1000, b) && b.serial > previous);
    CHECK(!timing.enqueued(8, 1010, rejected)); CHECK(timing.dequeued(b, 1090));
    CHECK(!timing.dequeued(b, 1100));
    s = timing.snapshot(); CHECK(s.rejected_epoch == 2 && s.invalid_token == 1);
    CHECK(!timing.beginEpoch(9, 0) && !timing.snapshot().active);
    CHECK(!timing.beginEpoch(9, 65));
    CHECK(!timing.enqueued(9, 0, a));
    CHECK(timing.beginEpoch(10, 1)); CHECK(timing.enqueued(10, 1000, a));
    CHECK(timing.dequeued(a, 900)); s = timing.snapshot();
    CHECK(s.clock_discontinuities == 1 && !s.residence.samples && s.dequeued == 1);
    CHECK(timing.beginEpoch(11, 64)); QueueToken tokens[64];
    for (auto& token : tokens) CHECK(timing.enqueued(11, 1000, token));
    CHECK(!timing.enqueued(11, 1100, rejected) && timing.snapshot().high_water == 64);
    for (auto token : tokens) CHECK(timing.dequeued(token, 1200));
    CHECK(timing.snapshot().depth == 0 && timing.snapshot().residence.total_us == 12800);
    return true;
}

// Catch I2S waiting being charged to decoder work or invalid, overlapping
// timing measurements being accepted as a trustworthy packet sample.
static bool packetWork() {
    PacketWorkTiming timing; timing.beginEpoch(4);
    CHECK(timing.record(4, {1000,6000,8000})); CHECK(timing.record(4, {3000,0,3500}));
    CHECK(!timing.record(4, {5000,5000,9000}));
    auto s = timing.snapshot();
    CHECK(s.packets == 2 && s.invalid_sample == 1);
    CHECK(s.decode_work.total_us == 4000 && s.decode_work.maximum_us == 3000);
    CHECK(s.write_wait.total_us == 6000 && s.write_wait.maximum_us == 6000);
    CHECK(s.elapsed.total_us == 11500 && s.elapsed.samples == 2);
    timing.endEpoch(4); CHECK(!timing.record(4, {1,0,1}));
    timing.beginEpoch(5); timing.endEpoch(4); CHECK(timing.snapshot().active);
    CHECK(!timing.record(4, {1,0,1}));
    CHECK(timing.snapshot().rejected_epoch == 2 && !timing.snapshot().packets);
    return true;
}

// Catch a partial DMA fill double-counting real audio, failing to count its
// zero-filled tail, or reading stale data after the real PCM ring wraps.
static bool outputAccounting() {
    int32_t memory[16] = {}, pcm[18], dma[16];
    for (int i = 0; i < 9; ++i) { pcm[2*i] = (i+1)*256; pcm[2*i+1] = -(i+1)*256; }
    I2sRing ring(memory, 8); OutputTiming timing; timing.beginEpoch(9);
    CHECK(ring.push(pcm, 4) == 4); auto before = ring.queued(); auto real = ring.fill(dma, 3);
    CHECK(timing.refill(9, 1000, 3, uint32_t(real), uint32_t(before)));
    CHECK(ring.push(pcm+8, 5) == 5); before = ring.queued(); real = ring.fill(dma, 8);
    CHECK(timing.refill(9, 2000, 8, uint32_t(real), uint32_t(before)));
    for (unsigned i = 0; i < 12; ++i) CHECK(dma[i] == pcm[i+6]);
    for (unsigned i = 12; i < 16; ++i) CHECK(dma[i] == 0);
    before = ring.queued(); real = ring.fill(dma, 3);
    CHECK(timing.refill(9, 3000, 3, uint32_t(real), uint32_t(before)));
    auto s = timing.snapshot();
    CHECK(s.refills == 3 && s.real_frames == 9 && s.silent_frames == 5 && s.underruns == 2);
    CHECK(s.minimum_queued == 0 && s.record_count == 2);
    CHECK(s.records[0].refill_number == 2 && s.records[0].queued_before == 6);
    CHECK(s.records[0].real_frames == 6 && s.records[0].silent_frames == 2);
    CHECK(s.records[1].refill_number == 3 && s.records[1].silent_frames == 3);
    CHECK(!timing.refill(9, 4000, 3, 4, 4));
    CHECK(!timing.refill(9, 4000, 3, 2, 1)); CHECK(!timing.refill(9, 4000, 0, 0, 0));
    CHECK(timing.snapshot().invalid_sample == 3 && timing.snapshot().refills == 3);
    return true;
}

// Catch first-event evidence being overwritten when a prolonged outage exceeds
// the bounded record capacity; aggregate accounting must continue past it.
static bool outputHistoryAndEpoch() {
    OutputTiming timing; timing.beginEpoch(3);
    for (unsigned i = 0; i < 18; ++i) CHECK(timing.refill(3, 0xfffffff0u+i*1000, 192, 0, 0));
    auto s = timing.snapshot();
    CHECK(s.refills == 18 && s.underruns == 18 && s.silent_frames == 3456);
    CHECK(s.record_count == 16 && s.records_not_retained == 2);
    CHECK(s.records[0].refill_number == 1 && s.records[0].at_us == 0xfffffff0u);
    CHECK(s.records[15].refill_number == 16 && s.records[15].at_us == 14984);
    timing.endEpoch(3); CHECK(!timing.refill(3, 18000, 192, 0, 0));
    timing.beginEpoch(4); timing.endEpoch(3); CHECK(timing.snapshot().active);
    CHECK(!timing.refill(3, 18000, 192, 0, 0));
    CHECK(timing.refill(4, 19000, 192, 192, 300));
    auto next = timing.snapshot();
    CHECK(next.refills == 1 && next.real_frames == 192 && !next.silent_frames);
    CHECK(!next.record_count && next.minimum_queued == 300 && next.rejected_epoch == 2);
    CHECK(s.refills == 18); // Snapshot publication cannot expose live storage.
    return true;
}

// A hand-scheduled burst trace delivers every RTP packet in sequence but delays
// three5ms frames to t=15ms. The real20ms PCM ring emits exactly10ms of silence.
static bool syntheticBurstWithoutPacketLoss() {
    int32_t memory[7680] = {}, pcm[1920], dma[384];
    for (auto& sample : pcm) sample = 256;
    I2sRing ring(memory, 3840); RtpTiming ingress; OutputTiming output;
    ingress.beginEpoch(11); output.beginEpoch(11);
    ingress.observe(11, 1, 1, 0, 0); CHECK(ring.push(pcm, 960) == 960);
    for (unsigned ms = 0; ms < 30; ++ms) {
        if (ms == 15) {
            for (unsigned packet = 0; packet < 3; ++packet) {
                ingress.observe(11, 1, uint16_t(packet+2), (packet+1)*960, 15000);
                CHECK(ring.push(pcm, 960) == 960);
            }
        }
        const auto before = ring.queued(); const auto real = ring.fill(dma, 192);
        CHECK(output.refill(11, ms*1000, 192, uint32_t(real), uint32_t(before)));
    }
    auto r = ingress.snapshot(); auto o = output.snapshot();
    CHECK(r.packets == 4 && !r.missing_sequences && !r.forward_gaps);
    CHECK(r.arrival_gaps.maximum_us == 15000 && r.arrival_gaps.buckets[0] == 2);
    CHECK(o.real_frames == 3840 && o.silent_frames == 1920 && o.underruns == 10);
    CHECK(o.real_frames + o.silent_frames == 5760 && o.record_count == 10);
    CHECK(o.records[0].at_us == 5000 && o.records[9].at_us == 14000 && ring.queued() == 0);
    return true;
}

int main() {
    const bool results[] = {rtpContinuity(),arrivalHistogram(),queueOwnership(),packetWork(),outputAccounting(),outputHistoryAndEpoch(),syntheticBurstWithoutPacketLoss()};
    unsigned failed = 0; for (bool result : results) if (!result) ++failed;
    std::printf("Audio timing: %u passed, %u failed\n", unsigned(sizeof(results)/sizeof(results[0]))-failed, failed);
    return failed ? 1 : 0;
}
