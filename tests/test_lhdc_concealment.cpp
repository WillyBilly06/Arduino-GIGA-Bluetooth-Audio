// Deterministic channel outcomes exercise the actual frame branches, adapter
// and receiver. This is observability coverage, not an encoded PCM oracle.
#include "GigaBredr.h"
#include "codecs/Codecs.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/lhdc/lhdc_dec_internal.h"
#include "vendor/codecs/lhdc/lhdc_imdct.h"
#include "vendor/codecs/lhdc/lhdc_entropy_dec.h"
}
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <thread>
using namespace giga_bredr;
static int mode;
extern "C" lhdc_dec_ret_t lhdc_test_channel(lhdc_decoder_t* d,int ch,float* out) {
    if(mode==4 || (ch==1 && (mode==1 || mode==3))) return LHDC_DEC_BITSTREAM_ERROR;
    for(unsigned i=0;i<d->header.samples_per_channel;++i)
        out[i]=(ch==0 && (mode==2 || mode==3))?1.e20f:float(10000+10000*ch);
    return LHDC_DEC_OK;
}
// Old source compiles and returns zero, providing a behavioral RED rather
// than failing only because the new public fields are absent.
template<class T> static auto flags(const T& x,int)->decltype(x.concealment_flags){return x.concealment_flags;}
template<class T> static unsigned flags(const T&,long){return 0;}
#define COUNTER(name) template<class T> static auto name(const T& x,int)->decltype(x.name){return x.name;} template<class T> static uint64_t name(const T&,long){return 0;}
COUNTER(lhdc_right_concealed_blocks) COUNTER(lhdc_right_concealed_frames)
COUNTER(lhdc_frame_muted_blocks) COUNTER(lhdc_frame_muted_frames)
static void cachesClose(){lhdc_dec_free_window();lhdc_dec_free_slot_scratch();lhdc_entropy_free();lhdc_imdct_free_480();lhdc_imdct_free_960();lhdc_imdct_free_1920();lhdc_imdct_free_cos();}
static void vendor() {
    for(unsigned rate:{48000u,192000u}) for(unsigned bits:{16u,24u}) {
        auto d=(lhdc_decoder_t*)gbr_calloc(1,lhdc_dec_get_workspace_size(rate,5));
        lhdc_dec_config_t c={};c.sample_rate=(lhdc_dec_sample_rate_t)rate;c.channels=2;c.bit_depth=(lhdc_dec_bitdepth_t)bits;c.frame_duration=LHDC_DEC_FRAME_5MS;c.max_frame_bytes=1536;
        assert(lhdc_dec_init(d,&c));uint8_t input[20]={9,0};int32_t pcm[1922];size_t consumed;uint32_t generated;lhdc_dec_frame_info_t info;
        for(int m:{1,2,3,0,4,0}) {
            mode=m;memset(&info,0xff,sizeof info);pcm[0]=pcm[1921]=0x12345678;
            auto r=lhdc_dec_decode_frame(d,input,sizeof input,pcm+1,960,&consumed,&generated,&info);
            unsigned want=m==1?1:m==2?2:m==3?3:0;
            assert(r==(m==4?LHDC_DEC_BITSTREAM_ERROR:LHDC_DEC_OK));
            assert(flags(info,0)==want);
            assert(pcm[0]==0x12345678 && pcm[1921]==0x12345678);
            if(m!=4){assert(consumed==20 && generated==(rate==48000?240u:960u));
                if(m==1 && bits==24){assert(pcm[1]!=0 && pcm[2]==0);}
                if(m==2 || m==3) for(unsigned i=0;i<generated*2;++i)
                    assert(bits==16?reinterpret_cast<int16_t*>(pcm+1)[i]==0:pcm[i+1]==0);
            }
        }
        memset(&info,0xff,sizeof info);assert(lhdc_dec_decode_frame(nullptr,input,20,pcm,960,&consumed,&generated,&info)==LHDC_DEC_NOT_INITIALIZED);assert(flags(info,0)==0);
        memset(&info,0xff,sizeof info);assert(lhdc_dec_decode_frame(d,nullptr,20,pcm,960,&consumed,&generated,&info)==LHDC_DEC_INVALID_PARAM);assert(flags(info,0)==0);
        memset(&info,0xff,sizeof info);assert(lhdc_dec_decode_frame(d,input,4,pcm,960,&consumed,&generated,&info)==LHDC_DEC_NEED_MORE_DATA);assert(flags(info,0)==0);
        lhdc_dec_deinit(d);gbr_free(d);cachesClose();assert(detail::memoryStats().current_bytes==0);
    }
    puts("LHDC_VENDOR_CONCEALMENT_PASS");
}
namespace giga_bredr {namespace detail {
static RadioCallbacks callbacks;
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c){callbacks=c;return Status::Ok;}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c,const BleConfig&){return radioBegin(n,m,c);}
void radioPoll(){} void radioEnd(){callbacks={};} RadioStats radioStats(){return {};}
BleStats radioBleStats(){return {};} Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;} bool radioBleReadWrite(BleWrite&){return false;}
Status i2sBegin(uint32_t){return Status::Ok;} Status i2sBegin(uint32_t,uint16_t){return Status::Ok;}
size_t i2sWrite(const int32_t*,size_t n){return n;} void i2sEnd(){} OutputStats i2sStats(){return {};}
OutputTimingSnapshot i2sTiming(){return {};}
}}
static void adapter() {
    auto ops=detail::codecOperations(Codec::LhdcV5);CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;c.bits_per_sample=24;void* state=nullptr;
    assert(ops->open(&state,c)==Status::Ok);uint8_t input[20]={9,0};int32_t pcm[1920];
    for(int m:{1,2,3,0,4,0}){mode=m;auto r=ops->decode(state,input,20,pcm,960);assert(r.status==(m==4?Status::CorruptFrame:Status::Ok));assert(flags(r,0)==unsigned(m==1?1:m==2?2:m==3?3:0));}
    mode=3;assert(flags(ops->decode(state,input,1,pcm,960),0)==0);assert(flags(ops->decode(state,input,20,pcm,1),0)==0);
    ops->close(state);assert(detail::memoryStats().current_bytes==0);state=nullptr;assert(ops->open(&state,c)==Status::Ok);mode=0;assert(flags(ops->decode(state,input,20,pcm,960),0)==0);ops->close(state);assert(detail::memoryStats().current_bytes==0);
    puts("LHDC_ADAPTER_CONCEALMENT_PASS");
}
static void frontend() {
    GigaBredr receiver;ReceiverConfig config;config.enable_i2s=false;config.maximum_packet_bytes=128;config.maximum_access_unit_bytes=128;config.queue_packets=1;
    assert(receiver.begin(config)==Status::Ok);CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=192000;c.bits_per_sample=24;
    assert(detail::callbacks.configure(c,detail::callbacks.user)==Status::Ok);detail::callbacks.stream(true,detail::callbacks.user);
    uint8_t p[34]={0x80};p[12]=1;p[14]=9; // RTP + media header + inner sequence + actual20-byte LHDC frame.
    std::atomic<bool> done{false};std::atomic<unsigned> snapshots{0};std::thread observer([&]{while(!done.load()){auto s=receiver.stats();assert(lhdc_right_concealed_frames(s,0)==960*lhdc_right_concealed_blocks(s,0));assert(lhdc_frame_muted_frames(s,0)==960*lhdc_frame_muted_blocks(s,0));++snapshots;}});
    while(!snapshots.load())std::this_thread::yield();
    for(int m:{1,2,3,0,4,0}){mode=m;++p[3];unsigned before=snapshots.load();detail::callbacks.media(p,sizeof p,detail::callbacks.user);receiver.poll();while(snapshots.load()==before)std::this_thread::yield();}
    done=true;observer.join();auto s=receiver.stats();assert(s.decoded_frames==4800 && s.decode_errors==1);
    assert(lhdc_right_concealed_blocks(s,0)==2 && lhdc_right_concealed_frames(s,0)==1920);
    assert(lhdc_frame_muted_blocks(s,0)==2 && lhdc_frame_muted_frames(s,0)==1920);
    detail::callbacks.stream(false,detail::callbacks.user);assert(lhdc_right_concealed_blocks(receiver.stats(),0)==2);
    assert(detail::callbacks.configure(c,detail::callbacks.user)==Status::Ok);assert(lhdc_frame_muted_blocks(receiver.stats(),0)==2);
    receiver.end();assert(detail::memoryStats().current_bytes==0);assert(lhdc_right_concealed_blocks(receiver.stats(),0)==0);
    assert(receiver.begin(config)==Status::Ok);assert(lhdc_right_concealed_blocks(receiver.stats(),0)==0 && lhdc_frame_muted_frames(receiver.stats(),0)==0);receiver.end();
    puts("LHDC_FRONTEND_CONCEALMENT_SNAPSHOT_LIFETIME_PASS");
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc<2 || !strcmp(argv[1],"vendor"))vendor();if(argc<2 || !strcmp(argv[1],"adapter"))adapter();if(argc<2 || !strcmp(argv[1],"frontend"))frontend();}

namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins&){return i2sBegin(rate,ms);}
}}
