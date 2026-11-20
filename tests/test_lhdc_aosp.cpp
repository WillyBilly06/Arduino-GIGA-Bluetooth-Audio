#include "detail/Decoder.h"
#include "detail/Memory.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "vendor/codecs/lhdc/lhdc_tables.h"
int lhdc_test_window_descriptor(int);
}
using namespace giga_bredr;
using namespace giga_bredr::detail;
int main(int argc, char** argv) {
    if (argc==3 && strcmp(argv[1],"--windows")==0) {
        FILE *out=fopen(argv[2],"wb"); if(!out)return 10;
        for(int n : {480,960,1920}) {
            const int shape=lhdc_test_window_descriptor(n);
            if(shape) { fprintf(stderr,"window n=%d descriptor/overlap failure=%d\n",n,shape); return 13; }
            const float *w=lhdc_get_window_const(n); if(!w)return 11;
            if(fwrite(w,sizeof(float),n,out)!=size_t(n))return 12;
        }
        fclose(out); return 0;
    }
    if (argc != 5) return 1;
    FILE* in = fopen(argv[1], "rb");
    if (!in) return 2;
    std::vector<uint8_t> bytes;
    for (int c; (c = fgetc(in)) != EOF;) bytes.push_back(uint8_t(c));
    fclose(in);
    CodecConfig c; c.codec=Codec::LhdcV5; c.sample_rate=uint32_t(atoi(argv[3]));
    c.channels=2; c.bits_per_sample=uint8_t(atoi(argv[4])); c.frame_duration_us=5000;
    Decoder decoder;
    if (memoryStats().current_bytes || decoder.open(c)!=Status::Ok) return 3;
    FILE* out=fopen(argv[2],"wb"); if(!out) return 4;
    size_t pos=0, frames=0, calls=0;
    while(pos<bytes.size() && calls<10000) {
        int32_t pcm[1922]={}; pcm[0]=pcm[1921]=0x17589324;
        DecodeResult r=decoder.decode(bytes.data()+pos,bytes.size()-pos,pcm+1,960);
        if(r.status!=Status::Ok || !r.consumed || r.consumed>bytes.size()-pos ||
           !r.frames || r.frames>960 || r.concealment_flags ||
           pcm[0]!=0x17589324 || pcm[1921]!=0x17589324) {
            fprintf(stderr,"FAIL call=%zu pos=%zu status=%d consumed=%zu frames=%zu conceal=%u\n",
                calls,pos,int(r.status),r.consumed,r.frames,unsigned(r.concealment_flags)); return 5;
        }
        if(fwrite(pcm+1,sizeof(int32_t),r.frames*2,out)!=r.frames*2) return 6;
        printf("call=%zu pos=%zu bytes=%zu frames=%zu\n",calls,pos,r.consumed,r.frames);
        pos+=r.consumed; frames+=r.frames; ++calls;
    }
    fclose(out); decoder.close(); auto m=memoryStats();
    if(pos!=bytes.size() || m.current_bytes || m.current_blocks || m.failures) return 7;
    printf("PASS bytes=%zu frames=%zu calls=%zu residual=%zu\n",pos,frames,calls,m.current_bytes);
}
