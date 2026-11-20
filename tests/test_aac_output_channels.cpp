// Bounded CPU fixture; no device, host control or external media.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/coder.h"
}
using namespace giga_bredr::detail;
struct Bits {
 unsigned char data[64]{};unsigned bits=0;
 void put(unsigned v,unsigned n){for(;n;--n){if(v&(1u<<(n-1)))data[bits/8]|=1u<<(7-bits%8);++bits;}}
 void sce(unsigned tag){put(0,3);put(tag,4);put(100,8);put(0,1);put(0,2);put(0,1);put(0,6);put(0,1);put(0,1);put(0,1);put(0,1);}
 void end(){put(7,3);while(bits%8)put(0,1);}
};
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;
 bool one=!std::strcmp(argv[1],"one"),two=!std::strcmp(argv[1],"two"),stereo=!std::strcmp(argv[1],"stereo"),direct=!std::strcmp(argv[1],"direct");
 if(!one&&!two&&!stereo&&!direct)return 2;if(memoryStats().current_blocks||memoryStats().current_bytes)return 3;
 HAACDecoder h=AACInitDecoder();if(!h)return 4;auto*a=(AACDecInfo*)h;auto*p=(PSInfoBase*)a->psInfoBase;
 AACFrameInfo info{};info.nChans=stereo?2:1;info.sampRateCore=44100;info.profile=AAC_PROFILE_LC;
 if(p->allocatedChannels!=2||AACSetRawBlockParams(h,0,&info)){AACFreeDecoder(h);return 5;}
 p->prevWinShape[1]=1;short out[2048+32];for(auto&v:out)v=0x5a5a;int status=0,left=0,initial=0;unsigned written=0;bool guard=false,far=false,history=false,uncommitted=false;
 if(direct){
  int first=IMDCT(a,0,1,out);int neg=IMDCT(a,0,-1,out);a->nChans=0;int zero=IMDCT(a,0,0,out);a->nChans=3;int over=IMDCT(a,0,0,out);
  bool unchanged=true;for(auto v:out)unchanged&=v==0x5a5a;history=p->prevWinShape[1]==0;bool ok=first==-1&&neg==-1&&zero==-1&&over==-1&&unchanged&&!history;
  AACFreeDecoder(h);bool clean=!memoryStats().current_blocks&&!memoryStats().current_bytes;
  std::printf("AAC_OUTPUT_DIRECT first=%d negative=%d zero=%d over=%d untouched=%u clean=%u result=%s\n",first,neg,zero,over,unsigned(unchanged),unsigned(clean),ok&&clean?"PASS":"FAIL");return ok&&clean?0:1;
 }
 Bits b;b.sce(0);if(two||stereo)b.sce(1);b.end();unsigned char*input=b.data;initial=int(b.bits/8);left=initial;status=AACDecode(h,&input,&left,out);
 const unsigned expected=stereo?2048:1024;for(unsigned i=0;i<expected;++i)written+=out[i]!=0x5a5a;guard=out[expected]!=0x5a5a;for(unsigned i=expected+1;i<expected+32;++i)far|=out[i]!=0x5a5a;history=p->prevWinShape[1]==0;uncommitted=input==b.data&&left==initial;
 bool ok=written==expected&&!guard&&!far;
 if(two)ok=ok&&status==ERR_AAC_NCHANS_TOO_HIGH&&!history&&uncommitted;
 else ok=ok&&status==ERR_AAC_NONE&&(stereo?history:!history)&&left==0;
 AACFreeDecoder(h);bool clean=!memoryStats().current_blocks&&!memoryStats().current_bytes;
 std::printf("AAC_OUTPUT_GUARD case=%s bits=%u channels=%d status=%d prefix=%u guard=%u far=%u second_history=%u input_uncommitted=%u clean=%u result=%s\n",argv[1],b.bits,info.nChans,status,written,unsigned(guard),unsigned(far),unsigned(history),unsigned(uncommitted),unsigned(clean),ok&&clean?"PASS":"FAIL");return ok&&clean?0:1;
}
