#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void dct2_8(int16_t *out,const int32_t *in);
extern void SynthWindow80_generated(int16_t *pcm,const int16_t *buffer,unsigned strideShift);
static uint32_t random_word=0x9e3779b9u;
static uint32_t next(void){random_word^=random_word<<13;random_word^=random_word>>17;random_word^=random_word<<5;return random_word;}
static void save(FILE *f,const int16_t *v,unsigned n){for(unsigned i=0;i<n;++i){uint16_t u=(uint16_t)v[i];fputc(u&255,f);fputc(u>>8,f);}}
int main(int argc,char **argv){
 if(argc!=2)return 2;FILE *f=fopen(argv[1],"wb");if(!f)return 3;
 int32_t in[8]={0};int16_t out[8],guarded[18],buffer[80];unsigned dct=0,windows=0;
 for(unsigned i=0;i<8;++i)for(int sign=-1;sign<=1;sign+=2){
  memset(in,0,sizeof in);in[i]=sign*1244918056;dct2_8(out,in);save(f,out,8);++dct;
 }
 for(unsigned mask=0;mask<256;++mask){for(unsigned i=0;i<8;++i)in[i]=(mask&(1u<<i))?1244918056:-1244918056;
  dct2_8(out,in);save(f,out,8);++dct;}
 for(unsigned k=0;k<10000;++k){for(unsigned i=0;i<8;++i)in[i]=(int32_t)(next()&0x03ffffffu)-0x02000000;
  dct2_8(out,in);save(f,out,8);++dct;}
 for(unsigned k=0;k<10000;++k){for(unsigned i=0;i<80;++i){
   if(k==0)buffer[i]=32767;else if(k==1)buffer[i]=-32768;else if(k==2)buffer[i]=(i&1)?32767:-32768;
   else {uint32_t v=next()&0xffffu;buffer[i]=(int16_t)(v<32768?v:(int32_t)v-65536);}}
  for(unsigned stride=0;stride<=1;++stride){for(unsigned i=0;i<18;++i)guarded[i]=12345;
   SynthWindow80_generated(guarded+1,buffer,stride);if(guarded[0]!=12345||guarded[17]!=12345)return 4;
   save(f,guarded+1,16);++windows;}}
 if(fclose(f))return 5;printf("DCT cases=%u; window cases=%u; guard words intact\n",dct,windows);return 0;
}
