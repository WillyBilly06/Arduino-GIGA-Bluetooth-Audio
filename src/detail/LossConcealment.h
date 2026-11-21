#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace giga_bredr { namespace detail {
// RTP media-packet loss concealment for interleaved stereo int32 PCM.
//
// A source that drops packets (e.g. PipeWire skipping a packet when the L2CAP
// socket is full) leaves a sequence gap. Without fill, the next packet plays
// immediately and the PCM reserve shrinks by the lost duration; repeated losses
// drain it and the output underruns into a long recovery hold. Filling the gap
// with exactly the lost duration keeps the reserve level.
//
// Fill: the last real block is replayed back and forth from its last sample
// (value-continuous at every turn, no wrap click) under a linear 1->0 envelope
// spanning the whole gap, so the fill always ends silent. The next real PCM
// then fades in over fade_in frames. No allocation; caller provides storage.
class LossConcealment {
public:
    // Largest gap filled; longer gaps are treated as stream interruptions.
    // A source that discards a run of queued packets (PipeWire on a full socket) still keeps
    // its timeline, so even long runs are filled rather than drained.
    static constexpr uint32_t MaximumPackets = 64, MaximumMs = 400;

    void attach(int32_t* storage, uint32_t capacity_frames, uint32_t sample_rate) {
        src_ = storage; cap_ = storage ? capacity_frames : 0; rate_ = sample_rate;
        fade_ = sample_rate / 400u; if (fade_ < 16u) fade_ = 16u; // 2.5 ms
        reset();
    }
    void reset() {
        len_ = 0; fade_left_ = 0; seq_valid_ = false; unfilled_ = false; packet_frames_ = 0;
        ts_valid_ = false; ts_fill_ = false; ratio_q16_ = 0; candidate_q16_ = 0; stable_ = 0;
    }

    // Sequence bookkeeping per received RTP packet. Returns the PCM frames to
    // fill before this packet (0: none). Duplicates/backward steps fill nothing.
    uint32_t onPacket(uint16_t sequence) {
        uint32_t fill = 0; unfilled_ = false; last_lost_ = 0; ts_fill_ = false; last_step_ = 0;
        if (seq_valid_) {
            const uint16_t step = uint16_t(sequence - last_seq_);
            if (step == 0 || step >= 0x8000u) return 0; // duplicate / reordered: keep last
            last_step_ = step;
            const uint32_t lost = step - 1u; last_lost_ = lost;
            if (lost && lost <= MaximumPackets && packet_frames_) {
                const uint64_t frames = uint64_t(lost) * packet_frames_;
                if (frames <= uint64_t(rate_) * MaximumMs / 1000u) fill = uint32_t(frames);
            }
            unfilled_ = lost && !fill; // A gap the timeline could not be kept across.
        }
        last_seq_ = sequence; seq_valid_ = true;
        return fill;
    }
    // Sequence and RTP timestamp. A sender whose transmit queue overflows (an
    // Android A2DP source on a congested link) drops whole encoded packets
    // before sequence numbers are assigned: the sequence stays continuous while
    // the media timestamp jumps by the dropped audio. The timestamp step per
    // decoded frame is learned per stream (it must repeat for StableSteps
    // packets); a jump past the previous packet's duration is then filled with
    // exactly the missing frames, up to MaximumMs. Longer jumps (pause, seek)
    // fill nothing and are not counted as unfilled.
    static constexpr uint32_t StableSteps = 8;
    uint32_t onPacket(uint16_t sequence, uint32_t timestamp) {
        const bool had_ts = ts_valid_ && seq_valid_;
        const uint32_t previous_ts = last_ts_;
        uint32_t fill = onPacket(sequence);
        if (seq_valid_ && (last_step_ || !had_ts)) { last_ts_ = timestamp; ts_valid_ = true; }
        if (!had_ts || last_step_ != 1u || !packet_frames_) return fill;
        const uint32_t delta = timestamp - previous_ts;
        if (ratio_q16_) {
            const uint64_t expected = (uint64_t(packet_frames_) * ratio_q16_ + 0x8000u) >> 16;
            if (delta > expected + expected / 2u && delta < 0x80000000u) {
                const uint64_t frames = ((uint64_t(delta) - expected) << 16) / ratio_q16_;
                if (frames && frames <= uint64_t(rate_) * MaximumMs / 1000u) { ts_fill_ = true; return uint32_t(frames); }
                return fill;
            }
        }
        // Learn ticks per decoded frame from in-order packets (Q16).
        const uint64_t q = (uint64_t(delta) << 16) / packet_frames_;
        if (!q || q > (uint64_t(64) << 16)) { stable_ = 0; return fill; }
        const uint64_t tolerance = candidate_q16_ / 64u + 1u;
        if (stable_ && q + tolerance >= candidate_q16_ && q <= candidate_q16_ + tolerance) {
            if (++stable_ >= StableSteps && !ratio_q16_) ratio_q16_ = candidate_q16_;
        } else if (!ratio_q16_) { candidate_q16_ = q; stable_ = 1; }
        return fill;
    }
    // True when the last onPacket() fill came from a timestamp jump (no sequence gap).
    bool lastFillFromTimestamp() const { return ts_fill_; }
    // Learned RTP ticks per decoded frame, Q16 (0: not learned yet).
    uint32_t timestampRatioQ16() const { return uint32_t(ratio_q16_); }
    // Real PCM frames decoded from the previous complete packet.
    void packetFrames(uint32_t frames) { if (frames) packet_frames_ = frames; }

    // Keep the tail of real output (after volume/mono expansion).
    void remember(const int32_t* pcm, uint32_t frames) {
        if (!cap_ || !pcm || !frames) return;
        const uint32_t n = frames < cap_ ? frames : cap_;
        memcpy(src_, pcm + size_t(frames - n) * 2, size_t(n) * 8);
        len_ = n;
    }
    // Writes fill frames [position, position+n) of a gap of total frames.
    void synthesize(int32_t* out, uint32_t position, uint32_t n, uint32_t total) const {
        if (!out || !n) return;
        if (!len_ || !total) { memset(out, 0, size_t(n) * 8); return; }
        const uint32_t period = 2u * len_;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t k = position + i;
            const uint32_t m = k % period;
            const uint32_t idx = m < len_ ? len_ - 1u - m : m - len_;
            const uint32_t left = k < total ? total - k : 0u;
            const uint32_t gain = uint32_t((uint64_t(left) << 16) / total); // Q16, 1 -> 0
            out[2 * i] = scale(src_[2 * idx], gain);
            out[2 * i + 1] = scale(src_[2 * idx + 1], gain);
        }
    }
    void beginFadeIn() { fade_left_ = fade_; }
    // Applies the pending fade-in to the start of real PCM.
    void applyFadeIn(int32_t* pcm, uint32_t frames) {
        for (uint32_t i = 0; i < frames && fade_left_; ++i, --fade_left_) {
            const uint32_t gain = uint32_t((uint64_t(fade_ - fade_left_) << 16) / fade_);
            pcm[2 * i] = scale(pcm[2 * i], gain);
            pcm[2 * i + 1] = scale(pcm[2 * i + 1], gain);
        }
    }
    // True when the last onPacket() saw lost packets it did not fill.
    bool unfilledGap() const { return unfilled_; }
    uint32_t lastLost() const { return last_lost_; }
    uint32_t fadeFrames() const { return fade_; }
    uint32_t rememberedFrames() const { return len_; }

private:
    static int32_t scale(int32_t v, uint32_t q16) {
        const int64_t p = int64_t(v) * int64_t(q16);
        return int32_t(p < 0 ? -((-p) >> 16) : (p >> 16)); // symmetric toward zero
    }
    int32_t* src_ = nullptr;
    uint32_t cap_ = 0, len_ = 0, rate_ = 48000, fade_ = 120, fade_left_ = 0, packet_frames_ = 0;
    uint16_t last_seq_ = 0, last_step_ = 0; uint32_t last_lost_ = 0, last_ts_ = 0, stable_ = 0;
    uint64_t ratio_q16_ = 0, candidate_q16_ = 0;
    bool seq_valid_ = false, unfilled_ = false, ts_valid_ = false, ts_fill_ = false;
};
}}
