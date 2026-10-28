#include "../src/owned/VoiceDacOutput.h"
#include "../src/detail/Memory.h"
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
namespace {unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Capture {int32_t pcm[2048]{};size_t frames=0,limit=240;};
size_t write(const int32_t* p,size_t n,void* user){auto& c=*static_cast<Capture*>(user);const size_t take=n<c.limit?n:c.limit;if(take*2>2048-c.frames*2)return 0;std::memcpy(c.pcm+c.frames*2,p,take*8);c.frames+=take;return take;}
}
extern "C" unsigned voiceDacTest(){checks=0;Capture capture;VoiceDacOutput dac;const size_t before=memoryStats().current_blocks;
 CHECK(dac.begin(VoiceCodec::Msbc,write,&capture)==Status::Ok);uint8_t pcm[240]{};pcm[0]=0xff;pcm[1]=0x7f;pcm[2]=0;pcm[3]=0x80;VoicePcmRequest request{1,0,0,{VoiceCodec::Msbc,16000,120,1}};
 capture.limit=1;CHECK(dac.write(request,pcm,120,15).frames==0&&capture.frames==1);capture.limit=0;CHECK(dac.write(request,pcm,120,15).frames==0&&capture.frames==1);capture.limit=240;CHECK(dac.write(request,pcm,120,15).frames==120&&capture.frames==120);CHECK(capture.pcm[0]==2147418112&&capture.pcm[1]==2147418112&&capture.pcm[2]==int32_t(-2147483647-1));dac.end();CHECK(memoryStats().current_blocks==before);
 capture=Capture();CHECK(dac.begin(VoiceCodec::Cvsd,write,&capture)==Status::Ok);uint8_t constant[60];for(unsigned i=0;i<30;++i){constant[2*i]=0x40;constant[2*i+1]=0x1f;}request={2,0,0,{VoiceCodec::Cvsd,8000,30,1}};
 for(unsigned block=0;block<6;++block){CHECK(dac.write(request,constant,30,15).frames==30);request.firstFrame+=30;}
 CHECK(capture.frames==360);for(unsigned i=160;i<360;++i)CHECK(capture.pcm[2*i]==8000*65536&&capture.pcm[2*i+1]==8000*65536);
 dac.end();CHECK(memoryStats().current_blocks==before);return 0;}
extern "C" unsigned voiceDacChecks(){return checks;}
