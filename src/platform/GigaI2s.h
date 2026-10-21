#pragma once
#include "../Types.h"
#include "../detail/AudioTiming.h"
namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t sample_rate);
size_t i2sWrite(const int32_t* interleaved_stereo_pcm, size_t frames);
void i2sEnd();
OutputStats i2sStats();
OutputTimingSnapshot i2sTiming();
}}
