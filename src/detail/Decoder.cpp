#include "Decoder.h"
#include <atomic>
namespace giga_bredr { namespace detail {
namespace { std::atomic<Decoder*> owner{nullptr}; }
Status Decoder::open(const CodecConfig& c) {
    close();
    if (!c.sample_rate || (c.channels != 1 && c.channels != 2) ||
        (c.bits_per_sample != 16 && c.bits_per_sample != 24 && c.bits_per_sample != 32) ||
        c.codec_info_size > sizeof(c.codec_info)) return Status::InvalidArgument;
    ops_ = codecOperations(c.codec);
    if (!ops_) return Status::Unsupported;
    Decoder* expected = nullptr;
    if (!owner.compare_exchange_strong(expected, this)) { ops_ = nullptr; return Status::Busy; }
    Status s = ops_->open(&state_, c);
    if (s != Status::Ok || !state_) { if (state_) ops_->close(state_); state_ = nullptr; ops_ = nullptr; owner.store(nullptr); return s == Status::Ok ? Status::HardwareError : s; }
    config_ = c;
    return Status::Ok;
}
DecodeResult Decoder::decode(const uint8_t* input, size_t bytes, int32_t* pcm, size_t capacity) {
    if (!state_ || !ops_ || !input || !bytes || !pcm || !capacity) return {Status::InvalidArgument, 0, 0};
    return ops_->decode(state_, input, bytes, pcm, capacity);
}
void Decoder::close() {
    if (state_ && ops_) ops_->close(state_);
    state_ = nullptr; ops_ = nullptr;
    Decoder* expected = this;
    owner.compare_exchange_strong(expected, nullptr);
}
}}
