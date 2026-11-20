#include "owned/VoiceMsbc.h"
#include "voice_vectors/msbc_reference.h"
#include <cstdio>
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;
static_assert(sizeof(MsbcInputPcm)/sizeof(MsbcInputPcm[0])>=16*120,"reference PCM too short");
int main(int argc,char** argv){
 if(argc!=4)return 2;VoiceMsbc c;if(c.begin()!=Status::Ok)return 3;
 if(!std::strcmp(argv[1],"boundary-encode")){
  FILE* o=std::fopen(argv[2],"wb");if(!o)return 4;uint8_t frame[57];int16_t pcm[120];uint32_t rng=0x79d381adu;
  for(unsigned i=0;i<512;++i){for(unsigned k=0;k<120;++k){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
   const int32_t r=int32_t(rng&65535u)-32768;pcm[k]=i==0?32767:i==1?-32768:i==2?(k%2?32767:-32768):i<123?(k==i-3?32767:0):int16_t(r);}
   if(c.encode(pcm,120,frame,57)!=Status::Ok)return 5;if(std::fwrite(frame,1,57,o)!=57)return 6;}
  std::fclose(o);c.end();std::puts("encoded 512 boundary, impulse and seeded-random PCM blocks");return 0;
 }
 if(!std::strcmp(argv[1],"encode")){
  FILE* o=std::fopen(argv[2],"wb");if(!o)return 4;uint8_t frame[57];
  for(unsigned i=0;i<16;++i){if(c.encode(MsbcInputPcm+i*120,120,frame,57)!=Status::Ok)return 5;if(std::fwrite(frame,1,57,o)!=57)return 6;}
  std::fclose(o);c.end();std::puts("encoded 16 reference PCM blocks");return 0;
 }
 if(!std::strcmp(argv[1],"decode")){
  FILE* in=std::fopen(argv[2],"rb");FILE* o=std::fopen(argv[3],"wb");if(!in||!o)return 7;uint8_t frame[57];int16_t pcm[120];
  for(unsigned i=0;i<16;++i){if(std::fread(frame,1,57,in)!=57)return 8;auto r=c.decode(frame,57,pcm,120);if(r.status!=Status::Ok||r.consumed!=57||r.frames!=120)return 9;
   for(auto s:pcm){uint16_t u=static_cast<uint16_t>(s);std::fputc(u&255,o);std::fputc(u>>8,o);}}
  if(std::fgetc(in)!=EOF)return 10;std::fclose(in);std::fclose(o);c.end();std::puts("decoded 16 encoded blocks to 1920 PCM samples");return 0;
 }
 return 11;
}
