#pragma once
#include <stddef.h>
#include <stdint.h>
namespace giga_bredr { namespace detail {
// Unity bypass preserves every source bit. This is applied once only when the
// sink advertises AVRCP absolute-volume rendering; external amplifiers may opt out.
inline void scaleStereoVolume(int32_t* pcm, size_t frames, uint8_t volume) {
    if (!pcm || volume >= 127 || frames > SIZE_MAX / 2) return;
    for (size_t i = 0; i < frames * 2; ++i)
        pcm[i] = int32_t(int64_t(pcm[i]) * volume / 127);
}
}}
