#pragma once
#include "HfpAudio.h"
#include "../Types.h"
namespace giga_bredr { namespace detail {
// PCM5102A with MCLK=-1 cannot use its BCK PLL at native8k. Both voice
// formats therefore render to16k stereo32-bit slots. CVSD alone reserves the
// 2x interpolation history. Its group delay is31/16000s (~1.94ms).
class VoiceDacOutput {
public:
 ~VoiceDacOutput(){end();}
 Status begin(owned::VoiceCodec,size_t(*write)(const int32_t*,size_t,void*),void*);
 void end();owned::VoicePcmRead write(const owned::VoicePcmRequest&,const uint8_t*,size_t,uint8_t speaker_gain);
private:
 int16_t even(int16_t);int32_t slot(int16_t,uint8_t)const;
 void* storage_=nullptr;int32_t* pcm_=nullptr;int16_t* history_=nullptr;
 size_t(*write_)(const int32_t*,size_t,void*)=nullptr;void* user_=nullptr;
 uint32_t generation_=0;uint64_t first_=0;size_t input_frames_=0,output_frames_=0,cursor_=0;owned::VoiceCodec codec_=owned::VoiceCodec::Cvsd;
};
}}
