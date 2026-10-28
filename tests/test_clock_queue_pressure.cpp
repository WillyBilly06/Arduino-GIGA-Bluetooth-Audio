// Actual shared frontend/ring/recovery/write, synthetic LHDC decode and DMA.
// Ordered input must not disappear locally merely because PCM never starves.
#include <atomic>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#define private public
#include "GigaBredr.h"
#undef private
#include "GigaBredr.cpp"
#include "platform/I2sBuffer.h"
#include "platform/PcmRecovery.h"
using namespace giga_bredr;
namespace probe {void(*beforeLock)(void*)=nullptr;void(*sleepHook)(unsigned)=nullptr;}
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"QUEUE_PRESSURE line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static constexpr uint32_t rate=192000,packetFrames=960;
static uint64_t nowFp=100*65536ULL,originFp=nowFp,nextSource=nowFp,nextDma=UINT64_MAX;
static uint64_t sourceFrames=0,leadFrames=0,prepared=0,missing=0;
static uint32_t packets=0,decoded=0,verified=0,identityBad=0,epoch=0,actuatorCalls=0,queueHigh=0;
static uint32_t batchUs=35000,decodeUs=4500;static bool seeded=false;
static int frac=0,baseFrac=0;static uint64_t baseCoefficient=0;static int32_t applied=0;
static detail::RadioCallbacks callbacks;static detail::I2sClockTrimSnapshot backend;
static std::unique_ptr<detail::I2sRing> ring;static std::vector<int32_t> pcmMemory,dmaMemory;
static detail::I2sBufferGeometry geometry;
static void advance(uint64_t target);
uint32_t micros(){return uint32_t(nowFp/65536);}
static int32_t word(uint32_t index){return int32_t(index*2654435761u+0x12345u);}
static void verify(const int32_t* p,uint32_t frames){for(uint32_t i=0;i<frames;++i){const int32_t v=word(verified++);if(p[2*i]!=v||p[2*i+1]!=~v)++identityBad;}}
static uint64_t dmaPeriod(){detail::I2sClock c;CHECK(detail::i2sClock(rate,c));
 const long double actual=(4000000.L/8/8192)*(baseCoefficient+frac-baseFrac)/(c.kernel_hz/(64.L*rate))/64;
 return uint64_t(std::llround(geometry.half_frames*1000000.L*65536/actual));}
namespace giga_bredr {namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::LhdcV5);}
static Status open(void** p,const CodecConfig& c){CHECK(c.codec==Codec::LhdcV5&&c.sample_rate==rate);*p=&decoded;return Status::Ok;}
static void close(void*){}
static DecodeResult decode(void*,const uint8_t*,size_t bytes,int32_t* p,size_t capacity){
 if(decodeUs)advance(nowFp+uint64_t(decodeUs)*65536);CHECK(bytes==20&&capacity>=packetFrames);
 for(unsigned i=0;i<packetFrames;++i){const int32_t v=word(decoded++);p[2*i]=v;p[2*i+1]=~v;}return {Status::Ok,bytes,packetFrames};}
const DecoderOps* codecOperations(Codec){static DecoderOps ops={open,decode,close};return &ops;}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig&){callbacks=c;return Status::Ok;}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}void radioPoll(){}void radioEnd(){callbacks={};}
RadioStats radioStats(){return{};}BleStats radioBleStats(){return{};}Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}bool radioBleReadWrite(BleWrite&){return false;}
Status radioBleDisconnect(uint32_t,bool){CHECK(false);return Status::Unsupported;}Status radioBleResumeAdvertising(){CHECK(false);return Status::Unsupported;}
struct DMA_HandleTypeDef {void(*XferHalfCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferErrorCallback)(DMA_HandleTypeDef*)=nullptr;};
struct I2S_HandleTypeDef {};enum:uint32_t{Stopped=0,Running=1,Priming=2};enum{HAL_OK=0,DMA1_Stream7_IRQn=7};
static I2S_HandleTypeDef i2s;static DMA_HandleTypeDef dma;static uint32_t outputPhase=Stopped,halfFrames=0;static int32_t* dmaWords=nullptr;
static uint32_t primeFrames=0; // Legacy-zero watermark for the verbatim i2sWrite mock.
static OutputStats stats;static PcmRecovery recovery;static uint64_t& sourcePrepared=::prepared;
static void clean(int32_t* p,uint32_t n){CHECK(p==dmaWords&&n==halfFrames*2);verify(p,n);}
static int HAL_I2S_Transmit_DMA(I2S_HandleTypeDef* p,uint16_t* data,uint32_t n){CHECK(p==&i2s&&data==reinterpret_cast<uint16_t*>(dmaWords)&&n==halfFrames*4&&outputPhase==Priming);return HAL_OK;}
static void NVIC_ClearPendingIRQ(unsigned){}static void NVIC_EnableIRQ(unsigned){CHECK(outputPhase==Running);nextDma=nowFp+dmaPeriod();}
static void halfDone(DMA_HandleTypeDef*){}static void fullDone(DMA_HandleTypeDef*){}static void dmaError(DMA_HandleTypeDef*){CHECK(false);}
Status i2sBegin(uint32_t r,uint16_t ms){CHECK(r==rate&&i2sBufferGeometry(r,ms,geometry));pcmMemory.assign(geometry.ring_frames*2,0);dmaMemory.assign(geometry.half_frames*4,0);
 ring.reset(new I2sRing(pcmMemory.data(),geometry.ring_frames));halfFrames=geometry.half_frames;primeFrames=0;dmaWords=dmaMemory.data();outputPhase=Priming;stats={};stats.initialized=stats.priming=true;stats.sample_rate=r;
 CHECK(recovery.begin(++epoch,geometry.ring_frames,halfFrames));I2sClock c;CHECK(i2sClock(r,c));baseFrac=frac=int(c.pll_frac);baseCoefficient=uint64_t(c.pll_n)*8192+c.pll_frac;backend={};backend.acquired=true;return Status::Ok;}
Status i2sBegin(uint32_t r){return i2sBegin(r,20);}Status i2sBegin(uint32_t r,uint16_t ms,const I2sPins&){return i2sBegin(r,ms);}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
// Generated by test_audio_clock.py from the current platform function, verbatim.
#include "i2s_write.inc"
void i2sEnd(){recovery.end(micros(),ring?uint32_t(ring->queued()):0);outputPhase=Stopped;stats={};ring.reset();dmaWords=nullptr;nextDma=UINT64_MAX;}
OutputStats i2sStats(){stats.queued_frames=ring?uint32_t(ring->queued()):0;return stats;}OutputTimingSnapshot i2sTiming(){return{};}PcmRecoverySnapshot i2sRecovery(){return recovery.snapshot();}
OutputSourceSnapshot i2sSourceSnapshot(){OutputSourceSnapshot s;s.output_epoch=epoch;s.sample_rate=rate;s.ring_capacity=geometry.ring_frames;s.half_frames=geometry.half_frames;s.prepared=prepared;s.missing=missing;s.initialized=stats.initialized;s.running=stats.running;s.recovering=recovery.snapshot().recovering;return s;}
I2sClockTrimSnapshot i2sClockTrimStats(){return backend;}
ClockTrimResult i2sClockTrim(uint32_t expected,int32_t request,int32_t lower,int32_t upper){
 CHECK(expected==epoch);++actuatorCalls;++backend.attempts;const auto q=i2sClockQuantize(uint16_t(baseCoefficient/8192),uint16_t(baseFrac),request,lower,upper);
 CHECK(q.representable&&q.applied_q16>=lower&&q.applied_q16<=upper&&std::abs(q.applied_q16-applied)<=10*65536);
 const uint64_t priorPeriod=dmaPeriod();ClockTrimResult r;r.output_epoch=epoch;r.requested_q16=request;r.status=q.fraction==frac?ClockTrimCode::Unchanged:ClockTrimCode::Applied;
 frac=q.fraction;applied=q.applied_q16;if(nextDma!=UINT64_MAX&&nextDma>nowFp)nextDma=nowFp+uint64_t((long double)(nextDma-nowFp)*dmaPeriod()/priorPeriod);
 r.accepted=true;r.applied_q16=applied;r.fraction=r.readback_fraction=uint16_t(frac);backend.last=r;++backend.accepted;return r;
}
}}
static void inject(){
 uint8_t packet[34]={0x80,0x60};const uint32_t timestamp=uint32_t(sourceFrames);packet[2]=uint8_t(packets>>8);packet[3]=uint8_t(packets);
 packet[4]=uint8_t(timestamp>>24);packet[5]=uint8_t(timestamp>>16);packet[6]=uint8_t(timestamp>>8);packet[7]=uint8_t(timestamp);packet[11]=1;
 packet[12]=4;packet[13]=uint8_t(packets);packet[14]=9;callbacks.media(packet,sizeof packet,callbacks.user);
 queueHigh=std::max(queueHigh,uint32_t(static_cast<GigaBredr::Impl*>(callbacks.user)->queue.count()));++packets;sourceFrames+=packetFrames;
 uint64_t when=(sourceFrames-leadFrames)*1000000/rate;if(seeded&&when>=20000000)when=((when+batchUs-1)/batchUs)*batchUs;nextSource=originFp+when*65536;
}
static void advance(uint64_t target){CHECK(target>=nowFp);
 while(std::min(nextSource,nextDma)<=target){nowFp=std::min(nextSource,nextDma);
  if(nextDma==nowFp){const auto got=detail::recovery.refill(*ring,dmaMemory.data(),micros());verify(dmaMemory.data(),uint32_t(got));prepared+=got;missing+=geometry.half_frames-got;nextDma+=dmaPeriod();}
  if(nextSource==nowFp)inject();
 }nowFp=target;
}
static void sleep(unsigned ms){advance(nowFp+uint64_t(ms)*1000*65536);}
int main(int argc,char** argv){batchUs=argc>1?unsigned(std::atoi(argv[1])):35000;const bool enabled=argc<=2||std::atoi(argv[2])!=0;CHECK(batchUs==35000||batchUs==70000);decodeUs=batchUs==35000?4500:0;
 GigaBredr receiver;ReceiverConfig cfg;cfg.clock.enabled=enabled;cfg.codecs=detail::compiledCodecs();cfg.queue_packets=32;cfg.startup_packets=26;cfg.maximum_packet_bytes=2048;cfg.pcm_buffer_ms=20;cfg.apply_remote_volume=false;
 CHECK(receiver.begin(cfg)==Status::Ok);CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=rate;c.channels=2;c.bits_per_sample=24;c.frame_duration_us=5000;CHECK(callbacks.configure(c,callbacks.user)==Status::Ok);callbacks.stream(true,callbacks.user);probe::sleepHook=sleep;
 for(unsigned k=0;k<32;++k)inject();leadFrames=sourceFrames-packetFrames;seeded=true;nextSource=originFp+5000*65536ULL;
 const uint64_t finish=originFp+120000000*65536ULL;uint64_t batches=0,calls=0;detail::setAllocationFailureCountdown(0);
 while(nowFp<finish){if(!receiver.impl_->wake.try_acquire_for(0)){const uint64_t deadline=std::min<uint64_t>(finish,nowFp+10000*65536ULL);while(nowFp<deadline){advance(std::min(deadline,std::min(nextSource,nextDma)));if(receiver.impl_->wake.try_acquire_for(0))break;}}
  if(nowFp>=finish)break;++batches;for(unsigned work=0;work<64&&nowFp<finish;++work){++calls;receiver.impl_->processOne();}}
 auto& i=*receiver.impl_;const auto rt=i.rtp_timing.snapshot();const auto qt=i.queue_timing.snapshot();const auto recovery=detail::recovery.snapshot();const auto control=i.clock_control.snapshot();
 std::printf("ACTUAL_CLOCK_QUEUE batch_us=%u decode_us=%u enabled=%u input_packets=%u RTP=%llu TS=%llu inner=%llu full=%llu drop=%u encoded_missing=%lld PCM_missing=%llu recovery=%u identity=%u high=%u target=%llu checks=%u\n",batchUs,decodeUs,unsigned(enabled),packets,(unsigned long long)rt.forward_gaps,(unsigned long long)rt.timestamp_discontinuities,(unsigned long long)rt.inner_sequence_discontinuities,(unsigned long long)qt.full,i.dropped.load(),(long long)sourceFrames-decoded-(long long)i.queue.count()*packetFrames,(unsigned long long)missing,recovery.entries,identityBad,queueHigh,(unsigned long long)control.target_frames,checks);std::fflush(stdout);
 CHECK(rt.packets==packets&&!rt.forward_gaps&&!rt.missing_sequences&&!rt.duplicates&&!rt.backward&&!rt.timestamp_discontinuities&&!rt.inner_sequence_discontinuities&&!rt.clock_discontinuities&&!rt.source_changes&&!rt.unknown_payloads&&!rt.malformed_payloads);
 CHECK(qt.full==i.dropped.load());CHECK(!qt.full&&!i.dropped.load());
 CHECK(sourceFrames==decoded+uint64_t(i.queue.count())*packetFrames);CHECK(decoded==prepared+ring->queued());
 CHECK(!missing&&!recovery.entries&&!recovery.resumes&&!identityBad&&!i.counters.decode_errors&&!i.counters.output_dropped_frames&&!detail::memoryStats().failures);
 CHECK(calls<=batches*64&&actuatorCalls<=121);if(!enabled)CHECK(!actuatorCalls&&!i.counters.clock.observations);receiver.end();return 0;
}

// New AVRCP facade boundary is unavailable in this pre-existing hardware stub.
namespace giga_bredr { namespace detail {
AvrcpStatus radioAvrcpStatus(){return{};}
AvrcpMetadata radioAvrcpMetadata(){return{};}
Status radioAvrcpCommand(AvrcpCommand,uint32_t,uint32_t&){return Status::Unsupported;}
Status radioAvrcpCancel(uint32_t,uint32_t){return Status::Unsupported;}
}}
