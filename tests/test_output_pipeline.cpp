#include <atomic>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define private public
#include "GigaBredr.h"
#undef private
#include "GigaBredr.cpp"
#include "platform/I2sBuffer.h"
#include "platform/PcmRecovery.h"
using namespace giga_bredr;
namespace probe {void(*beforeLock)(void*)=nullptr;void(*sleepHook)(unsigned)=nullptr;}
static uint32_t tick=100;uint32_t micros(){return tick;}
static detail::RadioCallbacks callbacks;
static std::unique_ptr<detail::I2sRing> ring;static std::vector<int32_t> memory,dma_storage;
static detail::I2sBufferGeometry geometry;static bool priming=false,running=false;static unsigned writes=0,starved=0,consumed=0;
namespace giga_bredr {namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Aac)|codecBit(Codec::AacHe);}
static size_t decodedFrames;
static Status open(void**p,const CodecConfig&c){decodedFrames=c.codec==Codec::AacHe?2048:1024;*p=&decodedFrames;return Status::Ok;}
static void close(void*){}
static DecodeResult decode(void*,const uint8_t*,size_t n,int32_t*p,size_t cap){if(cap<decodedFrames)std::abort();for(size_t i=0;i<decodedFrames*2;++i)p[i]=1;return {Status::Ok,n,decodedFrames};}
const DecoderOps* codecOperations(Codec){static DecoderOps ops={open,decode,close};return &ops;}
Status radioBegin(const char*,uint32_t,const RadioCallbacks&c,const BleConfig&){callbacks=c;return Status::Ok;}Status radioBegin(const char*n,uint32_t m,const RadioCallbacks&c){return radioBegin(n,m,c,{});}void radioPoll(){}void radioEnd(){callbacks={};}RadioStats radioStats(){return{};}BleStats radioBleStats(){return{};}Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}bool radioBleReadWrite(BleWrite&){return false;}
struct DMA_HandleTypeDef {void(*XferHalfCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferErrorCallback)(DMA_HandleTypeDef*)=nullptr;};
struct I2S_HandleTypeDef {};enum:uint32_t{Stopped=0,Running=1,Priming=2};enum{HAL_OK=0,DMA1_Stream7_IRQn=7};
static I2S_HandleTypeDef i2s;static DMA_HandleTypeDef dma;static uint32_t outputPhase=Stopped,halfFrames=0;static int32_t* dmaWords=nullptr;static OutputStats stats;static PcmRecovery recovery;
static void clean(int32_t* p,uint32_t n){if(p!=dmaWords||n!=halfFrames*2)std::abort();}
static int HAL_I2S_Transmit_DMA(I2S_HandleTypeDef* p,uint16_t* data,uint32_t words){if(p!=&i2s||data!=reinterpret_cast<uint16_t*>(dmaWords)||words!=halfFrames*4||outputPhase!=Priming)std::abort();return HAL_OK;}
static void NVIC_ClearPendingIRQ(unsigned){}static void NVIC_EnableIRQ(unsigned){if(outputPhase!=Running)std::abort();::running=true;::priming=false;}
static void halfDone(DMA_HandleTypeDef*){}static void fullDone(DMA_HandleTypeDef*){}static void dmaError(DMA_HandleTypeDef*){stats.running=stats.priming=false;outputPhase=Stopped;::running=::priming=false;recovery.end(tick,ring?uint32_t(ring->queued()):0);}
Status i2sBegin(uint32_t rate,uint16_t ms){if(!i2sBufferGeometry(rate,ms,geometry))return Status::InvalidArgument;memory.assign(geometry.ring_frames*2,0);dma_storage.assign(geometry.half_frames*4,0);ring.reset(new I2sRing(memory.data(),geometry.ring_frames));halfFrames=geometry.half_frames;dmaWords=dma_storage.data();outputPhase=Priming;stats={};stats.initialized=stats.priming=true;stats.sample_rate=rate;recovery.begin(1,geometry.ring_frames,halfFrames);priming=true;running=false;return Status::Ok;}
Status i2sBegin(uint32_t rate){return i2sBegin(rate,20);}Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins&){return i2sBegin(rate,ms);}
// Only rename the symbol; the included platform function body is verbatim.
#define i2sWrite verbatimPlatformWrite
static uint64_t sourcePrepared=0; // Mock storage for verbatim production prime credit.
#include "i2s_write.inc"
#undef i2sWrite
size_t i2sWrite(const int32_t* p,size_t n){++writes;return verbatimPlatformWrite(p,n);}
void i2sEnd(){recovery.end(tick,ring?uint32_t(ring->queued()):0);running=priming=false;outputPhase=Stopped;stats={};ring.reset();dmaWords=nullptr;}
OutputStats i2sStats(){stats.queued_frames=ring?uint32_t(ring->queued()):0;return stats;}OutputTimingSnapshot i2sTiming(){return{};}PcmRecoverySnapshot i2sRecovery(){return recovery.snapshot();}

}}
static void sleep(unsigned ms){for(unsigned i=0;i<ms;++i){tick+=1000;if(running){auto n=detail::recovery.refill(*ring,dma_storage.data(),tick);consumed+=unsigned(n);if(n<geometry.half_frames)++starved;}}}
int main(int argc,char**argv){bool he=argc>1&&!strcmp(argv[1],"he");unsigned buffer=argc>2?unsigned(std::strtoul(argv[2],nullptr,10)):20;GigaBredr receiver;ReceiverConfig cfg;cfg.codecs=detail::compiledCodecs();cfg.queue_packets=2;cfg.startup_packets=1;cfg.pcm_buffer_ms=uint16_t(buffer);cfg.apply_remote_volume=false;if(receiver.begin(cfg)!=Status::Ok)return 2;CodecConfig c;c.codec=he?Codec::AacHe:Codec::Aac;c.sample_rate=argc>3?unsigned(std::strtoul(argv[3],nullptr,10)):(he?16000:8000);c.channels=2;c.bits_per_sample=16;c.aac_transport=AacTransport::Raw;if(callbacks.configure(c,callbacks.user)!=Status::Ok)return 2;callbacks.stream(true,callbacks.user);uint8_t packet[13]={0x80,0x60,0,1};packet[12]=0x44;probe::sleepHook=sleep;unsigned blocks=argc>4?unsigned(std::strtoul(argv[4],nullptr,10)):1;std::vector<unsigned> per_block;for(unsigned i=0;i<blocks;++i){const auto before=writes;packet[3]=uint8_t(i+1);callbacks.media(packet,sizeof packet,callbacks.user);receiver.impl_->processOne();per_block.push_back(writes-before);}auto dropped=receiver.impl_->counters.output_dropped_frames;printf("ACTUAL_FRONTEND_HEALTHY_CONSUMER codec=%s rate=%u decoded=%zu buffer_ms=%u elapsed_us=%u dropped=%u dma_starved_refills=%u accepted=%zu writes=%u\n",he?"HE":"LC",c.sample_rate,detail::decodedFrames,buffer,tick-100,dropped,starved,detail::decodedFrames*blocks-dropped,writes);printf("MODEL_WRITES_BY_BLOCK");for(auto n:per_block)printf(" %u",n);puts("");const auto maximum=unsigned((detail::decodedFrames+geometry.half_frames-1)/geometry.half_frames+1);bool bounded=true;for(size_t i=1;i<per_block.size();++i)if(per_block[i]>maximum)bounded=false;printf("MODEL_RUNNING_WRITE_CALL_BOUND max=%u pass=%u\n",maximum,unsigned(bounded));receiver.end();return (dropped||starved||!bounded)?3:0;}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}
