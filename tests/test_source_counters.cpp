#include "platform/GigaI2s.h"
#include "platform/I2sBuffer.h"
#include "platform/AudioSupplyRuntime.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <memory>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"COUNTER line%d %s\n",__LINE__,#x);std::abort();}}while(0)
namespace giga_bredr { namespace detail {
struct DMA_HandleTypeDef{void(*XferHalfCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferErrorCallback)(DMA_HandleTypeDef*)=nullptr;};
struct I2S_HandleTypeDef{};enum:uint32_t{Stopped,Running,Priming};enum{HAL_OK,HAL_ERROR,DMA1_Stream7_IRQn=7};
static I2S_HandleTypeDef i2s;static DMA_HandleTypeDef dma;static I2sRing*ring=nullptr;static int32_t*dmaWords=nullptr;
static uint32_t halfFrames=0,outputPhase=Stopped,ringCapacity=0,timingEpoch=0,tick=0;static uint64_t sourcePrepared=0,sourceMissing=0;
static uint32_t primeFrames=0; // Legacy-zero watermark for the verbatim i2sWrite mock.
static OutputStats stats;static OutputTiming timing;static PcmRecovery recovery;static bool masked=false,fail=false;
struct Critical{bool prior;Critical():prior(masked){masked=true;}~Critical(){masked=prior;}};
static uint32_t micros(){return tick+=1000;}
static void clean(int32_t*p,uint32_t n){CHECK(p>=dmaWords&&p+n*2<=dmaWords+halfFrames*4);}
static int HAL_I2S_Transmit_DMA(I2S_HandleTypeDef*,uint16_t*,uint32_t){return fail?HAL_ERROR:HAL_OK;}
static void NVIC_ClearPendingIRQ(unsigned){}static void NVIC_EnableIRQ(unsigned){}
static void halfDone(DMA_HandleTypeDef*){}static void fullDone(DMA_HandleTypeDef*){}
static void dmaError(DMA_HandleTypeDef*){outputPhase=Stopped;stats.running=stats.priming=false;++stats.dma_errors;recovery.end(tick,uint32_t(ring->queued()));}
#include "source-functions.inc"
}}
using namespace giga_bredr;using namespace giga_bredr::detail;
struct Fixture{
 std::vector<int32_t> memory,output;std::unique_ptr<I2sRing> queue;
 Fixture(uint32_t rate,uint16_t ms){I2sBufferGeometry g;CHECK(i2sBufferGeometry(rate,ms,g));halfFrames=g.half_frames;primeFrames=0;ringCapacity=g.ring_frames;
  memory.assign(ringCapacity*2,0);output.assign(halfFrames*4,0);queue.reset(new I2sRing(memory.data(),ringCapacity));ring=queue.get();dmaWords=output.data();
  ++timingEpoch;stats={};stats.sample_rate=rate;stats.initialized=stats.priming=true;sourcePrepared=sourceMissing=0;fail=false;outputPhase=Priming;timing.beginEpoch(timingEpoch);CHECK(recovery.begin(timingEpoch,ringCapacity,halfFrames));}
 ~Fixture(){ring=nullptr;dmaWords=nullptr;outputPhase=Stopped;}
};
static std::vector<int32_t> pcm(unsigned n){std::vector<int32_t>out(n*2);for(unsigned i=0;i<n;++i){out[2*i]=int32_t(i+1);out[2*i+1]=-int32_t(i+1);}return out;}
int main(){
 for(uint32_t rate:{8000u,16000u,44100u,48000u,96000u,192000u})for(uint16_t ms:{uint16_t(2),uint16_t(20)}){
  Fixture f(rate,ms);auto input=pcm(ringCapacity+halfFrames*2+7);auto epoch=timingEpoch;
  CHECK(i2sSourceSnapshot().prepared==0&&i2sSourceSnapshot().missing==0);
  CHECK(i2sWrite(input.data(),input.size()/2)==ringCapacity+halfFrames*2);
  auto s=i2sSourceSnapshot();CHECK(s.prepared==halfFrames*2&&s.missing==0&&s.output_epoch==epoch&&s.sample_rate==rate&&s.ring_capacity==ringCapacity&&s.half_frames==halfFrames&&s.running&&s.initialized&&!s.recovering&&!masked);
  uint32_t remaining=ringCapacity;while(remaining){auto real=remaining<halfFrames?remaining:halfFrames;refill(0);remaining-=real;}
  s=i2sSourceSnapshot();CHECK(s.prepared==uint64_t(halfFrames)*2+ringCapacity);auto p=s.prepared,z=s.missing;
  refill(1);s=i2sSourceSnapshot();CHECK(s.prepared==p&&s.missing==z+halfFrames&&s.recovering);
  auto a=pcm(halfFrames);CHECK(ring->push(a.data(),halfFrames)==halfFrames);refill(0);s=i2sSourceSnapshot();CHECK(s.prepared==p&&s.missing==z+2*halfFrames&&s.recovering);
  auto rest=pcm(ringCapacity);CHECK(ring->push(rest.data(),ringCapacity)==ringCapacity-halfFrames);refill(1);s=i2sSourceSnapshot();CHECK(s.prepared==p+halfFrames&&s.missing==z+2*halfFrames&&!s.recovering);
  outputPhase=Stopped;refill(0);CHECK(i2sSourceSnapshot().prepared==s.prepared&&i2sSourceSnapshot().missing==s.missing);
 }
 {Fixture f(8000,2);auto p=pcm(32);fail=true;CHECK(i2sWrite(p.data(),32)==32);auto s=i2sSourceSnapshot();CHECK(s.prepared==16&&s.missing==0&&!s.running);CHECK(i2sWrite(p.data(),32)==0&&i2sSourceSnapshot().prepared==16);}
 {Fixture f(8000,2);auto p=pcm(32);CHECK(i2sWrite(p.data(),32)==32);sourcePrepared=UINT32_MAX-3ull;refill(0);CHECK(i2sSourceSnapshot().prepared==uint64_t(UINT32_MAX)-3+halfFrames);}
 puts("VERBATIM_PRIME_REFILL_SNAPSHOT_P_INCLUDES_BOTH_HALVES_Z_INCLUDES_HOLDS_64BIT_PASS");std::printf("SOURCE_COUNTER_PASS checks=%u\n",checks);
}
