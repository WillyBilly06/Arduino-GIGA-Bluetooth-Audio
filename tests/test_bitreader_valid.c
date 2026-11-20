#include "vendor/codecs/aac/bitstream.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t state=0x873012afu;static uint32_t next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint32_t read_ref(const uint8_t* b,unsigned pos,unsigned n){uint32_t value=0;for(unsigned i=0;i<n;++i)value=(value<<1)|((b[(pos+i)/8]>>(7-((pos+i)%8)))&1u);return value;}
int main(void){unsigned long operations=0;for(unsigned len=1;len<=64;++len){for(unsigned pass=0;pass<256;++pass){unsigned char b[64];for(unsigned i=0;i<len;++i)b[i]=pass==0?0:pass==1?0xff:pass==2?(uint8_t)(i*37u):(uint8_t)next();BitStreamInfo t;SetBitstreamPointer(&t,(int)len,b);unsigned pos=0;
 assert(GetBits(&t,0)==0);assert(GetBitsNoAdvance(&t,0)==0);
 while(pos<len*8){unsigned n=1+next()%31u;if(n>len*8-pos)n=len*8-pos;uint32_t expected=read_ref(b,pos,n);
  if((pass+pos)&1u){BitStreamInfo snapshot=t;uint32_t got=GetBitsNoAdvance(&t,(int)n);assert(got==expected);assert(t.bytePtr==snapshot.bytePtr&&t.iCache==snapshot.iCache&&t.cachedBits==snapshot.cachedBits&&t.nBytes==snapshot.nBytes);AdvanceBitstream(&t,(int)n);}else assert(GetBits(&t,(int)n)==expected);
  pos+=n;assert(CalcBitsUsed(&t,b,0)==(int)pos);++operations;
 }
 }}
 printf("PASS valid bit reads lengths=1..64 patterns=256 operations=%lu checked peek, advance, consumption; no overread\n",operations);return 0;}
