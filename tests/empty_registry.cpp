#include "detail/Decoder.h"
#include "detail/Memory.h"
namespace giga_bredr { namespace detail {
namespace {
Status open(void** state, const CodecConfig& c) {
    *state = gbr_malloc(64);
    if (*state) static_cast<uint8_t*>(*state)[0] = static_cast<uint8_t>(c.codec);
    return !*state || c.bitrate == 1 ? Status::OutOfMemory : Status::Ok;
}
DecodeResult decode(void* state, const uint8_t*, size_t n, int32_t* pcm, size_t cap) {
    if (static_cast<uint8_t*>(state)[0] == static_cast<uint8_t>(Codec::LhdcV5)) {
        if (cap < 240) return {Status::OutputFull, 0, 0};
        for (size_t i = 0; i < 480; ++i) pcm[i] = -655360;
        return {Status::Ok, n, 240};
    }
    pcm[0] = -655360; return {Status::Ok, n, 1};
}
void close(void* state) { gbr_free(state); }
const DecoderOps fake = {open, decode, close};
}
const DecoderOps* codecOperations(Codec c) { return c == Codec::Sbc || c == Codec::LhdcV5 ? &fake : nullptr; }
}}
