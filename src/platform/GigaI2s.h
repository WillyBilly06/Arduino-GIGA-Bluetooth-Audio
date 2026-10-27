#pragma once
#include "../Types.h"
#include "../I2sConfig.h"
#include "../detail/AudioTiming.h"
#include "PcmRecovery.h"
#include "I2sClockTrim.h"
namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t sample_rate);
Status i2sBegin(uint32_t sample_rate, uint16_t pcm_buffer_ms);
Status i2sBegin(uint32_t sample_rate, uint16_t pcm_buffer_ms, const I2sPins& pins);
Status i2sBegin(uint32_t sample_rate, uint16_t pcm_buffer_ms, const I2sPins& pins, uint16_t startup_buffer_ms);
size_t i2sWrite(const int32_t* interleaved_stereo_pcm, size_t frames);
void i2sEnd();
OutputStats i2sStats();
OutputTimingSnapshot i2sTiming();
PcmRecoverySnapshot i2sRecovery();
struct OutputSourceSnapshot {
    uint32_t output_epoch=0,sample_rate=0,ring_capacity=0,half_frames=0;
    uint64_t prepared=0,missing=0;
    bool initialized=false,running=false,recovering=false;
};
OutputSourceSnapshot i2sSourceSnapshot();
ClockTrimResult i2sClockTrim(uint32_t expected_output_epoch,int32_t requested_q16,int32_t lower_q16,int32_t upper_q16);
I2sClockTrimSnapshot i2sClockTrimStats();
}}
