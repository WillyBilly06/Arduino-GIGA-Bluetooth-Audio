// Real adapter/vendor tests; memory/state contracts, not independent PCM fidelity.
#include "codecs/Codecs.cpp"
#include "fixtures/opus_memory_packets.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
using namespace giga_bredr;using namespace giga_bredr::detail;
extern "C" {int cng_capacity(void);int cng_state_size(void);int cng_super_size(void);int run_opus_cng_storage(void);}
static std::map<void*,size_t> live;
static size_t live_bytes=0,allocation_attempts=0,peak_bytes=0;
static int fail_after=-1;static bool decoding=false;
extern "C" void* gbr_malloc(size_t n){
    ++allocation_attempts;if(decoding){fputs("unexpected codec allocation during decode\n",stderr);abort();}
    if(fail_after==0)return nullptr;if(fail_after>0)--fail_after;
    void*p=malloc(n);if(p){memset(p,0x5a,n);live[p]=n;live_bytes+=n;peak_bytes=std::max(peak_bytes,live_bytes);}return p;
}
extern "C" void* gbr_calloc(size_t n,size_t s){if(s&&n>SIZE_MAX/s)return nullptr;void*p=gbr_malloc(n*s);if(p)memset(p,0,n*s);return p;}
extern "C" void gbr_free(void*p){if(!p)return;auto it=live.find(p);assert(it!=live.end());live_bytes-=it->second;live.erase(it);free(p);}
namespace giga_bredr{namespace detail{MemoryStats memoryStats(){MemoryStats m;m.current_bytes=live_bytes;m.current_blocks=live.size();m.peak_bytes=peak_bytes;return m;}void setAllocationFailureCountdown(int n){fail_after=n;}}}
struct Sequence{const char*name;std::vector<std::vector<unsigned char>> packets;};
static std::vector<Sequence> sequences(){
    std::vector<Sequence> result;
    for(const auto& source:opus_memory_sequences){Sequence s;s.name=source.name;size_t pos=0;
        while(pos<source.size){assert(source.size-pos>=4);const auto*p=source.bytes+pos;uint32_t n=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;pos+=4;
            assert(n&&n<=source.size-pos);s.packets.emplace_back(source.bytes+pos,source.bytes+pos+n);pos+=n;}
        assert(!s.packets.empty());result.push_back(s);
    }return result;
}
static void checkClean(){assert(live_bytes==0&&live.empty());}
static void direct(const std::vector<Sequence>& seq){
    unsigned calls=0,loss=0,fec=0;
    for(int rate:{8000,12000,16000,24000,48000})for(int channels:{1,2})for(unsigned policy=0;policy<4;++policy){
        int error=0;auto*decoder=opus_decoder_create(rate,channels,&error);assert(decoder&&error==OPUS_OK);
        for(const auto&s:seq){if(policy==3)assert(opus_decoder_ctl(decoder,OPUS_RESET_STATE)==OPUS_OK);
            for(unsigned i=0;i<s.packets.size();++i){const auto&p=s.packets[i];int frames=opus_packet_get_nb_samples(p.data(),(opus_int32)p.size(),rate);assert(frames>0&&frames<=rate*120/1000);
                std::vector<int16_t> pcm(frames*channels+2,12345);size_t attempts=allocation_attempts;decoding=true;int got;
                if(policy==1&&i%5==2){got=opus_decode(decoder,nullptr,0,pcm.data()+1,frames,0);++loss;}
                else {int useFec=policy==2&&i%7==3;got=opus_decode(decoder,p.data(),(opus_int32)p.size(),pcm.data()+1,frames,useFec);fec+=useFec;}
                decoding=false;assert(got>=0&&got<=frames&&pcm.front()==12345&&pcm.back()==12345&&attempts==allocation_attempts);++calls;
            }
        }opus_decoder_destroy(decoder);checkClean();
    }
    printf("PASS Opus actual SILK/Hybrid/CELT/DTX transitions calls=%u PLC=%u FEC=%u\n",calls,loss,fec);
}
static void wrapper(const std::vector<Sequence>& seq){
    const DecoderOps*ops=codecOperations(Codec::Opus);assert(ops);
    const auto found=std::find_if(seq.begin(),seq.end(),[](const Sequence&s){return !strcmp(s.name,"celt-fb2_5-stereo");});assert(found!=seq.end());
    const auto& aliasPacket=found->packets.front();unsigned configs=0,oom=0,calls=0,mutations=0;uint32_t rng=0x34a71829;
    auto next=[&](){rng=rng*1664525u+1013904223u;return rng;};
    for(int rate:{8000,12000,16000,24000,48000})for(int channels:{1,2})for(int duration:{2500,5000,10000,20000,40000,60000}){
        CodecConfig c;c.codec=Codec::Opus;c.sample_rate=rate;c.channels=channels;c.frame_duration_us=duration;void*state=nullptr;
        for(int fail=0;fail<5;++fail){fail_after=fail;Status status=ops->open(&state,c);fail_after=-1;
            if(fail<3)assert(status==Status::OutOfMemory&&!state);else{assert(status==Status::Ok&&state);ops->close(state);state=nullptr;}checkClean();++oom;}
        assert(ops->open(&state,c)==Status::Ok&&state);size_t frames=(uint64_t)rate*duration/1000000,bytes=live_bytes,blocks=live.size();assert(blocks==3);
        assert(bytes==sizeof(OpusState)+size_t(opus_decoder_get_size(channels))+frames*channels*2);
        for(const auto&s:seq)for(const auto&p:s.packets){std::vector<int32_t> out(frames*channels+2,0x3578912);size_t attempts=allocation_attempts;
            decoding=true;auto result=ops->decode(state,p.data(),p.size(),out.data()+1,frames);decoding=false;
            assert(result.frames<=frames&&result.consumed<=p.size()&&out.front()==0x3578912&&out.back()==0x3578912);
            if(result.status!=Status::Ok)for(auto sample:out)assert(sample==0x3578912);
            assert(allocation_attempts==attempts&&live_bytes==bytes&&live.size()==blocks);++calls;}
        for(unsigned i=0;i<24;++i){auto p=aliasPacket;if(i<12)p.resize(i%p.size());else p[next()%p.size()]^=1u<<(next()%8);
            unsigned char zero=0;std::vector<int32_t> out(frames*channels+2,0x3578912);size_t attempts=allocation_attempts;decoding=true;
            auto result=ops->decode(state,p.empty()?&zero:p.data(),p.size(),out.data()+1,frames);decoding=false;
            assert(result.frames<=frames&&result.consumed<=p.size()&&out.front()==0x3578912&&out.back()==0x3578912);
            if(result.status!=Status::Ok)for(auto sample:out)assert(sample==0x3578912);
            assert(allocation_attempts==attempts&&live_bytes==bytes&&live.size()==blocks);++mutations;}
        ops->close(state);checkClean();void*normal=nullptr,*aliased=nullptr;assert(ops->open(&normal,c)==Status::Ok&&ops->open(&aliased,c)==Status::Ok);
        c.sample_rate=0;c.channels=0;c.frame_duration_us=1; // Caller-owned configuration lifetime cannot change retained geometry.
        std::vector<int32_t> out(frames*channels+2,0x3578912),alias(std::max(frames*channels,(aliasPacket.size()+3)/4)+2,0x3578912);memcpy(alias.data()+1,aliasPacket.data(),aliasPacket.size());
        size_t attempts=allocation_attempts;decoding=true;auto a=ops->decode(normal,aliasPacket.data(),aliasPacket.size(),out.data()+1,frames);
        auto b=ops->decode(aliased,reinterpret_cast<const uint8_t*>(alias.data()+1),aliasPacket.size(),alias.data()+1,frames);decoding=false;
        assert(a.status==Status::Ok&&a.status==b.status&&a.frames==b.frames&&a.consumed==b.consumed&&!memcmp(out.data()+1,alias.data()+1,a.frames*channels*sizeof(int32_t)));
        assert(out.front()==0x3578912&&out.back()==0x3578912&&alias.front()==0x3578912&&alias.back()==0x3578912);
        auto*vendor=((OpusState*)aliased)->decoder;std::vector<unsigned char> saved((unsigned char*)vendor,(unsigned char*)vendor+opus_decoder_get_size(channels));
        std::fill(out.begin(),out.end(),0x3578912);decoding=true;auto full=ops->decode(aliased,aliasPacket.data(),aliasPacket.size(),out.data()+1,1);
        auto invalid=ops->decode(aliased,nullptr,aliasPacket.size(),out.data()+1,frames);decoding=false;
        assert(full.status==Status::OutputFull&&!full.frames&&!full.consumed&&invalid.status==Status::InvalidArgument&&!invalid.frames&&!invalid.consumed);
        for(auto sample:out)assert(sample==0x3578912);assert(!memcmp(saved.data(),vendor,saved.size())&&allocation_attempts==attempts);
        ops->close(normal);ops->close(aliased);checkClean();++configs;
    }
    printf("PASS Opus wrapper configs=%u OOM=%u decoded=%u mutations=%u alias/error/caller-geometry/no-hotalloc\n",configs,oom,calls,mutations);
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);assert(cng_capacity()==256);assert(sizeof(OpusState)<=2*sizeof(void*)+sizeof(size_t)+8);
    printf("Opus compact storage cng=%d channel=%d silk=%d wrapper=%zu mono=%d stereo=%d\n",cng_capacity(),cng_state_size(),cng_super_size(),sizeof(OpusState),opus_decoder_get_size(1),opus_decoder_get_size(2));
    assert(run_opus_cng_storage()==12288);auto seq=sequences();direct(seq);wrapper(seq);checkClean();
    puts("PASS portable codec memory regression; no independent PCM/performance/interoperability claim");return 0;
}
