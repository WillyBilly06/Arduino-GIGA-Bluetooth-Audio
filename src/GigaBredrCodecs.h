#pragma once
#include <stdint.h>
#include "Types.h"
// Choosing which decoders are compiled into a sketch.
//
// By default every decoder enabled in CodecBuildConfig.h is linked. To keep
// only some of them, put ONE line at global scope in the sketch:
//
//     GIGA_BREDR_CODECS(Sbc, Aac, LhdcV5);
//
// Names: Sbc (SBC and SBC-XQ), Aac (AAC-LC), AacHe (HE-AAC v1), Aptx,
// AptxHd, Ldac, LhdcV5, Opus, Ssc. The linker then drops every other decoder
// (flash is only spent on what you list) and the receiver only offers the
// listed codecs to phones. SBC is mandatory for A2DP; keep it in the list.
namespace giga_bredr {
namespace detail { struct DecoderOps; }
struct CodecEntry { Codec codec; const detail::DecoderOps* ops; };
struct CodecTable { const CodecEntry* const* entries; uint32_t count; };
namespace codecs {
extern const CodecEntry Sbc, Aac, AacHe, Aptx, AptxHd, Ldac, LhdcV5, Opus, Ssc;
}
}
extern const giga_bredr::CodecTable gbr_codec_table;

#define GBR_CODEC_ADDR_(name) &::giga_bredr::codecs::name
#define GBR_FE1_(m, a) m(a)
#define GBR_FE2_(m, a, ...) m(a), GBR_FE1_(m, __VA_ARGS__)
#define GBR_FE3_(m, a, ...) m(a), GBR_FE2_(m, __VA_ARGS__)
#define GBR_FE4_(m, a, ...) m(a), GBR_FE3_(m, __VA_ARGS__)
#define GBR_FE5_(m, a, ...) m(a), GBR_FE4_(m, __VA_ARGS__)
#define GBR_FE6_(m, a, ...) m(a), GBR_FE5_(m, __VA_ARGS__)
#define GBR_FE7_(m, a, ...) m(a), GBR_FE6_(m, __VA_ARGS__)
#define GBR_FE8_(m, a, ...) m(a), GBR_FE7_(m, __VA_ARGS__)
#define GBR_FE9_(m, a, ...) m(a), GBR_FE8_(m, __VA_ARGS__)
#define GBR_FE_PICK_(_1, _2, _3, _4, _5, _6, _7, _8, _9, name, ...) name
#define GBR_FOR_EACH_(m, ...) GBR_FE_PICK_(__VA_ARGS__, GBR_FE9_, GBR_FE8_, GBR_FE7_, GBR_FE6_, GBR_FE5_, \
    GBR_FE4_, GBR_FE3_, GBR_FE2_, GBR_FE1_, unused)(m, __VA_ARGS__)

#define GIGA_BREDR_CODECS(...) \
    static const ::giga_bredr::CodecEntry* const gbr_selected_codec_entries_[] = { GBR_FOR_EACH_(GBR_CODEC_ADDR_, __VA_ARGS__) }; \
    extern const ::giga_bredr::CodecTable gbr_codec_table = { gbr_selected_codec_entries_, \
        uint32_t(sizeof(gbr_selected_codec_entries_) / sizeof(gbr_selected_codec_entries_[0])) }
