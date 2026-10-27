#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
struct DecoderOps {
    Status (*open)(void** state, const CodecConfig& config);
    DecodeResult (*decode)(void* state, const uint8_t* input, size_t bytes,
                           int32_t* pcm, size_t frame_capacity);
    void (*close)(void* state);
};
const DecoderOps* codecOperations(Codec codec);
class Decoder {
public:
    Decoder() = default;
    ~Decoder() { close(); }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Status open(const CodecConfig& config);
    DecodeResult decode(const uint8_t* input, size_t bytes, int32_t* pcm, size_t frame_capacity);
    void close();
    bool active() const { return state_ != nullptr; }
    const CodecConfig& configuration() const { return config_; }
private:
    const DecoderOps* ops_ = nullptr;
    void* state_ = nullptr;
    CodecConfig config_{};
};
}}
