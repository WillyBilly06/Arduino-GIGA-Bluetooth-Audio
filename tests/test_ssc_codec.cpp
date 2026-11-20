// Native Samsung SSC checks through the real vendored decoder, Codecs.cpp, Media.cpp and the
// receive loop's per-frame contract. Host-only: no phone, no M7 timing and no acoustic claim.
// tools/test_ssc.py builds the RTP packet files and compares PCM with the standalone decoder.
#include "codecs/Codecs.h"
#include "detail/Media.h"
#include "detail/Memory.h"
#include "vendor/codecs/ssc/ssc_decoder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace giga_bredr;
using namespace giga_bredr::detail;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d %s\n",__LINE__,#x);std::abort();}}while(0)
using Bytes=std::vector<uint8_t>;
static size_t provider_calls=0,provider_frees=0;
static void* countingAllocate(size_t n,void*){++provider_calls;return std::malloc(n);}
static void countingRelease(void* p,void*){++provider_frees;std::free(p);}
static std::vector<Bytes> records(const char* path){
    FILE* f=std::fopen(path,"rb");CHECK(f);std::vector<Bytes> out;uint8_t h[2];
    while(std::fread(h,1,2,f)==2){Bytes b(size_t(h[0]|(h[1]<<8)));CHECK(!b.empty()&&std::fread(b.data(),1,b.size(),f)==b.size());out.push_back(b);}
    std::fclose(f);CHECK(!out.empty());return out;
}
static CodecConfig config(uint32_t rate,uint8_t bits,uint8_t channels=2){CodecConfig c;c.codec=Codec::Ssc;c.sample_rate=rate;c.bits_per_sample=bits;c.channels=channels;return c;}
// Exact-size heap copy, so any read past the payload reaches an ASan redzone.
struct Exact { uint8_t* p;size_t n; explicit Exact(const uint8_t* d,size_t k):p((uint8_t*)std::malloc(k?k:1)),n(k){CHECK(p);if(k)std::memcpy(p,d,k);} ~Exact(){std::free(p);} };
// Same contract as GigaBredr::Impl::processOne: advance by consumed, stop at the first error.
struct Walk { size_t frames=0,errors=0,used=0; };
static Walk walk(const DecoderOps* ops,void* state,const uint8_t* data,size_t bytes,int32_t* pcm,std::vector<int32_t>* out=nullptr){
    Walk w;
    while(w.used<bytes){
        const size_t calls=provider_calls;
        DecodeResult r=ops->decode(state,data+w.used,bytes-w.used,pcm,SSC_FRAME_SAMPLES);
        CHECK(provider_calls==calls); // No decode-time allocation, on success or failure.
        if(r.status!=Status::Ok){CHECK(r.status==Status::CorruptFrame&&!r.consumed&&!r.frames);++w.errors;break;}
        CHECK(r.consumed>=6&&r.consumed<=bytes-w.used&&r.frames==SSC_FRAME_SAMPLES);
        if(out)out->insert(out->end(),pcm,pcm+2*SSC_FRAME_SAMPLES);
        w.used+=r.consumed;w.frames+=r.frames;
    }
    return w;
}
static int decode(int argc,char** argv){
    if(argc!=7)return 2;
    const uint32_t rate=uint32_t(std::atoi(argv[2]));const uint8_t bits=uint8_t(std::atoi(argv[3]));const size_t expected=size_t(std::atol(argv[6]));
    auto packets=records(argv[4]);std::vector<int32_t> pcm(2*SSC_FRAME_SAMPLES),out;
    const auto* ops=codecOperations(Codec::Ssc);CHECK(ops&&(compiledCodecs()&codecBit(Codec::Ssc)));
    CHECK(memoryStats().current_blocks==0);void* state=nullptr;const size_t opened=provider_calls;
    CHECK(ops->open(&state,config(rate,bits))==Status::Ok&&state);
    const auto live=memoryStats();
    // Adapter state plus the decoder's three blocks (ssc_decoder_size), and nothing else.
    CHECK(live.current_blocks==4&&provider_calls-opened==4&&live.current_bytes>size_t(ssc_decoder_size())&&live.current_bytes<=size_t(ssc_decoder_size())+4*sizeof(void*));
    MediaAssembler media;CHECK(media.configure(config(rate,bits),8192)==Status::Ok&&memoryStats().current_blocks==4); // No SSC assembly buffer.
    size_t histogram[3]={};uint32_t sizes=0;size_t previous=0;
    for(const auto& packet:packets){
        Exact exact(packet.data(),packet.size());MediaView view;
        CHECK(media.feed(exact.p,exact.n,view)==Status::Ok&&view.data==exact.p+12&&view.bytes==exact.n-12);
        const auto w=walk(ops,state,view.data,view.bytes,pcm.data(),&out);
        CHECK(!w.errors&&w.used==view.bytes&&w.frames/SSC_FRAME_SAMPLES>=1&&w.frames/SSC_FRAME_SAMPLES<=2);
        ++histogram[w.frames/SSC_FRAME_SAMPLES];media.consumed();
        if(view.bytes!=previous)++sizes;previous=view.bytes;
    }
    CHECK(out.size()==expected*2*SSC_FRAME_SAMPLES&&memoryStats().current_blocks==4);
    media.close();ops->close(state);CHECK(memoryStats().current_blocks==0&&memoryStats().current_bytes==0&&provider_calls==provider_frees);
    FILE* f=std::fopen(argv[5],"wb");CHECK(f&&std::fwrite(out.data(),sizeof(int32_t),out.size(),f)==out.size());std::fclose(f);
    std::printf("SSC_DECODE_PASS rate=%u bits=%u packets=%zu one_frame=%zu two_frame=%zu payload_size_changes=%u frames=%zu state_bytes=%d checks=%u\n",
        rate,bits,packets.size(),histogram[1],histogram[2],sizes,out.size()/(2*SSC_FRAME_SAMPLES),ssc_decoder_size(),checks);
    return 0;
}
static uint32_t rng=0x5eed1234u;
static uint32_t next(){rng=rng*1664525u+1013904223u;return rng;}
static int malformed(int argc,char** argv){
    if(argc!=4)return 2;
    const uint32_t rate=uint32_t(std::atoi(argv[2]));auto packets=records(argv[3]);
    const auto* ops=codecOperations(Codec::Ssc);void* state=nullptr;std::vector<int32_t> pcm(2*SSC_FRAME_SAMPLES);
    CHECK(ops->open(&state,config(rate,24))==Status::Ok);const size_t base=provider_calls;
    unsigned truncated=0,headers=0,flips=0,garbage=0,corrupt=0;
    auto recovered=[&](){Bytes p(packets[0].begin()+12,packets[0].end());Exact e(p.data(),p.size());const auto w=walk(ops,state,e.p,e.n,pcm.data());CHECK(!w.errors&&w.used==e.n);};
    // Every truncation of the first packets: whole leading frames decode, the cut frame fails.
    for(size_t k=0;k<packets.size()&&k<3;++k){
        Bytes p(packets[k].begin()+12,packets[k].end());size_t boundary=0;
        {Exact e(p.data(),p.size());DecodeResult r=ops->decode(state,e.p,e.n,pcm.data(),SSC_FRAME_SAMPLES);CHECK(r.status==Status::Ok);boundary=r.consumed;}
        for(size_t n=1;n<p.size();++n){
            Exact e(p.data(),n);const auto w=walk(ops,state,e.p,e.n,pcm.data());
            if(n==boundary)CHECK(!w.errors&&w.used==n); // Cut exactly between two 96 kHz frames.
            else CHECK(w.errors==1&&w.used==(n>boundary?boundary:0));
            corrupt+=w.errors;++truncated;
        }
        recovered();
    }
    // Every value of each header byte.
    for(unsigned at=0;at<4;++at)for(unsigned v=0;v<256;++v){
        Bytes p(packets[1].begin()+12,packets[1].end());p[at]=uint8_t(v);Exact e(p.data(),p.size());corrupt+=walk(ops,state,e.p,e.n,pcm.data()).errors;++headers;
    }
    recovered();
    // Random payload corruption keeps a valid header: range decoder must stay inside the frame.
    for(unsigned i=0;i<1500;++i){
        const auto& src=packets[next()%packets.size()];Bytes p(src.begin()+12,src.end());
        for(unsigned j=1+next()%16;j;--j)p[4+next()%(p.size()-4)]^=uint8_t(1u<<(next()%8));
        Exact e(p.data(),p.size());corrupt+=walk(ops,state,e.p,e.n,pcm.data()).errors;++flips;
    }
    recovered();
    // Garbage after a syntactically valid header for every rate code, at sizes around the frame size.
    static const uint8_t families[][2]={{0xee,0x10},{0xee,0x11},{0xee,0x12},{0xee,0x13},{0xee,0x14},{0xee,0x15},{0xee,0x16},
        {0xe0,0},{0xe0,1},{0xe0,2},{0xe0,3},{0xe0,4},{0xe0,5},{0xe0,6},{0xe0,0xe6}};
    for(const auto& f:families)for(unsigned i=0;i<12;++i){
        Bytes p(6+next()%2300);for(auto& b:p)b=uint8_t(next());p[0]=0xff;p[1]=uint8_t(f[0]|(f[0]==0xe0?next()&15:0));p[3]=uint8_t(f[1]|(f[1]<0xe0?0xe0:0));
        Exact e(p.data(),p.size());corrupt+=walk(ops,state,e.p,e.n,pcm.data()).errors;++garbage;
    }
    recovered();
    CHECK(provider_calls==base&&memoryStats().current_blocks==4);
    // Undersized output, empty and tiny inputs never touch the decoder.
    {Bytes p(packets[0].begin()+12,packets[0].end());Exact e(p.data(),p.size());
     CHECK(ops->decode(state,e.p,e.n,pcm.data(),SSC_FRAME_SAMPLES-1).status==Status::OutputFull);
     CHECK(ops->decode(state,e.p,0,pcm.data(),SSC_FRAME_SAMPLES).status==Status::InvalidArgument);
     for(size_t n=1;n<6;++n)CHECK(ops->decode(state,e.p,n,pcm.data(),SSC_FRAME_SAMPLES).status==Status::CorruptFrame);}
    ops->close(state);CHECK(memoryStats().current_blocks==0&&provider_calls==provider_frees);
    // Media framing: RTP then frames; payloads below one header or above the access unit are refused.
    MediaAssembler media;CHECK(media.configure(config(rate,24),1024)==Status::Ok);MediaView view;
    {Bytes p(packets[0].begin(),packets[0].begin()+12+5);Exact e(p.data(),p.size());CHECK(media.feed(e.p,e.n,view)==Status::CorruptFrame&&!view.data);}
    {Bytes p(packets[0].begin(),packets[0].begin()+12);p.resize(12+1025,0xa5);Exact e(p.data(),p.size());CHECK(media.feed(e.p,e.n,view)==Status::CorruptFrame&&!view.data);}
    {Bytes p(packets[0]);p[0]=0x40;Exact e(p.data(),p.size());CHECK(media.feed(e.p,e.n,view)==Status::CorruptFrame);}
    media.close();CHECK(memoryStats().current_blocks==0);
    std::printf("SSC_MALFORMED_PASS rate=%u truncations=%u header_values=%u payload_flips=%u garbage=%u rejected=%u decode_allocations=0 checks=%u\n",
        rate,truncated,headers,flips,garbage,corrupt,checks);
    return 0;
}
static int memory(){
    const auto* ops=codecOperations(Codec::Ssc);CHECK(ops);unsigned refused=0,oom=0,cycles=0;
    const uint32_t rates[]={44100,48000,96000};
    for(uint32_t rate:rates)for(uint8_t bits:{uint8_t(16),uint8_t(24)}){
        for(int fail=0;fail<=4;++fail){
            void* state=nullptr;setAllocationFailureCountdown(fail);Status s=ops->open(&state,config(rate,bits));setAllocationFailureCountdown(-1);
            if(fail<4){CHECK(s==Status::OutOfMemory&&!state);++oom;}else{CHECK(s==Status::Ok&&state&&memoryStats().current_blocks==4);ops->close(state);}
            CHECK(memoryStats().current_blocks==0&&memoryStats().current_bytes==0&&provider_calls==provider_frees);
        }
        for(unsigned i=0;i<50;++i){void* state=nullptr;CHECK(ops->open(&state,config(rate,bits))==Status::Ok);ops->close(state);++cycles;}
        CHECK(memoryStats().current_blocks==0&&provider_calls==provider_frees);
    }
    const CodecConfig bad[]={config(44100,16,1),config(32000,16),config(88200,24),config(192000,24),config(16000,16),config(48000,32)};
    for(const auto& c:bad){void* state=nullptr;CHECK(ops->open(&state,c)==Status::Unsupported&&!state&&memoryStats().current_blocks==0);++refused;}
    ops->close(nullptr);
    std::printf("SSC_MEMORY_PASS open_failures=%u refused=%u cycles=%u state_bytes=%d leaks=0 checks=%u\n",oom,refused,cycles,ssc_decoder_size(),checks);
    return 0;
}
int main(int argc,char** argv){
    CHECK(setMemoryProvider(MemoryKind::Fast,countingAllocate,countingRelease,nullptr));
    if(argc>1&&!std::strcmp(argv[1],"decode"))return decode(argc,argv);
    if(argc>1&&!std::strcmp(argv[1],"malformed"))return malformed(argc,argv);
    if(argc==2&&!std::strcmp(argv[1],"memory"))return memory();
    return 2;
}
