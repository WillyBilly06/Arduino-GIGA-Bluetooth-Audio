#include "../src/owned/VoiceMsbc.h"
#include "../src/detail/Memory.h"
#include "voice_vectors/msbc_reference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace giga_bredr;using namespace giga_bredr::detail;
namespace {
unsigned checks;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"MSBC CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
void reference_decode(){VoiceMsbc c;REQUIRE(c.begin()==Status::Ok);int16_t pcm[122];unsigned maximum=0;uint64_t squared=0;
 for(unsigned f=0;f<32;++f){pcm[0]=1234;pcm[121]=-1234;auto r=c.decode(MsbcReferenceFrames+57*f,57,pcm+1,120);REQUIRE(r.status==Status::Ok&&r.consumed==57&&r.frames==120);REQUIRE(pcm[0]==1234&&pcm[121]==-1234);
  for(unsigned n=0;n<120;++n){int d=int(pcm[n+1])-int(MsbcDecodedReference[f*120+n]);unsigned a=d<0?unsigned(-d):unsigned(d);if(a>maximum)maximum=a;squared+=uint64_t(int64_t(d)*d);}
 }
 std::printf("MSBC_REFERENCE samples=3840 max_abs=%u squared=%llu\n",maximum,(unsigned long long)squared);REQUIRE(maximum<=32);c.end();}
void encode_interleaving(){VoiceMsbc a,b,solo;REQUIRE(a.begin()==Status::Ok&&b.begin()==Status::Ok&&solo.begin()==Status::Ok);uint8_t expected[57],mixed[57],other[57];int16_t silence[120]{};
 for(unsigned f=0;f<16;++f){REQUIRE(solo.encode(MsbcInputPcm+120*f,120,expected,sizeof expected)==Status::Ok);REQUIRE(b.encode(silence,120,other,sizeof other)==Status::Ok);REQUIRE(a.encode(MsbcInputPcm+120*f,120,mixed,sizeof mixed)==Status::Ok);REQUIRE(!std::memcmp(mixed,expected,57)&&mixed[0]==0xad);}
}
void lifecycle_bounds(){const auto before=memoryStats();VoiceMsbc c;uint8_t frame[59];int16_t pcm[122]{};std::memset(frame,0xa5,sizeof frame);
 setAllocationFailureCountdown(0);REQUIRE(c.begin()==Status::OutOfMemory);REQUIRE(memoryStats().current_bytes==before.current_bytes);setAllocationFailureCountdown(-1);
 REQUIRE(c.begin()==Status::Ok);const auto opened=memoryStats();REQUIRE(opened.current_blocks==before.current_blocks+1);REQUIRE(c.begin()==Status::Busy);
 REQUIRE(c.encode(pcm,119,frame+1,57)==Status::NeedMoreData);REQUIRE(c.encode(pcm,120,frame+1,56)==Status::OutputFull);REQUIRE(frame[0]==0xa5&&frame[58]==0xa5);
 REQUIRE(c.decode(MsbcReferenceFrames,56,pcm+1,120).status==Status::NeedMoreData);REQUIRE(c.decode(MsbcReferenceFrames,57,pcm+1,119).status==Status::OutputFull);
 for(unsigned n=0;n<200;++n){REQUIRE(c.encode(pcm,120,frame+1,57)==Status::Ok);REQUIRE(frame[0]==0xa5&&frame[58]==0xa5);REQUIRE(memoryStats().current_bytes==opened.current_bytes&&memoryStats().current_blocks==opened.current_blocks);}
 c.end();c.end();REQUIRE(memoryStats().current_bytes==before.current_bytes&&memoryStats().current_blocks==before.current_blocks);
 REQUIRE(c.begin()==Status::Ok);REQUIRE(c.decode(MsbcReferenceFrames,57,pcm+1,120).status==Status::Ok);c.end();REQUIRE(memoryStats().current_bytes==before.current_bytes);
}
void h2(){const uint8_t expected[]={0x08,0x38,0xc8,0xf8};uint8_t packet[62];
 for(uint8_t seq=0;seq<4;++seq){std::memset(packet,0xa5,sizeof packet);REQUIRE(VoiceMsbc::packH2(seq,MsbcReferenceFrames,57,packet+1,60)==Status::Ok);REQUIRE(packet[0]==0xa5&&packet[61]==0xa5&&packet[1]==1&&packet[2]==expected[seq]&&packet[60]==0);uint8_t observed=255;const uint8_t* frame=nullptr;REQUIRE(VoiceMsbc::unpackH2(packet+1,60,observed,frame)==Status::Ok&&observed==seq&&frame==packet+3);REQUIRE(!std::memcmp(frame,MsbcReferenceFrames,57));}
 uint8_t seq=0;const uint8_t* frame=nullptr;packet[2]=0x18;REQUIRE(VoiceMsbc::unpackH2(packet+1,60,seq,frame)==Status::CorruptFrame);REQUIRE(VoiceMsbc::packH2(4,MsbcReferenceFrames,57,packet+1,60)==Status::InvalidArgument);
}
}
int main(int argc,char** argv){const std::string s=argc>1?argv[1]:"all";if(s=="all"||s=="decode")reference_decode();if(s=="all"||s=="interleave")encode_interleaving();if(s=="all"||s=="lifecycle")lifecycle_bounds();if(s=="all"||s=="h2")h2();std::printf("VOICE_MSBC checks=%u\n",checks);return 0;}
