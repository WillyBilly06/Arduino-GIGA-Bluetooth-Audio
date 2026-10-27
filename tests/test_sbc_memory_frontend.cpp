#include "GigaBredr.cpp"
#include <cstdio>
#include <cstdlib>
using namespace giga_bredr;
namespace giga_bredr {namespace detail {
static RadioCallbacks callbacks;
static size_t writtenFrames;
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c){callbacks=c;return Status::Ok;}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig&){callbacks=c;return Status::Ok;}
void radioPoll(){}void radioEnd(){}RadioStats radioStats(){return {};}
BleStats radioBleStats(){return {};}Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}
bool radioBleReadWrite(BleWrite&){return false;}
Status i2sBegin(uint32_t){return Status::Ok;}Status i2sBegin(uint32_t,uint16_t){return Status::Ok;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Ok;}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
size_t i2sWrite(const int32_t* pcm,size_t n){
    for(size_t i=0;i<n;++i)if(pcm[2*i]!=pcm[2*i+1]){fputs("FAIL mono stereo expansion\n",stderr);abort();}
    writtenFrames+=n;return n;
}
void i2sEnd(){}OutputStats i2sStats(){OutputStats s;s.initialized=true;s.running=true;return s;}
OutputTimingSnapshot i2sTiming(){return {};}
}}
static void check(bool ok,const char* what){if(!ok){printf("FAIL %s\n",what);exit(3);}}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    CodecConfig c;c.codec_info_size=4;c.codec_info[0]=0x12;c.codec_info[1]=0x89;c.codec_info[2]=2;c.codec_info[3]=128;
    auto n=giga_bredr::frameCapacity(c);printf("FRONTEND_CAPACITY frames=%zu pcm_bytes=%zu\n",n,n*2*sizeof(int32_t));
    check(n==16,"production frontend4x4 capacity16 stereoPCM128");
    puts("SBC_FRONTEND_SELECTED_CAPACITY_PASS");
    for(unsigned blocks:{4u,8u,12u,16u})for(unsigned bands:{4u,8u}){
        c.codec_info[1]=(0x80>>(blocks/4-1))|(bands==8?4:8)|1;
        check(giga_bredr::frameCapacity(c)==blocks*bands,"frontend all selected capacities");
    }
    c.codec_info_size=0;check(giga_bredr::frameCapacity(c)==128,"frontend no-info fallback");
    c.codec_info_size=3;check(giga_bredr::frameCapacity(c)==0,"frontend malformed config");
    GigaBredr receiver;ReceiverConfig options;options.enable_i2s=false;options.queue_packets=1;
    options.maximum_packet_bytes=128;options.maximum_access_unit_bytes=128;
    check(receiver.begin(options)==Status::Ok,"real frontend begin");
    c.codec_info_size=4;c.codec_info[1]=0x15;
    check(giga_bredr::detail::callbacks.configure(c,giga_bredr::detail::callbacks.user)==Status::Ok,"real frontend max configure");
    auto maxTotal=receiver.stats().memory.current_bytes,maxDecoder=receiver.stats().decoder_memory_bytes;
    c.codec_info[1]=0x89;
    check(giga_bredr::detail::callbacks.configure(c,giga_bredr::detail::callbacks.user)==Status::Ok,"real frontend compact configure");
    auto smallTotal=receiver.stats().memory.current_bytes,smallDecoder=receiver.stats().decoder_memory_bytes;
    printf("FRONTEND_REAL_ALLOCATION decoder=%zu->%zu total=%zu->%zu\n",maxDecoder,smallDecoder,maxTotal,smallTotal);
    check(maxDecoder-smallDecoder==1344 && maxTotal-smallTotal==2240,"real frontend stereo PCM allocation reduction");
    receiver.end();check(giga_bredr::detail::memoryStats().current_bytes==0,"real frontend cleanup");
    puts("SBC_FRONTEND_ACTUAL_ALLOCATION_PASS");
    // Device boundary is stubbed; configuration, RTP/SBC assembly, real decoder
    // and in-place mono-to-stereo expansion are the production implementation.
    options.enable_i2s=true;
    check(receiver.begin(options)==Status::Ok,"mono frontend begin");
    c.channels=1;c.codec_info[0]=0x18;c.codec_info[1]=0x89;c.codec_info[3]=64;
    check(giga_bredr::detail::callbacks.configure(c,giga_bredr::detail::callbacks.user)==Status::Ok,"mono compact configure");
    giga_bredr::detail::callbacks.stream(true,giga_bredr::detail::callbacks.user);
    uint8_t packet[20]={0x80};packet[12]=1;packet[13]=0x9c;packet[14]=0xc0;packet[15]=2;
    uint8_t crc=0x0f;
    for(unsigned i=0;i<32;++i){unsigned bit=i<16?((uint16_t(0xc002)>>(15-i))&1):0;unsigned carry=(crc>>7)^bit;crc=uint8_t(crc<<1);if(carry)crc^=0x1d;}
    packet[16]=crc;size_t steady=receiver.stats().memory.current_bytes;
    giga_bredr::detail::callbacks.media(packet,sizeof(packet),giga_bredr::detail::callbacks.user);receiver.poll();
    check(receiver.stats().decoded_frames==16 && giga_bredr::detail::writtenFrames==16,"real compact mono pipeline frames");
    check(receiver.stats().memory.current_bytes==steady,"real compact mono pipeline growth");
    receiver.end();check(giga_bredr::detail::memoryStats().current_bytes==0,"mono frontend cleanup");
    puts("SBC_FRONTEND_MONO_STEREO_STRIDE_PASS");
}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}

// New AVRCP facade boundary is unavailable in this pre-existing hardware stub.
namespace giga_bredr { namespace detail {
AvrcpStatus radioAvrcpStatus(){return{};}
AvrcpMetadata radioAvrcpMetadata(){return{};}
Status radioAvrcpCommand(AvrcpCommand,uint32_t,uint32_t&){return Status::Unsupported;}
Status radioAvrcpCancel(uint32_t,uint32_t){return Status::Unsupported;}
}}
