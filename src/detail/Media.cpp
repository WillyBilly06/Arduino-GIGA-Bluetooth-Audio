#include "Media.h"
#include "Memory.h"
#include <string.h>
namespace giga_bredr { namespace detail {
namespace {
uint16_t be16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t be32(const uint8_t* p) { return uint32_t(be16(p)) << 16 | be16(p + 2); }
bool sbcFrames(const uint8_t* p, size_t n, unsigned count) {
    if (!count) return false;
    while (count--) {
        if (n < 4 || p[0] != 0x9c) return false;
        unsigned mode = (p[1] >> 2) & 3, channels = mode == 0 ? 1 : 2;
        unsigned bands = (p[1] & 1) ? 8 : 4, blocks = 4 * (((p[1] >> 4) & 3) + 1);
        unsigned pool = p[2];
        if (pool < 2 || pool > 250 || pool > (mode < 2 ? 16 : 32) * bands) return false;
        unsigned audio = blocks * pool * (mode < 2 ? channels : 1) + (mode == 3 ? bands : 0);
        size_t frame = 4 + bands * channels / 2 + (audio + 7) / 8;
        if (frame > n) return false;
        p += frame; n -= frame;
    }
    return n == 0;
}
bool ldacFrames(const uint8_t* p, size_t n, unsigned count) {
    if (!count) return false;
    while (count--) {
        if (n < 3 || p[0] != 0xaa) return false;
        size_t frame = 4 + ((p[1] & 7u) << 6) + (p[2] >> 2);
        if (frame > n || frame > 330) return false;
        p += frame; n -= frame;
    }
    return n == 0;
}
}
Status parseRtp(const uint8_t* p, size_t n, RtpView& out) {
    out = {};
    if (!p || n < 12 || (p[0] >> 6) != 2) return Status::CorruptFrame;
    size_t off = 12 + 4 * (p[0] & 15);
    if (off > n) return Status::CorruptFrame;
    if (p[0] & 0x10) {
        if (n - off < 4) return Status::CorruptFrame;
        size_t length = 4 + size_t(be16(p + off + 2)) * 4;
        if (length > n - off) return Status::CorruptFrame;
        off += length;
    }
    size_t end = n;
    if (p[0] & 0x20) {
        size_t padding = p[n - 1];
        if (!padding || padding > n - off) return Status::CorruptFrame;
        end -= padding;
    }
    if (off >= end) return Status::CorruptFrame;
    out.payload = p + off; out.bytes = end - off;
    out.sequence = be16(p + 2); out.timestamp = be32(p + 4); out.marker = (p[1] & 0x80) != 0;
    return Status::Ok;
}
Status MediaAssembler::configure(const CodecConfig& c, size_t cap, MemoryKind kind) {
    close();
    if (!cap || cap > 65535 || static_cast<unsigned>(c.codec) >= static_cast<unsigned>(Codec::Count)) return Status::InvalidArgument;
    codec_ = c.codec; capacity_ = cap;
    if (codec_ == Codec::Sbc || codec_ == Codec::Aac || codec_ == Codec::AacHe ||
        codec_ == Codec::Opus || codec_ == Codec::LhdcV5) {
        assembly_ = static_cast<uint8_t*>(allocate(cap, kind));
        if (!assembly_) { close(); return Status::OutOfMemory; }
    }
    return Status::Ok;
}
void MediaAssembler::consumed() { used_ = 0; fragmented_ = false; fragment_remaining_ = 0; if (!++assembly_generation_) ++assembly_generation_; }
void MediaAssembler::reset() { consumed(); have_sequence_ = false; have_timestamp_ = false; }
void MediaAssembler::close() { release(assembly_); assembly_ = nullptr; capacity_ = 0; codec_ = Codec::Count; reset(); }
Status MediaAssembler::feed(const uint8_t* packet, size_t length, MediaView& view) {
    view = {};
    if (codec_ == Codec::Count || !packet || !length) return Status::InvalidArgument;
    if (codec_ == Codec::Aptx) {
        if (length % 4 || length > capacity_) return Status::CorruptFrame;
        view = {packet, length}; return Status::Ok;
    }
    RtpView r;
    Status status = parseRtp(packet, length, r);
    if (status != Status::Ok) { consumed(); return status; }
    bool gap = have_sequence_ && r.sequence != uint16_t(previous_sequence_ + 1);
    bool timestamp_change = have_timestamp_ && r.timestamp != timestamp_;
    previous_sequence_ = r.sequence; have_sequence_ = true;
    if (codec_ == Codec::Aac || codec_ == Codec::AacHe) {
        if (gap || timestamp_change) consumed();
        timestamp_ = r.timestamp; have_timestamp_ = true;
        if (r.bytes > capacity_ - used_) { consumed(); return Status::OutputFull; }
        memcpy(assembly_ + used_, r.payload, r.bytes); used_ += r.bytes;
        view = {assembly_, used_}; return Status::Ok;
    }
    const uint8_t* data = r.payload; size_t bytes = r.bytes;
    if (codec_ == Codec::AptxHd) {
        if (bytes % 6 || bytes > capacity_) return Status::CorruptFrame;
        view = {data, bytes}; return Status::Ok;
    }
    if (bytes < 2) { consumed(); return Status::CorruptFrame; }
    uint8_t header = *data++; --bytes;
    unsigned count = header & 15;
    if (codec_ == Codec::Ldac) {
        if ((header & 0xf0) || !ldacFrames(data, bytes, count)) return Status::CorruptFrame;
        view = {data, bytes}; return Status::Ok;
    }
    bool is_fragment = (header & 0x80) != 0, first = (header & 0x40) != 0, last = (header & 0x20) != 0;
    if (is_fragment) {
        if (first) { consumed(); fragmented_ = true; timestamp_ = r.timestamp; have_timestamp_ = true; }
        else if (!fragmented_ || gap || timestamp_change) { consumed(); return Status::CorruptFrame; }
        if (codec_ != Codec::Sbc) {
            if (!count || (!first && count + 1 != fragment_remaining_) || (last && count != 1)) {
                consumed(); return Status::CorruptFrame;
            }
            fragment_remaining_ = uint8_t(count);
        }
        if (!assembly_ || bytes > capacity_ - used_) { consumed(); return Status::OutputFull; }
        memcpy(assembly_ + used_, data, bytes); used_ += bytes;
        if (!last) return Status::NeedMoreData;
        data = assembly_; bytes = used_; fragmented_ = false; used_ = 0;
        count = 1;
    } else { consumed(); }
    if (codec_ == Codec::Sbc) {
        if ((header & 0x10) || (!is_fragment && (first || last)) || !sbcFrames(data, bytes, count)) return Status::CorruptFrame;
    } else if (codec_ == Codec::Opus) {
        if ((!is_fragment && count != 1) || (header & 0x10)) return Status::CorruptFrame;
    } else if (codec_ == Codec::LhdcV5) {
        if (bytes < 3) return Status::CorruptFrame;
        ++data; --bytes; // LHDC's separate sequence/marker byte precedes self-delimiting frames.
    } else return Status::Unsupported;
    if (!bytes || bytes > capacity_) return Status::CorruptFrame;
    view = {data, bytes}; return Status::Ok;
}
}}
