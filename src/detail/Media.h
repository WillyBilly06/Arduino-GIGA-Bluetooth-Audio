#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
struct RtpView {
    const uint8_t* payload = nullptr; size_t bytes = 0;
    uint16_t sequence = 0; uint32_t timestamp = 0; bool marker = false;
};
Status parseRtp(const uint8_t* packet, size_t bytes, RtpView& result);
struct MediaView { const uint8_t* data = nullptr; size_t bytes = 0; };
class MediaAssembler {
public:
    ~MediaAssembler() { close(); }
    Status configure(const CodecConfig& config, size_t maximum_access_unit,
                     MemoryKind kind = MemoryKind::Fast);
    Status feed(const uint8_t* packet, size_t bytes, MediaView& view);
    // After AAC decode succeeds or fails irrecoverably, discard retained fragments.
    void consumed();
    void reset();
    void close();
private:
    Codec codec_ = Codec::Count;
    uint8_t* assembly_ = nullptr;
    size_t capacity_ = 0, used_ = 0;
    uint16_t previous_sequence_ = 0;
    uint32_t timestamp_ = 0;
    uint8_t fragment_remaining_ = 0;
    bool have_sequence_ = false, fragmented_ = false, have_timestamp_ = false;
};
}}
