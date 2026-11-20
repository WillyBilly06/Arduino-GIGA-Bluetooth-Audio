#include "vendor/codecs/aac/aacdec.h"
#include "fixtures/owned_adts_retained_prefix.h"
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){if(argc!=2)return 2;FILE*out=std::fopen(argv[1],"wb");if(!out)return 3;HAACDecoder h=AACInitDecoderChannels(2);if(!h)return 4;
 const uint8_t* frames[]={FRAME_A,FRAME_B};const size_t sizes[]={sizeof FRAME_A,sizeof FRAME_B};
 for(unsigned i=0;i<2;++i){unsigned char input[1024]={};if(sizes[i]>sizeof input)return 5;std::memcpy(input,frames[i],sizes[i]);unsigned char* p=input;int remain=int(sizes[i]);short pcm[2050];for(short &v:pcm)v=12345;
  int rc=AACDecode(h,&p,&remain,pcm+1);AACFrameInfo info{};AACGetLastFrameInfo(h,&info);if(rc||remain||info.nChans!=2||info.outputSamps!=2048||pcm[0]!=12345||pcm[2049]!=12345){std::fprintf(stderr,"decode %u rc=%d remain=%d channels=%d samples=%d\n",i,rc,remain,info.nChans,info.outputSamps);return 6;}
  for(int j=0;j<info.outputSamps;++j){const unsigned short v=(unsigned short)pcm[j+1];std::fputc(v&255,out);std::fputc(v>>8,out);}
  std::printf("frame=%u bytes=%u channels=%d rate=%d samples=%d consumed=%u\n",i,unsigned(sizes[i]),info.nChans,info.sampRateOut,info.outputSamps,unsigned(p-input));
 }
 AACFreeDecoder(h);if(std::fclose(out))return 7;return 0;}
