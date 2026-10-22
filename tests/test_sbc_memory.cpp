#include "codecs/Codecs.h"
#include "detail/Memory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <vector>
extern "C" {
#include "vendor/codecs/sbc/include/oi_codec_sbc.h"
}
using namespace giga_bredr;
using namespace giga_bredr::detail;
static std::unordered_map<void*,size_t> live;
static std::vector<size_t> requests;
static size_t attempts;
static int failAfter=-1;
static bool deny=false;
extern "C" void* gbr_malloc(size_t n) {
    ++attempts;requests.push_back(n);
    if(deny){fputs("UNEXPECTED_DECODE_ALLOCATION_ATTEMPT\n",stderr);abort();}
    if(failAfter==0)return nullptr;
    if(failAfter>0)--failAfter;
    void* p=malloc(n);if(p){memset(p,0x5a,n);live[p]=n;}return p;
}
extern "C" void* gbr_calloc(size_t n,size_t s) {
    if(s && n>SIZE_MAX/s)return nullptr;
    void* p=gbr_malloc(n*s);if(p)memset(p,0,n*s);return p;
}
extern "C" void gbr_free(void* p){if(p){live.erase(p);free(p);}}
namespace giga_bredr {namespace detail {
MemoryStats memoryStats(){MemoryStats m;for(auto e:live)m.current_bytes+=e.second;m.current_blocks=live.size();return m;}
}}
static void check(bool ok,const char* what){if(!ok){printf("FAIL %s\n",what);exit(3);}}
static CodecConfig selected(unsigned blocks,unsigned bands,unsigned mode,unsigned alloc,unsigned frequency=3) {
    CodecConfig c;c.sample_rate=frequency==0?16000:frequency==1?32000:frequency==2?44100:48000;c.channels=mode?2:1;
    c.codec_info_size=4;c.codec_info[0]=(0x80>>frequency)|(8>>mode);
    c.codec_info[1]=(0x80>>(blocks/4-1))|(bands==8?4:8)|(alloc?2:1);
    c.codec_info[2]=2;c.codec_info[3]=std::min(250u,(mode<2?16:32)*bands);
    return c;
}
// Standalone bitwise CRC and zero-scalefactor payload: a bounds/equivalence
// fixture, not an independent SBC fidelity oracle.
static std::vector<uint8_t> frame(unsigned blocks,unsigned bands,unsigned mode,unsigned alloc,unsigned frequency,unsigned pool) {
    unsigned channels=mode?2:1,audioBits=blocks*pool*(mode<2?channels:1)+(mode==3?bands:0);
    std::vector<uint8_t> f(4+bands*channels/2+(audioBits+7)/8,0);
    f[0]=0x9c;f[1]=(frequency<<6)|((blocks/4-1)<<4)|(mode<<2)|(alloc<<1)|(bands==8);f[2]=pool;
    uint8_t crc=0x0f;
    auto bit=[&](unsigned b){unsigned carry=(crc>>7)^b;crc=uint8_t(crc<<1);if(carry)crc^=0x1d;};
    for(unsigned i=8;i>0;--i)bit((f[1]>>(i-1))&1);
    for(unsigned i=8;i>0;--i)bit((f[2]>>(i-1))&1);
    for(unsigned i=0;i<(mode==3?bands:0)+4*bands*channels;++i)bit(0);
    f[3]=crc;return f;
}
struct Legacy {
    OI_CODEC_SBC_DECODER_CONTEXT context;
    std::vector<OI_UINT32> work;
    explicit Legacy(unsigned channels):work(944*channels/4,0){check(OI_CODEC_SBC_DecoderReset(&context,work.data(),work.size()*4,channels,channels,0,0)==OI_OK,"legacy Reset");}
    std::vector<int16_t> decode(const std::vector<uint8_t>& f,unsigned frames,unsigned channels){
        std::vector<int16_t> pcm(frames*channels,0x34);const OI_BYTE* p=f.data();OI_UINT32 n=f.size(),bytes=pcm.size()*2;
        check(OI_CODEC_SBC_DecodeFrame(&context,&p,&n,pcm.data(),&bytes)==OI_OK,"legacy valid fixture decode");
        check(n==0 && bytes==frames*channels*2,"legacy full consume/output");return pcm;
    }
};
static std::vector<std::pair<void*,std::vector<uint8_t>>> snapshot(){
    std::vector<std::pair<void*,std::vector<uint8_t>>> copy;
    for(auto e:live){auto p=(uint8_t*)e.first;copy.push_back({e.first,std::vector<uint8_t>(p,p+e.second)});}return copy;
}
static void unchanged(const std::vector<std::pair<void*,std::vector<uint8_t>>>& saved){
    for(auto& e:saved)check(live.count(e.first) && memcmp(e.first,e.second.data(),e.second.size())==0,"rejected selection mutated decoder/storage");
}
static void rejected(const DecoderOps* ops,void* state,const std::vector<uint8_t>& f,unsigned count,unsigned channels,Status want,bool stateUnchanged=true){
    const int32_t guard=0x3578912;std::vector<int32_t> pcm(count*channels+2,guard);auto saved=snapshot();size_t calls=attempts;
    auto r=ops->decode(state,f.data(),f.size(),pcm.data()+1,count);
    check(r.status==want && r.frames==0 && r.consumed==0,"rejected frame status/commit");
    for(auto x:pcm)check(x==guard,"rejected frame wrote output");check(attempts==calls,"rejected frame allocated");
    if(stateUnchanged)unchanged(saved);
}
static void matrix(){
    unsigned configs=0,decoded=0;auto ops=codecOperations(Codec::Sbc);
    for(unsigned frequency=0;frequency<4;++frequency)for(unsigned mode=0;mode<4;++mode)
    for(unsigned blocks:{4u,8u,12u,16u})for(unsigned bands:{4u,8u})for(unsigned alloc=0;alloc<2;++alloc){
        CodecConfig c=selected(blocks,bands,mode,alloc,frequency);unsigned channels=c.channels,count=blocks*bands,maxPool=c.codec_info[3];
        requests.clear();void* state=nullptr;check(ops->open(&state,c)==Status::Ok,"matrix selected open");
        check(requests.size()==3 && requests[1]==(4*count+432)*channels && requests[2]==2*count*channels,"matrix requested bounds");
        Legacy old(channels);size_t steady=memoryStats().current_bytes;size_t calls=attempts;deny=true;
        for(unsigned i=0;i<60;++i){
            unsigned pool=i%3==0?2:i%3==1?maxPool:(maxPool+2)/2;
            auto f=frame(blocks,bands,mode,alloc,frequency,pool);
            if(i==0){
                rejected(ops,state,f,count-1,channels,Status::OutputFull);
                for(unsigned shortSize=1;shortSize<4;++shortSize){auto shortFrame=f;shortFrame.resize(shortSize);rejected(ops,state,shortFrame,count,channels,Status::NeedMoreData);}
                auto shortFrame=f;shortFrame.pop_back();rejected(ops,state,shortFrame,count,channels,Status::NeedMoreData);
                auto bad=f;bad[3]^=1;rejected(ops,state,bad,count,channels,Status::CorruptFrame,false);
                rejected(ops,state,frame(blocks,bands,mode,alloc,(frequency+1)%4,2),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks,bands,mode^1,alloc,frequency,2),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks==4?16:4,bands,mode,alloc,frequency,2),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks,bands==4?8:4,mode,alloc,frequency,2),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks,bands,mode,alloc^1,frequency,2),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks,bands,mode,alloc,frequency,1),128,channels,Status::Unsupported);
                rejected(ops,state,frame(blocks,bands,mode,alloc,frequency,maxPool+1),128,channels,Status::Unsupported);
            }
            auto ref=old.decode(f,count,channels);std::vector<int32_t> pcm(count*channels+2,0x3578912);
            auto r=ops->decode(state,f.data(),f.size(),pcm.data()+1,count);
            check(r.status==Status::Ok && r.frames==count && r.consumed==f.size(),"matrix decode result");
            check(pcm.front()==0x3578912 && pcm.back()==0x3578912,"matrix output guard");
            for(size_t j=0;j<ref.size();++j)check(pcm[j+1]==int32_t(ref[j])*65536,"compact/legacy PCM equivalence");
            check(memoryStats().current_bytes==steady && attempts==calls,"decode heap attempt/growth");++decoded;
        }
        deny=false;ops->close(state);check(live.empty(),"matrix cleanup");++configs;
    }
    printf("SBC_GEOMETRY_BOUNDS_EQUIVALENCE_PASS configurations=%u frames=%u (not independent fidelity)\n",configs,decoded);
}
static void malformed(){
    auto good=selected(4,4,2,0);std::vector<CodecConfig> bad;
    for(unsigned n:{1u,2u,3u,5u,24u,25u}){auto c=good;c.codec_info_size=n;bad.push_back(c);}
    for(unsigned byte:{0u,1u})for(unsigned value:{0u,255u}){auto c=good;c.codec_info[byte]=value;bad.push_back(c);}
    for(unsigned value:{0u,1u,129u,250u,251u}){auto c=good;c.codec_info[2]=value;bad.push_back(c);}
    for(unsigned value:{0u,1u,129u,250u,251u}){auto c=good;c.codec_info[3]=value;bad.push_back(c);}
    {auto c=good;c.channels=1;bad.push_back(c);c=good;c.sample_rate=44100;bad.push_back(c);}
    auto ops=codecOperations(Codec::Sbc);
    for(auto& c:bad){size_t calls=attempts;void* state=(void*)1;check(ops->open(&state,c)==Status::Unsupported && state==nullptr,"malformed config accepted");check(attempts==calls && live.empty(),"malformed config allocated");}
    for(unsigned mode=0;mode<4;++mode)for(unsigned bands:{4u,8u}){
        auto c=selected(4,bands,mode,0);
        for(int fail=0;fail<4;++fail){void* state=nullptr;failAfter=fail;auto s=ops->open(&state,c);failAfter=-1;
            if(fail<3)check(s==Status::OutOfMemory && state==nullptr,"open OOM status/rollback");
            else{check(s==Status::Ok && state,"open after three allocations");ops->close(state);}check(live.empty(),"OOM residue");}
    }
    printf("SBC_MALFORMED_NO_ALLOCATION_OOM_PASS malformed=%zu positions=32\n",bad.size());
}
static void fallback(){
    auto ops=codecOperations(Codec::Sbc);unsigned decoded=0;
    for(unsigned frequency=0;frequency<4;++frequency)for(unsigned channels=1;channels<=2;++channels){
        CodecConfig c=selected(4,4,channels==1?0:2,0,frequency);c.codec_info_size=0;requests.clear();void* state=nullptr;
        check(ops->open(&state,c)==Status::Ok,"legacy no-info open");check(requests.size()==3 && requests[1]==944*channels && requests[2]==256*channels,"legacy no-info allocation");
        Legacy old(channels);size_t calls=attempts;deny=true;
        for(unsigned mode=channels==1?0:1;mode<(channels==1?1:4);++mode)for(unsigned blocks:{4u,8u,12u,16u})for(unsigned bands:{4u,8u})for(unsigned alloc=0;alloc<2;++alloc){
            auto f=frame(blocks,bands,mode,alloc,frequency,2);auto ref=old.decode(f,blocks*bands,channels);std::vector<int32_t> pcm(128*channels+2,0x3578912);
            auto r=ops->decode(state,f.data(),f.size(),pcm.data()+1,128);check(r.status==Status::Ok && r.frames==blocks*bands && r.consumed==f.size(),"legacy no-info in-band choice");
            for(size_t j=0;j<ref.size();++j)check(pcm[j+1]==int32_t(ref[j])*65536,"legacy no-info output changed");
            check(pcm.front()==0x3578912 && pcm.back()==0x3578912 && calls==attempts,"legacy no-info guard/allocation");++decoded;
        }
        deny=false;ops->close(state);check(live.empty(),"legacy no-info cleanup");
    }
    printf("SBC_NOINFO_LEGACY_CHOICES_PASS frames=%u\n",decoded);
}
static void rawPartial(){
    // Mono encoded data into a stride-two output is accepted by the existing
    // public ConfigureRaw API. Compare one-block pulls with one whole pull.
    auto f=frame(16,4,0,0,3,8);
    for(size_t i=6;i<f.size();++i)f[i]=uint8_t(29+73*i);
    OI_CODEC_SBC_DECODER_CONTEXT whole,part;
    std::vector<OI_UINT32> wholeWork(1888/4,0);
    std::vector<OI_UINT32> partWork(1376/4,0);
    check(OI_CODEC_SBC_DecoderResetGeometry(&part,partWork.data(),1376,2,2,0,0,4,16)==OI_OK,"bounded raw Reset");
    check(OI_CODEC_SBC_DecoderReset(&whole,wholeWork.data(),1888,2,2,0,0)==OI_OK,"legacy whole raw Reset");
    for(auto ctx:{&whole,&part})check(OI_CODEC_SBC_DecoderConfigureRaw(ctx,0,3,0,0,3,0,8)==OI_OK,"actual public mono ConfigureRaw stride2");
    const OI_BYTE* data=f.data()+4;OI_UINT32 bytes=f.size()-4,pcmBytes=256;std::vector<int16_t> expected(128,0),actual;
    check(OI_CODEC_SBC_DecodeRaw(&whole,8,&data,&bytes,expected.data(),&pcmBytes)==OI_OK && bytes==0 && pcmBytes==256,"actual whole DecodeRaw");
    data=f.data()+4;bytes=f.size()-4;
    for(unsigned block=0;block<16;++block){int16_t out[10];for(auto& x:out)x=0x3567;pcmBytes=16;
        auto s=OI_CODEC_SBC_DecodeRaw(&part,8,&data,&bytes,out+1,&pcmBytes);
        check(s==(block<15?OI_CODEC_SBC_PARTIAL_DECODE:OI_OK) && pcmBytes==16 && bytes==0,"actual partial DecodeRaw status/consume");
        check(out[0]==0x3567 && out[9]==0x3567,"partial raw output guard");actual.insert(actual.end(),out+1,out+9);
    }
    if(actual!=expected){size_t first=0;while(first<actual.size() && actual[first]==expected[first])++first;printf("RAW_PARTIAL_MISMATCH sample=%zu got=%d expected=%d\n",first,actual[first],expected[first]);}
    check(actual==expected,"4SB actual DecodeRaw partial versus whole");
    puts("SBC_4SB_PARTIAL_RAW_EQUIVALENCE_BOUNDS_PASS");
}
static void boundedApi(){
    unsigned tested=0;
    for(unsigned channels:{1u,2u})for(unsigned blocks:{4u,8u,12u,16u})for(unsigned bands:{4u,8u}){
        size_t count=blocks*bands,bytes=(4*count+432)*channels;
        std::vector<OI_UINT32> work(bytes/4,0);OI_CODEC_SBC_DECODER_CONTEXT context;
        check(OI_CODEC_SBC_DecoderResetGeometry(&context,work.data(),bytes,channels,channels,0,0,bands,blocks)==OI_OK,"bounded API Reset");
        check(context.common.filterBufferLen==216,"bounded API preserves fast27 filters");
        for(unsigned bad=0;bad<2;++bad){
            if((bad==0 && bands==8)||(bad==1 && blocks==16))continue;
            auto f=frame(bad==1?16:blocks,bad==0?8:bands,channels==1?0:2,0,3,2);
            auto old=work;const OI_BYTE* p=f.data();OI_UINT32 n=f.size();std::vector<int16_t> out(128*channels+2,0x3567);OI_UINT32 pcmBytes=128*channels*2;
            auto status=OI_CODEC_SBC_DecodeFrame(&context,&p,&n,out.data()+1,&pcmBytes);
            check(status==OI_STATUS_INVALID_PARAMETERS && n==f.size() && work==old,"bounded API rejects larger geometry before sample/history writes");
            for(auto x:out)check(x==0x3567,"bounded API oversized geometry output");
        }
        auto f=frame(blocks,bands,channels==1?0:2,0,3,2);const OI_BYTE* p=f.data();OI_UINT32 n=f.size(),pcmBytes=count*channels*2;std::vector<int16_t> out(count*channels,0);
        check(OI_CODEC_SBC_DecodeFrame(&context,&p,&n,out.data(),&pcmBytes)==OI_OK && n==0,"bounded API valid after reject");
        ++tested;
    }
    printf("SBC_BOUNDED_RESET_DIRECT_GUARD_PASS configurations=%u\n",tested);
}
static void extraRawMatrix() {
    unsigned configurations=0,pulls=0;
    for(unsigned frequency=0;frequency<4;++frequency)for(unsigned mode=0;mode<1;++mode)
    for(unsigned blocks:{4u,8u,12u,16u})for(unsigned bands:{4u,8u})for(unsigned alloc=0;alloc<2;++alloc)
    for(unsigned seed:{0u,1u})for(unsigned chunk:{1u,3u,5u}) {
        auto f=frame(blocks,bands,mode,alloc,frequency,8);
        if(seed)for(size_t i=4;i<f.size();++i)f[i]=uint8_t(29+73*i);
        const size_t samples=blocks*bands*2,workBytes=(4*blocks*bands+432)*2;
        std::vector<OI_UINT32> wholeWork(1888/4,0),partWork(workBytes/4,0);
        OI_CODEC_SBC_DECODER_CONTEXT whole,part;
        check(OI_CODEC_SBC_DecoderReset(&whole,wholeWork.data(),1888,2,2,0,0)==OI_OK,"review raw whole Reset");
        check(OI_CODEC_SBC_DecoderResetGeometry(&part,partWork.data(),workBytes,2,2,0,0,bands,blocks)==OI_OK,"review raw bounded Reset");
        check(part.common.filterBufferLen==216,"review retained fast27 ring");
        for(auto ctx:{&whole,&part})check(OI_CODEC_SBC_DecoderConfigureRaw(ctx,0,frequency,mode,bands==8,blocks/4-1,alloc,8)==OI_OK,"review raw Configure");
        std::vector<int16_t> expected(samples+2,0x3567),actual;
        const OI_BYTE* p=f.data()+4;OI_UINT32 bytes=f.size()-4,pcmBytes=samples*2;
        const size_t calls=attempts;deny=true;
        check(OI_CODEC_SBC_DecodeRaw(&whole,8,&p,&bytes,expected.data()+1,&pcmBytes)==OI_OK && bytes==0 && pcmBytes==samples*2,"review whole raw consume/output");
        check(expected.front()==0x3567 && expected.back()==0x3567,"review whole guard");
        p=f.data()+4;bytes=f.size()-4;
        for(unsigned start=0;start<blocks;) {
            unsigned count=std::min(chunk,blocks-start);std::vector<int16_t> piece(count*bands*2+2,0x3567);pcmBytes=count*bands*4;
            auto status=OI_CODEC_SBC_DecodeRaw(&part,8,&p,&bytes,piece.data()+1,&pcmBytes);
            check(status==(start+count<blocks?OI_CODEC_SBC_PARTIAL_DECODE:OI_OK) && bytes==0 && pcmBytes==count*bands*4,"review partial raw status/consume");
            check(piece.front()==0x3567 && piece.back()==0x3567,"review partial raw guard");
            actual.insert(actual.end(),piece.begin()+1,piece.end()-1);start+=count;++pulls;
        }
        check(attempts==calls,"review raw decode allocated");deny=false;
        check(actual.size()==samples && std::equal(actual.begin(),actual.end(),expected.begin()+1),"review raw partial/whole equality");
        ++configurations;
    }
    printf("INDEPENDENT_REVIEW_RAW_PARTIAL_PASS configurations=%u pulls=%u; same-library equivalence only\n",configurations,pulls);
}
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    matrix();malformed();fallback();rawPartial();boundedApi();extraRawMatrix();
    puts("PASS portable SBC memory geometry; generated bounds/equivalence, no independent four-band PCM oracle");
    return 0;
}
