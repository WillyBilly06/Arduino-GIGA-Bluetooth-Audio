#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/coder.h"
int gbr_test_sbr_full(AACDecInfo*);
}
#include <cstdio>
#include <cstring>
using namespace giga_bredr::detail;
static bool clear(){auto s=memoryStats();return !s.current_bytes&&!s.current_blocks;}
static int sweep(){int cases=0;
 for(int channels=1;channels<=2;++channels)for(int fail=0;fail<11;++fail){
  if(!clear())return 10;HAACDecoder h=AACInitDecoderRawChannels(channels);if(!h)return 11;
  auto* info=(AACDecInfo*)h;auto* base=(PSInfoBase*)info->psInfoBase;auto before=memoryStats();
  setAllocationFailureCountdown(fail);int status=AACEnableSBR(h);setAllocationFailureCountdown(-1);auto after=memoryStats();
  if(status==0||info->psInfoSBR||base->sbrWorkBuf||before.current_bytes!=after.current_bytes||before.current_blocks!=after.current_blocks){std::printf("FAIL_AT ch=%d index=%d\n",channels,fail);return 12;}
  if(AACEnableSBR(h)!=0||!gbr_test_sbr_full(info)||!base->sbrWorkBuf)return 13;
  auto ready=memoryStats();if(AACEnableSBR(h)!=0)return 14;auto repeated=memoryStats();
  if(ready.current_bytes!=repeated.current_bytes||ready.current_blocks!=repeated.current_blocks)return 15;
  if(AACFlushCodec(h)!=0)return 16;AACFreeDecoder(h);if(!clear())return 17;++cases;
 }
 std::printf("FAIL_RETRY_SWEEP cases=%d PASS\n",cases);return cases==22?0:18;
}
static int generic(){
 for(int channels=1;channels<=2;++channels){if(!clear())return 20;HAACDecoder h=AACInitDecoderChannels(channels);if(!h)return 21;
  auto* a=(AACDecInfo*)h;auto* p=(PSInfoBase*)a->psInfoBase;
  if(p->allocatedPces!=MAX_NUM_PCE_ADIF||p->allocatedChannels!=channels)return 22;
  std::memset(p->pce,0x5a,sizeof(*p->pce)*p->allocatedPces);
  for(int i=0;i<channels;++i){p->icsInfo[i].maxSFB=31+i;p->scaleFactors[i][0]=71+i;p->sfbCodeBook[i][0]=11+i;p->gbCurrent[i]=42+i;}
  const unsigned char* bytes=(const unsigned char*)p->pce;for(size_t i=0;i<sizeof(*p->pce)*p->allocatedPces;++i)if(bytes[i]!=0x5a)return 23;
  if(AACFlushCodec(h)!=0)return 24;AACFreeDecoder(h);if(!clear())return 25;
 }
 std::puts("GENERIC_PROGRAM_PLANE_GUARDS channels=2 PASS");return 0;
}
static int cpe(){
 if(!clear())return 30;HAACDecoder h=AACInitDecoderRawChannels(1);if(!h)return 31;auto* a=(AACDecInfo*)h;auto* p=(PSInfoBase*)a->psInfoBase;
 p->icsInfo[0].maxSFB=57;unsigned char raw[16]={0x20};unsigned char* ptr=raw;int offset=0,bits=8;
 int status=DecodeNextElement(a,&ptr,&offset,&bits);bool ok=status==ERR_AAC_NCHANS_TOO_HIGH&&p->icsInfo[0].maxSFB==57;
 AACFreeDecoder(h);if(!clear())return 32;std::printf("MONO_CPE_GUARD status=%d untouched=%d result=%s\n",status,ok,ok?"PASS":"RED");return ok?0:1;
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;if(!std::strcmp(argv[1],"sweep"))return sweep();if(!std::strcmp(argv[1],"generic"))return generic();if(!std::strcmp(argv[1],"cpe"))return cpe();return 2;}
