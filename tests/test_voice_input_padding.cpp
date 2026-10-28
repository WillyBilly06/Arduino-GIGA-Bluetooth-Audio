#include "../src/owned/HfpAudio.h"
#include <cstdint>
using namespace giga_bredr::owned;
namespace {
unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Source {unsigned available_calls=0,read_calls=0;uint64_t expected=0;size_t available_frames=0;bool correct=true;};
size_t available(void* p,const VoicePcmRequest& r){auto& s=*static_cast<Source*>(p);++s.available_calls;s.correct=s.correct&&r.firstFrame==s.expected;return s.available_frames;}
VoicePcmRead read(void* p,const VoicePcmRequest& r,uint8_t* out,size_t n){auto& s=*static_cast<Source*>(p);++s.read_calls;s.correct=s.correct&&r.firstFrame==s.expected;for(size_t i=0;i<n*2;++i)out[i]=0x55;return {n,VoiceIoResult::Ready};}
}
extern "C" unsigned voiceInputPaddingTest(){
 checks=0;HfpPcmInput input;Source source;const VoicePcmFormat f{VoiceCodec::Cvsd,8000,30,1};uint8_t bytes[62];for(auto& b:bytes)b=0xa5;
 CHECK(input.configure(7,1,f,MicrophoneMode::Provider,{&source,available,read}));
 auto first=input.readPadded(7,1,bytes+1,30);CHECK(first.result==VoiceIoResult::Ready&&first.frames==30);CHECK(source.available_calls==1&&!source.read_calls&&source.correct);CHECK(input.counters().micStarvedFrames==30);for(unsigned i=1;i<=60;++i)CHECK(bytes[i]==0);CHECK(bytes[0]==0xa5&&bytes[61]==0xa5);
 source.expected=30;source.available_frames=15;auto second=input.readPadded(7,1,bytes+1,30);CHECK(second.result==VoiceIoResult::Ready&&second.frames==30&&source.correct);CHECK(input.counters().micStarvedFrames==45);for(unsigned i=1;i<=30;++i)CHECK(bytes[i]==0x55);for(unsigned i=31;i<=60;++i)CHECK(bytes[i]==0);
 source.expected=60;source.available_frames=30;CHECK(input.readPadded(7,1,bytes+1,30).frames==30&&source.correct);CHECK(input.counters().micStarvedFrames==45);
 input.detach();const auto calls=source.available_calls;CHECK(input.readPadded(7,1,bytes+1,30).result==VoiceIoResult::Stale&&source.available_calls==calls);
 CHECK(input.configure(7,2,f,MicrophoneMode::None));CHECK(input.readPadded(7,2,bytes+1,30).frames==30&&source.available_calls==calls);CHECK(input.counters().noMicSilenceFrames==30&&input.counters().micStarvedFrames==45);
 return 0;
}
extern "C" unsigned voiceInputPaddingChecks(){return checks;}
