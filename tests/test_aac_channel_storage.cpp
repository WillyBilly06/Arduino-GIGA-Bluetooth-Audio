// Portable allocation/ownership and exact-PCM regression. Run the same binary
// source against the frozen baseline and candidate; compare PCM rows exactly.
// Media are supplied separately and are never embedded in a release.
#include "codecs/Codecs.h"
#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/sbr.h"
}
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
using namespace giga_bredr;
using namespace giga_bredr::detail;
static unsigned liveRaw, rawCalls;
static size_t liveRawBytes;
struct RawHeader { size_t n; unsigned char guard[24]; };
static void* guardedAlloc(size_t n,void*) {
    RawHeader* h=(RawHeader*)malloc(sizeof(RawHeader)+n+32);assert(h);
    h->n=n;memset(h->guard,0xa7,sizeof(h->guard));
    unsigned char* p=(unsigned char*)(h+1);memset(p,0xcd,n);memset(p+n,0x7a,32);
    ++liveRaw;++rawCalls;liveRawBytes+=n;return p;
}
static void guardedFree(void* p,void*) {
    RawHeader* h=(RawHeader*)p-1;
    for(unsigned char v:h->guard)assert(v==0xa7);
    for(unsigned i=0;i<32;++i)assert(((unsigned char*)p)[h->n+i]==0x7a);
    --liveRaw;liveRawBytes-=h->n;free(h);
}
static void clean() { assert(memoryStats().current_bytes==0&&memoryStats().current_blocks==0&&liveRaw==0&&liveRawBytes==0); }
static CodecConfig config(bool he,unsigned channels,unsigned rate,AacTransport transport) {
    CodecConfig c;c.codec=he?Codec::AacHe:Codec::Aac;c.channels=channels;c.sample_rate=rate;c.aac_transport=transport;return c;
}
static void geometryAndFailure() {
    const unsigned rates[]={8000,16000,24000,32000,44100,48000};
    unsigned configs=0;
    for(unsigned he=0;he<2;++he)for(unsigned ch=1;ch<=2;++ch)for(unsigned rate:rates) {
        if(he&&rate==8000)continue;
        for(unsigned t=0;t<3;++t) {
            CodecConfig c=config(he,ch,rate,(AacTransport)t);const DecoderOps* ops=codecOperations(c.codec);assert(ops);
            void* p=nullptr;unsigned calls=rawCalls;assert(ops->open(&p,c)==Status::Ok&&p);
            MemoryStats m=memoryStats();size_t raw=liveRawBytes;unsigned blocks=rawCalls-calls;
            printf("ALLOC he=%u ch=%u rate=%u transport=%u payload=%zu blocks=%zu raw=%zu calls=%u\n",he,ch,rate,t,m.current_bytes,m.current_blocks,raw,blocks);
            ops->close(p);clean();
            // Every partial allocation is failed in turn, plus successful open.
            for(unsigned fail=0;fail<=blocks;++fail) {
                setAllocationFailureCountdown(fail);p=nullptr;Status s=ops->open(&p,c);setAllocationFailureCountdown(-1);
                if(fail<blocks)assert(s==Status::OutOfMemory&&!p);
                else { assert(s==Status::Ok&&p);ops->close(p); }
                clean();
            }
            ++configs;
        }
    }
    assert(configs==66);printf("ALLOCATION_MATRIX configs=%u\n",configs);
}
static void tailAndFlush() {
    for(int channels=1;channels<=2;++channels) {
        HAACDecoder h=AACInitDecoderRawChannels(channels);assert(h&&AACEnableSBR(h)==0);
        AACDecInfo* a=(AACDecInfo*)h;PSInfoSBR* s=(PSInfoSBR*)a->psInfoSBR;
        for(int ch=0;ch<channels;++ch)for(int e=0;e<MAX_NUM_ENV;++e)for(int b=0;b<MAX_QMF_BANDS;++b)s->envDataDequant[ch][e][b]=0x12345678;
        assert(AACFlushCodec(h)==0);
        assert(s->allocatedChannels==channels);
        for(int ch=0;ch<channels;++ch)for(int e=0;e<MAX_NUM_ENV;++e)for(int b=0;b<MAX_QMF_BANDS;++b)assert(s->envDataDequant[ch][e][b]==0);
        // The existing channel guards must reject a stereo element in mono
        // before touching the missing tail, header, or output.
        if(channels==1) {
            a->currBlockID=AAC_ID_FIL;a->prevBlockID=AAC_ID_CPE;a->fillExtType=EXT_SBR_DATA;
            short output[2]={0x1234,0x5678};
            assert(DecodeSBRBitstream(a,0)==ERR_AAC_SBR_NCHANS_TOO_HIGH);
            assert(DecodeSBRData(a,0,output)==ERR_AAC_SBR_NCHANS_TOO_HIGH);
            assert(output[0]==0x1234&&output[1]==0x5678);
        }
        AACFreeDecoder(h);clean();
    }
    puts("TAIL_FLUSH_CHANNEL_GUARDS PASS");
}
static std::vector<unsigned char> readFile(const char* path) {
    FILE* f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    std::vector<unsigned char> b((size_t)n);assert(fread(b.data(),1,b.size(),f)==b.size());fclose(f);return b;
}
static uint64_t hashWord(uint64_t h,uint32_t v) { for(unsigned k=0;k<4;++k){h^=(v>>(8*k))&255;h*=UINT64_C(1099511628211);}return h; }
static void stream(const char* label,const char* path,bool he,unsigned channels,unsigned rate,bool loas,bool raw) {
    auto data=readFile(path);CodecConfig c=config(he,channels,rate,loas?AacTransport::Latm:raw?AacTransport::Raw:AacTransport::Adts);
    const DecoderOps* ops=codecOperations(c.codec);void* p=nullptr;assert(ops->open(&p,c)==Status::Ok);
    size_t pos=0,frames=0,packets=0;uint64_t hash=UINT64_C(14695981039346656037);
    if(data.size()>10&&!memcmp(data.data(),"ID3",3))pos=10+((data[6]&127)<<21)+((data[7]&127)<<14)+((data[8]&127)<<7)+(data[9]&127);
    while(pos<data.size()) {
        assert(data.size()-pos>=7);size_t n,skip=0;
        if(loas){assert(data[pos]==0x56&&(data[pos+1]&0xe0)==0xe0);n=((data[pos+1]&31)<<8)|data[pos+2];skip=3;}
        else {assert(data[pos]==0xff&&(data[pos+1]&0xf6)==0xf0);n=((data[pos+3]&3)<<11)|(data[pos+4]<<3)|(data[pos+5]>>5);if(raw)skip=(data[pos+1]&1)?7:9;}
        size_t advance=loas?n+3:n;assert(advance>skip&&advance<=data.size()-pos);size_t bytes=advance-skip;
        int32_t out[4098];for(auto&v:out)v=0x5a123456;
        unsigned before=rawCalls;uint32_t failures=memoryStats().failures;
        setAllocationFailureCountdown(0);DecodeResult r=ops->decode(p,data.data()+pos+skip,bytes,out+1,he?2048:1024);setAllocationFailureCountdown(-1);
        assert(r.status==Status::Ok&&r.consumed==bytes&&r.frames==(he?2048u:1024u));
        assert(rawCalls==before&&memoryStats().failures==failures);
        assert(out[0]==0x5a123456&&out[r.frames*channels+1]==0x5a123456);
        hash=hashWord(hash,(uint32_t)r.status);hash=hashWord(hash,r.consumed);hash=hashWord(hash,r.frames);
        for(size_t i=0;i<r.frames*channels;++i)hash=hashWord(hash,(uint32_t)out[i+1]);
        frames+=r.frames;++packets;pos+=advance;
    }
    ops->close(p);clean();printf("PCM label=%s packets=%zu frames=%zu hash=%016llx\n",label,packets,frames,(unsigned long long)hash);
}
int main(int argc,char**argv) {
    setvbuf(stdout,nullptr,_IONBF,0);assert(argc==6);
    assert(setMemoryProvider(MemoryKind::Fast,guardedAlloc,guardedFree,nullptr));
    geometryAndFailure();tailAndFlush();
    stream("lc-adts",argv[1],false,2,48000,false,false);
    stream("lc-raw",argv[1],false,2,48000,false,true);
    stream("lc-latm",argv[2],false,2,48000,true,false);
    stream("he-mono-adts",argv[3],true,1,44100,false,false);
    stream("he-mono-raw",argv[3],true,1,44100,false,true);
    stream("he-stereo-adts",argv[4],true,2,44100,false,false);
    stream("he-stereo-raw",argv[4],true,2,44100,false,true);
    stream("he-stereo-latm",argv[5],true,2,44100,true,false);
    puts("AAC_CHANNEL_STORAGE PASS");
}
