#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/coder.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
using namespace giga_bredr::detail;
struct Header{size_t n;unsigned char pre[24];};static int live;
static void* ga(size_t n,void*){auto*h=(Header*)malloc(sizeof(Header)+n+32);if(!h)abort();h->n=n;memset(h->pre,0xa7,24);auto*p=(unsigned char*)(h+1);memset(p+n,0x7a,32);++live;return p;}
static void gf(void* p,void*){auto*h=(Header*)p-1;for(auto c:h->pre)if(c!=0xa7)abort();for(unsigned i=0;i<32;++i)if(((unsigned char*)p)[h->n+i]!=0x7a)abort();--live;free(h);}
struct Span{const char*name;unsigned char*p;size_t n,align;unsigned char value;};
static int run(){
 for(int c=1;c<=2;++c){if(live)return 10;HAACDecoder h=AACInitDecoderChannels(c);if(!h)return 11;auto* a=(AACDecInfo*)h;auto* p=(PSInfoBase*)a->psInfoBase;if(p->allocatedChannels!=c||p->allocatedPces!=16)return 12;
 std::vector<Span> s;
#define SP(name, ptr, type, count) s.push_back({name,(unsigned char*)(ptr),sizeof(type)*(size_t)(count),alignof(type),(unsigned char)(0x31+s.size())})
 SP("pce",p->pce,ProgConfigElement,16);SP("ics",p->icsInfo,ICSInfo,c);SP("scale",p->scaleFactors,short[MAX_SF_BANDS],c);SP("codebook",p->sfbCodeBook,unsigned char[MAX_SF_BANDS],c);SP("pns",p->pnsUsed,int,c);SP("intensity",p->intensityUsed,int,c);SP("pulse",p->pulseInfo,PulseInfo,c);SP("tns",p->tnsInfo,TNSInfo,c);SP("gain",p->gainControlInfo,GainControlInfo,c);SP("guardbits",p->gbCurrent,int,c);
#undef SP
 for(size_t i=0;i<s.size();++i){auto&x=s[i];uintptr_t a=(uintptr_t)x.p;if(!x.p||a%x.align||a>UINTPTR_MAX-x.n){printf("SPAN_INVALID channels=%d plane=%s\n",c,x.name);return 13;}for(size_t j=0;j<i;++j){auto&y=s[j];uintptr_t b=(uintptr_t)y.p;if(a<b+y.n&&b<a+x.n){printf("SPAN_OVERLAP channels=%d planes=%s,%s\n",c,x.name,y.name);return 14;}}}
 for(auto &x:s)memset(x.p,x.value,x.n);
 for(auto &x:s)for(size_t k=0;k<x.n;++k)if(x.p[k]!=x.value){printf("SENTINEL_FAIL channels=%d plane=%s offset=%zu\n",c,x.name,k);return 15;}
 for(int ch=0;ch<c;++ch){if((unsigned char)p->sfbCodeBook[ch][MAX_SF_BANDS-1]!=s[3].value)return 16;auto* end=(unsigned char*)&p->scaleFactors[ch][MAX_SF_BANDS-1];if(end[0]!=s[2].value||end[1]!=s[2].value)return 17;}
 printf("PLANE_SPANS channels=%d spans=%zu scale_bands=%d result=PASS\n",c,s.size(),MAX_SF_BANDS);AACFreeDecoder(h);if(live||memoryStats().current_blocks||memoryStats().current_bytes)return 18;
 }return 0;
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);if(!setMemoryProvider(giga_bredr::MemoryKind::Fast,ga,gf,nullptr))return 1;return run();}
