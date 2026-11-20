#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
/* Exercise actual static numerical routines directly, without wire packets. */
#include "vendor/codecs/aac/stproc.c"
#include "vendor/codecs/aac/pns.c"
static uint32_t state=0x6f42d5a3u;
static uint32_t next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int i32(uint32_t n){return n<=INT32_MAX?(int)n:-1-(int)(~n);}
static void save(FILE *f,int n){uint32_t x=(uint32_t)n;for(unsigned i=0;i<4;++i)fputc((x>>(8*i))&255,f);}
static int bounded(int64_t x){if(x<INT32_MIN||x>INT32_MAX){fputs("inverse-root intermediate out of range\n",stderr);exit(6);}return (int)x;}
static int reference_invroot(int r){
 int xn=bounded((int64_t)MULSHIFT32(r,X0_COEF_2)*4+X0_OFF_2);
 for(unsigned i=0;i<NUM_ITER_INVSQRT;++i){int t=MULSHIFT32(xn,xn);t=bounded((int64_t)Q26_3-(int64_t)MULSHIFT32(r,t)*4);xn=bounded((int64_t)MULSHIFT32(xn,t)*32);}
 if(xn>>30)xn=(1<<30)-1;return xn;
}
int main(int argc,char**argv){
 if(argc!=2)return 2;FILE *f=fopen(argv[1],"wb");if(!f)return 3;
 for(unsigned i=0;i<20000;++i){int r=i==0?0x10000000:i==1?0x40000000:(int)(0x10000000u+next()%0x30000001u);int v=InvRootR(r);if(v!=reference_invroot(r))return 5;save(f,v);}
 const int edge[]={INT_MIN,INT_MIN+1,-1073741824,-1,0,1,1073741823,INT_MAX};
 for(unsigned mode=0;mode<3;++mode)for(unsigned k=0;k<20000;++k){
  int l=k<8?edge[k]:i32(next()),r=k<8?edge[7-k]:i32(next()),tab[]={0,1},gb[]={31,31};
  unsigned char mask[]={1},cb[]={mode==0?14:mode==1?15:0};short sf[]={(short)(k%257)};
  StereoProcessGroup(&l,&r,tab,mode==2?2:1,mask,0,1,cb,sf,gb);save(f,l);save(f,r);save(f,gb[0]);save(f,gb[1]);
 }
 for(unsigned k=0;k<10000;++k){int coef[96];int len=(int)(1+next()%96u);for(int i=0;i<len;++i){unsigned v=next()&65535u;coef[i]=v<32768?(int)v:(int)v-65536;}
  int gb=ScaleNoiseVector(coef,len,(int)(next()%257u));save(f,gb);for(int i=0;i<len;++i)save(f,coef[i]);
 }
 if(fclose(f))return 4;puts("PASS valid numerical domains: 20000 inverse roots, 60000 stereo cases, 10000 noise vectors");return 0;
}
