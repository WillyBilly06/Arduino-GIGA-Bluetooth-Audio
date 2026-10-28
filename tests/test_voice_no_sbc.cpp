#include "../src/owned/VoiceAudioPipeline.h"
#include "../src/detail/Memory.h"
#include <cassert>
#include <cstdio>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
int main(){VoiceMsbc msbc;assert(msbc.begin()==Status::Unsupported);VoiceAudioPipeline pipeline;VoicePipelineConfig c;c.format={VoiceCodec::Cvsd,8000,30,1};c.sink={nullptr,[](void*,const VoicePcmRequest&,const uint8_t*,size_t n){return VoicePcmRead{n,VoiceIoResult::Ready};}};int16_t pcm[32];uint8_t packet[64],out[60];VoicePipelineBuffers b;b.rx_pcm=pcm;b.rx_frames=32;b.packet=packet;b.packet_bytes=64;assert(pipeline.begin(1,1,c,b)==Status::Ok);size_t n=0;assert(pipeline.renderTx(1,out,sizeof out,n)==Status::Ok&&n==60);for(auto byte:out)assert(!byte);pipeline.end();assert(memoryStats().current_blocks==0);std::puts("VOICE_NO_SBC CVSD linked and rendered without SBC objects");}
