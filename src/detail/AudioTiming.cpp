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
bool knownSamples(const RtpPacketDetail& packet) {
    const uint8_t required=RtpLhdcPayload|RtpInnerSequenceValid|RtpDeclaredFramesValid|RtpScannedFramesValid|RtpSamplesValid;
    return (packet.flags&required)==required&&!(packet.flags&(RtpPayloadFragmented|RtpPayloadMalformed))&&
        (packet.sample_hop==240||packet.sample_hop==480||packet.sample_hop==960)&&
        packet.scanned_frames>0&&packet.scanned_frames<=15&&packet.scanned_frames==packet.declared_frames&&
        packet.sample_frames==uint32_t(packet.sample_hop)*packet.scanned_frames;
}
}

RtpPacketDetail lhdcRtpDetail(const uint8_t* payload, size_t bytes, uint32_t hop) {
    RtpPacketDetail out; out.flags=RtpLhdcPayload;
    out.payload_bytes=uint16_t(bytes>65535?65535:bytes);
    // The V5 sender's nonfragmented header encodes count<<2 followed by its
    // independent packet sequence. At most15 self-delimiting frames are read.
    if (!payload || bytes<2 || bytes>2+15*1538) { out.flags|=RtpPayloadMalformed;return out; }
    out.header=payload[0];out.inner_sequence=payload[1];out.flags|=RtpInnerSequenceValid;
    if (out.header&0xc0) { out.flags|=RtpPayloadFragmented;return out; }
    out.declared_frames=uint8_t((out.header>>2)&15);
    if ((out.header&3) || !out.declared_frames) { out.flags|=RtpPayloadMalformed;return out; }
    out.flags|=RtpDeclaredFramesValid;
    size_t offset=2;
    for (unsigned i=0;i<out.declared_frames;++i) {
        if (bytes-offset<2) { out.flags|=RtpPayloadMalformed;return out; }
        const unsigned header=unsigned(payload[offset])|(unsigned(payload[offset+1])<<8);
        const size_t frame=2+(header&1023)*2;
        if (frame<20 || frame>1538 || frame>bytes-offset) { out.flags|=RtpPayloadMalformed;return out; }
        offset+=frame;++out.scanned_frames;
    }
    if (offset!=bytes) { out.flags|=RtpPayloadMalformed;return out; }
    out.flags|=RtpScannedFramesValid;
    if (hop==240 || hop==480 || hop==960) {
        out.sample_hop=uint16_t(hop);out.sample_frames=hop*out.scanned_frames;out.flags|=RtpSamplesValid;
    }
    return out;
}
void RtpTiming::record(uint32_t ssrc, uint16_t sequence, uint32_t timestamp, uint32_t arrival,
                       RtpPacketDetail packet, RtpObservation observation, bool clock_bad) {
    uint16_t reasons=0;
    if (packet.flags&RtpLhdcPayload) {
        if (!knownSamples(packet)) { ++stats_.unknown_payloads;reasons|=RtpPayloadUnknown; }
        if (packet.flags&RtpPayloadMalformed) ++stats_.malformed_payloads;
    }
    if (observation==RtpObservation::SourceChange) reasons|=RtpSourceChanged;
    if (observation==RtpObservation::Duplicate) reasons|=RtpDuplicatePacket;
    if (observation==RtpObservation::Backward) reasons|=RtpBackwardPacket;
    const bool same_source=have_source_&&ssrc==stats_.ssrc;
    const uint16_t step=uint16_t(sequence-stats_.high_sequence);
    if (observation==RtpObservation::Forward&&step>1) reasons|=RtpSequenceGap;
    if (clock_bad) reasons|=RtpArrivalClock;
    // A missing RTP packet has an unknown sample extent. Duplicate/backward
    // packets never replace the forward anchor. Unknown framing on EITHER end
    // also prevents timestamp-span or inner-sequence interpretation.
    const bool expected=same_source&&observation==RtpObservation::Forward&&step==1&&
        knownSamples(previous_packet_)&&knownSamples(packet)&&
        previous_packet_.sample_hop==packet.sample_hop;
    const uint32_t delta=timestamp-stats_.source_last_timestamp;
    if (expected&&delta!=previous_packet_.sample_frames) {
        ++stats_.timestamp_discontinuities;reasons|=RtpTimestampStep;
    }
    if (expected&&(previous_packet_.flags&RtpInnerSequenceValid)&&(packet.flags&RtpInnerSequenceValid)&&
        uint8_t(packet.inner_sequence-previous_packet_.inner_sequence)!=1) {
        ++stats_.inner_sequence_discontinuities;reasons|=RtpInnerSequenceStep;
    }
    if (!reasons) return;
    ++stats_.discontinuities;
    if (stats_.record_count==16) { ++stats_.records_not_retained;return; }
    auto& out=stats_.records[stats_.record_count++];out={};
    out.packet_number=stats_.packets;out.previous_ssrc=have_source_?stats_.ssrc:0;out.ssrc=ssrc;
    out.previous_sequence=have_source_?stats_.high_sequence:0;out.sequence=sequence;
    out.previous_timestamp=have_source_?stats_.source_last_timestamp:0;out.timestamp=timestamp;
    out.previous_arrival_us=have_source_?stats_.source_last_arrival_us:0;out.arrival_us=arrival;
    const uint32_t arrival_delta=arrival-stats_.source_last_arrival_us;
    out.arrival_delta_valid=same_source&&arrival_delta<halfClock;
    if (out.arrival_delta_valid) out.arrival_delta_us=arrival_delta;
    out.expected_delta_valid=expected;
    if (expected) out.expected_timestamp_delta=previous_packet_.sample_frames;
    out.timestamp_delta_valid=expected&&delta<halfClock;
    if (out.timestamp_delta_valid) out.timestamp_delta=delta;
    out.previous_packet=previous_packet_;out.packet=packet;out.reasons=reasons;
}
void RtpTiming::beginEpoch(uint32_t epoch) {
    stats_ = {}; stats_.epoch = epoch; stats_.active = epoch != 0;
    previous_arrival_ = 0; have_arrival_ = have_source_ = false;
    previous_packet_ = {};
}
void RtpTiming::endEpoch(uint32_t epoch) { end(stats_, epoch); }
RtpObservation RtpTiming::observe(uint32_t epoch, uint32_t ssrc, uint16_t sequence,
                                 uint32_t timestamp, uint32_t arrival, RtpPacketDetail packet) {
    if (!accepts(stats_, epoch)) return RtpObservation::Rejected;
    ++stats_.packets;
    bool clock_bad=false;
    if (have_arrival_) {
        const uint32_t gap = arrival - previous_arrival_;
        if (gap < halfClock) duration(stats_.arrival_gaps, gap);
        else { ++stats_.clock_discontinuities;clock_bad=true; }
    }
    previous_arrival_ = arrival; have_arrival_ = true;
    if (!have_source_ || stats_.ssrc != ssrc) {
        const bool changed = have_source_;
        record(ssrc,sequence,timestamp,arrival,packet,changed?RtpObservation::SourceChange:RtpObservation::First,clock_bad);
        if (changed) ++stats_.source_changes;
        stats_.ssrc = ssrc; stats_.high_sequence = sequence;
        stats_.source_packets = 1;
        stats_.source_first_timestamp = stats_.source_last_timestamp = timestamp;
        stats_.source_first_arrival_us = stats_.source_last_arrival_us = arrival;
        have_source_ = true;
        previous_packet_=packet;
        return changed ? RtpObservation::SourceChange : RtpObservation::First;
    }
    const uint16_t step = uint16_t(sequence - stats_.high_sequence);
    if (!step) { ++stats_.duplicates;record(ssrc,sequence,timestamp,arrival,packet,RtpObservation::Duplicate,clock_bad);return RtpObservation::Duplicate; }
    if (step >= 0x8000u) { ++stats_.backward;record(ssrc,sequence,timestamp,arrival,packet,RtpObservation::Backward,clock_bad);return RtpObservation::Backward; }
    record(ssrc,sequence,timestamp,arrival,packet,RtpObservation::Forward,clock_bad);
    if (step > 1) { ++stats_.forward_gaps; stats_.missing_sequences += step - 1; }
    stats_.high_sequence = sequence; ++stats_.source_packets;
    stats_.source_last_timestamp = timestamp; stats_.source_last_arrival_us = arrival;
    previous_packet_=packet;
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
