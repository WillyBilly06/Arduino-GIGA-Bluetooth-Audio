#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/coder.h"
int InitSBRChannels(AACDecInfo*,int);
void FreeSBR(AACDecInfo*);
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cstdint>
using namespace giga_bredr::detail;
struct Header{size_t n;unsigned char pre[24];};static int live=0;
static void* allocateGuard(size_t n,void*){auto* h=(Header*)malloc(sizeof(Header)+n+32);if(!h)abort();h->n=n;memset(h->pre,0xa7,24);auto* p=(unsigned char*)(h+1);memset(p,0xcc,n);memset(p+n,0x7a,32);++live;return p;}
static void freeGuard(void* p,void*){auto*h=(Header*)p-1;for(unsigned char c:h->pre)if(c!=0xa7)abort();for(unsigned i=0;i<32;++i)if(((unsigned char*)p)[h->n+i]!=0x7a)abort();--live;free(h);}
static bool clean(){auto s=memoryStats();return !s.current_bytes&&!s.current_blocks&&!live;}
struct Bits{std::vector<unsigned char> b;unsigned bits=0;void put(unsigned v,unsigned n){for(unsigned i=n;i>0;--i){if((bits&7)==0)b.push_back(0);b.back()|=((v>>(i-1))&1)<<(7-(bits&7));++bits;}}void align(){while(bits&7)put(0,1);} };
static int adif(){
 Bits x;for(char c:std::string("ADIF"))x.put((unsigned)c,8);x.put(0,1);x.put(0,1);x.put(0,1);x.put(1,1);x.put(128000,23);x.put(15,4);
 for(int i=0;i<16;++i){x.put(i,4);x.put(AAC_PROFILE_LC,2);x.put(4,4);x.put(1,4);x.put(0,4);x.put(0,4);x.put(0,2);x.put(0,3);x.put(0,4);x.put(0,1);x.put(0,1);x.put(0,1);x.put(0,1);x.put(i,4);x.align();x.put(0,8);}x.align();
 HAACDecoder h=AACInitDecoderChannels(2);if(!h)return 10;auto*a=(AACDecInfo*)h;auto*base=(PSInfoBase*)a->psInfoBase;auto data=x.b;data.resize(data.size()+16);auto*pos=data.data();int offset=0,bits=x.bits;int r=UnpackADIFHeader(a,&pos,&offset,&bits);
 bool ok=r==0&&base->fhADIF.numPCE==16&&base->allocatedPces==16&&a->nChans==16&&a->sampRate==44100;
 for(int i=0;i<16;++i)ok=ok&&base->pce[i].elemInstTag==i&&base->pce[i].profile==AAC_PROFILE_LC&&base->pce[i].sampRateIdx==4&&base->pce[i].numFCE==1&&base->pce[i].fce[0]==i;
 printf("ADIF16 parse=%d programs=%d channels=%d remaining_bits=%d records=%s\n",r,base->fhADIF.numPCE,a->nChans,bits,ok?"PASS":"FAIL");AACFreeDecoder(h);return ok&&clean()?0:1;
}
static uint64_t step(uint64_t h,uint32_t w){for(int k=0;k<4;++k){h^=(w>>(8*k))&255;h*=UINT64_C(1099511628211);}return h;}
static void sce(Bits& b,int tag){b.put(0,3);b.put(tag,4);b.put(100,8);b.put(0,1);b.put(0,2);b.put(0,1);b.put(0,6);b.put(0,1);b.put(0,1);b.put(0,1);b.put(0,1);}
static int twoSce(){
 HAACDecoder h=AACInitDecoderRawChannels(2);if(!h)return 20;AACFrameInfo cfg={};cfg.nChans=2;cfg.sampRateCore=44100;cfg.profile=AAC_PROFILE_LC;if(AACSetRawBlockParams(h,0,&cfg))return 21;
 auto*a=(AACDecInfo*)h;auto*base=(PSInfoBase*)a->psInfoBase;for(int i=0;i<AAC_MAX_NSAMPS;++i){base->overlap[0][i]=0x1000000+(i<<5);base->overlap[1][i]=-(0x2000000+(i<<4));}
 Bits x;sce(x,0);sce(x,1);x.put(7,3);x.align();auto data=x.b;data.resize(data.size()+16);uint64_t hash=UINT64_C(14695981039346656037);bool distinct=false,nonzero=false;
 for(int frame=0;frame<3;++frame){unsigned char* ptr=data.data();int remain=x.bits/8;short out[2050];for(auto &v:out)v=0x1234;int r=AACDecode(h,&ptr,&remain,out+1);AACFrameInfo info={};AACGetLastFrameInfo(h,&info);if(r||remain||info.nChans!=2||info.outputSamps!=2048||out[0]!=0x1234||out[2049]!=0x1234){printf("TWO_SCE_ERROR frame=%d r=%d remain=%d samples=%d\n",frame,r,remain,info.outputSamps);return 22;}for(int i=0;i<1024;++i){short l=out[1+2*i],rr=out[2+2*i];distinct|=l!=rr;nonzero|=l!=0||rr!=0;hash=step(hash,(uint16_t)l);hash=step(hash,(uint16_t)rr);}for(int c=0;c<2;++c)for(int i=0;i<AAC_MAX_NSAMPS;++i)hash=step(hash,(uint32_t)base->overlap[c][i]);}
 bool ok=distinct&&nonzero;printf("TWO_SCE frames=3 seeded_history=1 distinct=%d nonzero=%d pcm_history_hash=%016llx result=%s\n",distinct,nonzero,(unsigned long long)hash,ok?"PASS":"FAIL");AACFreeDecoder(h);return ok&&clean()?0:1;
}
static int guards(){
 HAACDecoder h=AACInitDecoderRawChannels(1);if(!h)return 30;auto*a=(AACDecInfo*)h;auto*base=(PSInfoBase*)a->psInfoBase;base->icsInfo[0].maxSFB=57;a->currBlockID=AAC_ID_SCE;
 unsigned char input[32]={0x20};unsigned char* ptr=input;int off=0,bits=8;short pcm[2050];for(auto&v:pcm)v=0x1234;
 int d=DecodeNoiselessData(a,&ptr,&off,&bits,1);int q=Dequantize(a,1),p=PNS(a,1),t=TNSFilter(a,1),i=IMDCT(a,1,1,pcm+1);a->currBlockID=AAC_ID_CPE;int s=StereoProcess(a);ptr=input;off=0;bits=8;int c=DecodeNextElement(a,&ptr,&off,&bits);
 bool ok=d!=0&&q!=0&&p!=0&&t!=0&&i!=0&&s!=0&&c==ERR_AAC_NCHANS_TOO_HIGH&&base->icsInfo[0].maxSFB==57&&pcm[0]==0x1234&&pcm[2049]==0x1234;
 printf("MONO_CHANNEL1 guards=%d,%d,%d,%d,%d,%d cpe=%d result=%s\n",d,q,p,t,i,s,c,ok?"PASS":"FAIL");AACFreeDecoder(h);return ok&&clean()?0:1;
}
static int lifecycle(){
 HAACDecoder h=AACInitDecoderRawChannels(1);if(!h)return 40;auto*a=(AACDecInfo*)h;auto*base=(PSInfoBase*)a->psInfoBase;int init=InitSBRChannels(a,1);int enable=AACEnableSBR(h);int missing=base->sbrWorkBuf==nullptr;printf("MIXED_INIT init=%d enable=%d missing_work=%d flush=SKIPPED\n",init,enable,missing);AACFreeDecoder(h);if(!clean())return 41;
 h=AACInitDecoderRawChannels(1);if(!h)return 42;a=(AACDecInfo*)h;base=(PSInfoBase*)a->psInfoBase;if(AACEnableSBR(h))return 43;void* previous=base->sbrWorkBuf;FreeSBR(a);int again=AACEnableSBR(h);int replaced=previous!=base->sbrWorkBuf;AACFreeDecoder(h);if(replaced)gbr_free(previous);bool ok=clean();printf("MIXED_FREESBR enable=%d work_replaced=%d cleanup=%s\n",again,replaced,ok?"PASS":"FAIL");return ok?0:1;
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;if(!setMemoryProvider(giga_bredr::MemoryKind::Fast,allocateGuard,freeGuard,nullptr))return 3;if(!strcmp(argv[1],"adif"))return adif();if(!strcmp(argv[1],"two-sce"))return twoSce();if(!strcmp(argv[1],"guards"))return guards();if(!strcmp(argv[1],"lifecycle"))return lifecycle();return 2;}
