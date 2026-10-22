#pragma once
#include "../Types.h"
#include "../I2sConfig.h"
#include "../detail/AudioTiming.h"
#include "PcmRecovery.h"
namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t sample_rate);
Status i2sBegin(uint32_t sample_rate, uint16_t pcm_buffer_ms);
Status i2sBegin(uint32_t sample_rate, uint16_t pcm_buffer_ms, const I2sPins& pins);
size_t i2sWrite(const int32_t* interleaved_stereo_pcm, size_t frames);
void i2sEnd();
OutputStats i2sStats();
OutputTimingSnapshot i2sTiming();
PcmRecoverySnapshot i2sRecovery();
}}
