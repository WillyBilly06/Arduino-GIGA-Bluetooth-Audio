#include "AudioTiming.h"
namespace giga_bredr { namespace detail {
namespace {
constexpr uint32_t halfClock = 0x80000000u;
void duration(DurationHistogram& out, uint32_t us) {
    const uint32_t upper[] = {1000,2000,5000,10000,20000,50000};
    unsigned bucket = 0;
    while (bucket < 6 && us > upper[bucket]) ++bucket;
    ++out.buckets[bucket]; ++out.samples; out.total_us += us;
    if (us > out.maximum_us) out.maximum_us = us;
}
template<class Snapshot> bool accepts(Snapshot& stats, uint32_t epoch) {
    if (stats.active && epoch && epoch == stats.epoch) return true;
    ++stats.rejected_epoch; return false;
}
template<class Snapshot> void end(Snapshot& stats, uint32_t epoch) {
    if (epoch && epoch == stats.epoch) stats.active = false;
    else ++stats.rejected_epoch;
}
}

void RtpTiming::beginEpoch(uint32_t epoch) {
    stats_ = {}; stats_.epoch = epoch; stats_.active = epoch != 0;
    previous_arrival_ = 0; have_arrival_ = have_source_ = false;
}
void RtpTiming::endEpoch(uint32_t epoch) { end(stats_, epoch); }
RtpObservation RtpTiming::observe(uint32_t epoch, uint32_t ssrc, uint16_t sequence,
                                 uint32_t timestamp, uint32_t arrival) {
    if (!accepts(stats_, epoch)) return RtpObservation::Rejected;
    ++stats_.packets;
    if (have_arrival_) {
        const uint32_t gap = arrival - previous_arrival_;
        if (gap < halfClock) duration(stats_.arrival_gaps, gap);
        else ++stats_.clock_discontinuities;
    }
    previous_arrival_ = arrival; have_arrival_ = true;
    if (!have_source_ || stats_.ssrc != ssrc) {
        const bool changed = have_source_;
        if (changed) ++stats_.source_changes;
        stats_.ssrc = ssrc; stats_.high_sequence = sequence;
        stats_.source_packets = 1;
        stats_.source_first_timestamp = stats_.source_last_timestamp = timestamp;
        stats_.source_first_arrival_us = stats_.source_last_arrival_us = arrival;
        have_source_ = true;
        return changed ? RtpObservation::SourceChange : RtpObservation::First;
    }
    const uint16_t step = uint16_t(sequence - stats_.high_sequence);
    if (!step) { ++stats_.duplicates; return RtpObservation::Duplicate; }
    if (step >= 0x8000u) { ++stats_.backward; return RtpObservation::Backward; }
    if (step > 1) { ++stats_.forward_gaps; stats_.missing_sequences += step - 1; }
    stats_.high_sequence = sequence; ++stats_.source_packets;
    stats_.source_last_timestamp = timestamp; stats_.source_last_arrival_us = arrival;
    return RtpObservation::Forward;
}

bool PacketQueueTiming::beginEpoch(uint32_t epoch, uint32_t capacity) {
    stats_ = {}; stats_.epoch = epoch; read_ = write_ = 0;
    if (!epoch || !capacity || capacity > 64) return false;
    stats_.capacity = capacity; stats_.active = true; return true;
}
void PacketQueueTiming::endEpoch(uint32_t epoch) {
    if (!epoch || epoch != stats_.epoch) { ++stats_.rejected_epoch; return; }
    stats_.discarded_on_end += stats_.depth; stats_.depth = 0;
    read_ = write_ = 0; stats_.active = false;
}
bool PacketQueueTiming::enqueued(uint32_t epoch, uint32_t arrival, QueueToken& token) {
    token = {};
    if (!accepts(stats_, epoch)) return false;
    if (stats_.depth == stats_.capacity) { ++stats_.full; return false; }
    if (!++serial_) ++serial_; // Keep the invalid-token sentinel reserved.
    entries_[write_] = {serial_, arrival};
    write_ = (write_ + 1) % stats_.capacity;
    ++stats_.enqueued; ++stats_.depth;
    if (stats_.depth > stats_.high_water) stats_.high_water = stats_.depth;
    token = {epoch, serial_}; return true;
}
bool PacketQueueTiming::dequeued(QueueToken token, uint32_t now) {
    if (!accepts(stats_, token.epoch)) return false;
    if (!stats_.depth || !token.serial || entries_[read_].serial != token.serial) {
        ++stats_.invalid_token; return false;
    }
    const uint32_t residence = now - entries_[read_].birth_us;
    read_ = (read_ + 1) % stats_.capacity; --stats_.depth; ++stats_.dequeued;
    if (residence < halfClock) duration(stats_.residence, residence);
    else ++stats_.clock_discontinuities;
    return true;
}

void PacketWorkTiming::beginEpoch(uint32_t epoch) {
    stats_ = {}; stats_.epoch = epoch; stats_.active = epoch != 0;
}
void PacketWorkTiming::endEpoch(uint32_t epoch) { end(stats_, epoch); }
bool PacketWorkTiming::record(uint32_t epoch, PacketWorkSample sample) {
    if (!accepts(stats_, epoch)) return false;
    if (sample.elapsed_us >= halfClock ||
        uint64_t(sample.decode_work_us) + sample.write_wait_us > sample.elapsed_us) {
        ++stats_.invalid_sample; return false;
    }
    ++stats_.packets;
    duration(stats_.decode_work, sample.decode_work_us);
    duration(stats_.write_wait, sample.write_wait_us);
    duration(stats_.elapsed, sample.elapsed_us); return true;
}

void OutputTiming::beginEpoch(uint32_t epoch) {
    stats_ = {}; stats_.epoch = epoch; stats_.active = epoch != 0;
}
void OutputTiming::endEpoch(uint32_t epoch) { end(stats_, epoch); }
bool OutputTiming::refill(uint32_t epoch, uint32_t at, uint32_t requested,
                         uint32_t real, uint32_t queued) {
    if (!accepts(stats_, epoch)) return false;
    if (!requested || real > requested || real > queued) { ++stats_.invalid_sample; return false; }
    ++stats_.refills; stats_.real_frames += real;
    const uint32_t silent = requested - real; stats_.silent_frames += silent;
    if (stats_.refills == 1 || queued < stats_.minimum_queued) stats_.minimum_queued = queued;
    if (silent) {
        ++stats_.underruns;
        if (stats_.record_count < 16) {
            stats_.records[stats_.record_count++] = {stats_.refills, at, queued, real, silent};
        } else ++stats_.records_not_retained;
    }
    return true;
}
}}
