#include "platform/GigaI2s.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
static unsigned checks=0;
#define CHECK(x)do{++checks;if(!(x)){std::fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
namespace giga_bredr {namespace detail {
struct DMA_HandleTypeDef {void(*XferHalfCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferCpltCallback)(DMA_HandleTypeDef*)=nullptr;void(*XferErrorCallback)(DMA_HandleTypeDef*)=nullptr;};
struct I2S_HandleTypeDef {};
enum:uint32_t{Stopped=0,Running=1,Priming=2};enum{HAL_OK=0,HAL_ERROR=1,DMA1_Stream7_IRQn=7};
static I2S_HandleTypeDef i2s;static DMA_HandleTypeDef dma;static I2sRing* ring=nullptr;static int32_t* dmaWords=nullptr;static uint32_t halfFrames=0,outputPhase=Stopped;static OutputStats stats;static PcmRecovery recovery;
static bool irq=false,fail_start=false;static unsigned starts=0,cleans=0,clears=0,errors=0;static size_t queued_at_start=0;static std::vector<int32_t> dma_at_start;static std::vector<unsigned> events;
static void clean(int32_t* data,uint32_t frames){CHECK(data==dmaWords&&frames==halfFrames*2);++cleans;events.push_back(1);}
static int HAL_I2S_Transmit_DMA(I2S_HandleTypeDef* device,uint16_t* data,uint32_t words){CHECK(device==&i2s&&data==reinterpret_cast<uint16_t*>(dmaWords)&&words==halfFrames*4&&!irq&&outputPhase==Priming);++starts;events.push_back(2);queued_at_start=ring->queued();dma_at_start.assign(dmaWords,dmaWords+halfFrames*4);return fail_start?HAL_ERROR:HAL_OK;}
static void NVIC_ClearPendingIRQ(unsigned irq_number){CHECK(irq_number==DMA1_Stream7_IRQn);++clears;events.push_back(3);}
static void NVIC_EnableIRQ(unsigned irq_number){CHECK(irq_number==DMA1_Stream7_IRQn&&outputPhase==Running);irq=true;events.push_back(4);}
static void halfDone(DMA_HandleTypeDef*){}static void fullDone(DMA_HandleTypeDef*){}
static void dmaError(DMA_HandleTypeDef* device){CHECK(device==&dma);++errors;++stats.dma_errors;irq=false;stats.running=stats.priming=false;outputPhase=Stopped;recovery.end(0,ring?uint32_t(ring->queued()):0);events.push_back(5);}
// The generated include contains the production i2sWrite function verbatim.
static uint64_t sourcePrepared=0; // Mock storage for verbatim production prime credit.
#include "i2s_write.inc"
}}
using namespace giga_bredr;using namespace giga_bredr::detail;
struct Fixture {
 std::vector<int32_t> memory,output;std::unique_ptr<I2sRing> value;uint32_t capacity,half;
 Fixture(uint32_t c,uint32_t h):memory(c*2,0),output(h*4,0x51515151),value(new I2sRing(memory.data(),c)),capacity(c),half(h){ring=value.get();dmaWords=output.data();halfFrames=h;outputPhase=Priming;stats={};stats.initialized=stats.priming=true;dma={};irq=fail_start=false;starts=cleans=clears=errors=0;queued_at_start=0;dma_at_start.clear();events.clear();CHECK(recovery.begin(1,c,h));}
 ~Fixture(){ring=nullptr;dmaWords=nullptr;outputPhase=Stopped;}
};
static std::vector<int32_t> pcm(unsigned frames,unsigned first=1){std::vector<int32_t> p(frames*2);for(unsigned i=0;i<frames;++i){p[2*i]=int32_t((first+i)*256);p[2*i+1]=-int32_t((first+i)*512);}return p;}
static void startup_red(){Fixture f(16,8);auto p=pcm(64);auto accepted=i2sWrite(p.data(),64);std::printf("PRIMING_STARTUP accepted=%zu ring_at_HAL=%zu ring_after=%zu dma_frames=%u\n",accepted,queued_at_start,ring->queued(),16u);CHECK(accepted==32&&queued_at_start==16&&ring->queued()==16);CHECK(dma_at_start==std::vector<int32_t>(p.begin(),p.begin()+32));CHECK(events==std::vector<unsigned>({1,2,3,4}));}
static void geometry_matrix(){
 for(uint32_t rate:{8000u,11025u,16000u,22050u,24000u,32000u,44100u,48000u,88200u,96000u,176400u,192000u})for(uint16_t ms:{uint16_t(2),uint16_t(3),uint16_t(20),uint16_t(100)}){
  I2sBufferGeometry geometry;CHECK(i2sBufferGeometry(rate,ms,geometry));Fixture f(geometry.ring_frames,geometry.half_frames);auto input=pcm(f.capacity+2*f.half+7);auto accepted=i2sWrite(input.data(),input.size()/2);CHECK(accepted==f.capacity+2*f.half&&queued_at_start==f.capacity&&ring->queued()==f.capacity&&starts==1&&cleans==1&&irq&&stats.running&&!stats.priming);CHECK(f.output==std::vector<int32_t>(input.begin(),input.begin()+4*f.half));std::vector<int32_t> queued(f.capacity*2);CHECK(ring->fill(queued.data(),f.capacity)==f.capacity);CHECK(queued==std::vector<int32_t>(input.begin()+4*f.half,input.begin()+2*accepted));CHECK(events==std::vector<unsigned>({1,2,3,4}));
 }
 puts("ALL12_RATES_2_3_20_100MS_STEREO_ORDER_AND_HAL_BOUNDARY_PASS");
}
static void partial_and_exact_source_bounds(){
 {Fixture f(16,2);auto first=pcm(10);CHECK(i2sWrite(first.data(),10)==10&&outputPhase==Priming&&!starts);auto next=pcm(30,11);CHECK(i2sWrite(next.data(),30)==10&&outputPhase==Running&&ring->queued()==16);auto all=first;all.insert(all.end(),next.begin(),next.begin()+20);CHECK(f.output==std::vector<int32_t>(all.begin(),all.begin()+8));std::vector<int32_t> queued(32);CHECK(ring->fill(queued.data(),16)==16&&queued==std::vector<int32_t>(all.begin()+8,all.end()));}
 {Fixture f(16,8);auto exact=pcm(23);CHECK(i2sWrite(exact.data(),23)==23&&ring->queued()==7&&queued_at_start==7);std::vector<int32_t> out(14);CHECK(ring->fill(out.data(),7)==7&&out==std::vector<int32_t>(exact.begin()+32,exact.end()));}
 {Fixture f(16,8);auto exact=pcm(16);CHECK(i2sWrite(exact.data(),16)==16&&!ring->queued());}
 {Fixture f(16,8);auto exact=pcm(15);CHECK(i2sWrite(exact.data(),15)==15&&outputPhase==Priming&&ring->queued()==15&&!starts);}
 puts("PRIOR_PRIMING_PARTIAL_LAST_CALL_EXACT_SIZE_SOURCE_BOUNDS_PASS");
}
static void null_zero_and_running(){
 {Fixture f(16,8);auto input=pcm(64);CHECK(!i2sWrite(nullptr,0)&&!i2sWrite(nullptr,64)&&!i2sWrite(input.data(),0)&&!ring->queued()&&!starts);outputPhase=Stopped;CHECK(!i2sWrite(input.data(),64)&&!ring->queued());outputPhase=Running;CHECK(i2sWrite(input.data(),64)==16&&ring->queued()==16&&!starts);CHECK(!i2sWrite(input.data(),64));CHECK(!i2sWrite(nullptr,64)&&!i2sWrite(input.data(),0)&&ring->queued()==16);std::vector<int32_t> out(16);CHECK(ring->fill(out.data(),8)==8);CHECK(i2sWrite(input.data()+32,48)==8&&ring->queued()==16&&!starts&&!cleans&&!irq);}
 // Null/zero calls preserve the old ability to prime an already-ready queue.
 {Fixture f(16,8);auto input=pcm(16);CHECK(ring->push(input.data(),16)==16);CHECK(!i2sWrite(nullptr,64)&&starts==1&&outputPhase==Running&&!ring->queued());}
 {Fixture f(16,8);auto input=pcm(16);CHECK(ring->push(input.data(),16)==16);CHECK(!i2sWrite(input.data(),0)&&starts==1&&outputPhase==Running&&!ring->queued());}
 puts("NULL_ZERO_STOPPED_RUNNING_SINGLE_PUSH_CONTRACT_PASS");
}
static void start_failure_and_seed(){
 {Fixture f(16,8);auto input=pcm(64);fail_start=true;CHECK(i2sWrite(input.data(),64)==32&&starts==1&&errors==1&&outputPhase==Stopped&&!irq&&ring->queued()==16&&!stats.running&&!stats.priming);CHECK(f.output==std::vector<int32_t>(input.begin(),input.begin()+32));std::vector<int32_t> queued(32);CHECK(ring->fill(queued.data(),16)==16&&queued==std::vector<int32_t>(input.begin()+32,input.begin()+64));CHECK(!i2sWrite(input.data(),64));CHECK(events==std::vector<unsigned>({1,2,5}));}
 {Fixture f(16,8);auto input=pcm(16);CHECK(i2sWrite(input.data(),16)==16);std::vector<int32_t> absent(16);CHECK(!recovery.refill(*ring,absent.data(),1000));auto s=recovery.snapshot();CHECK(s.entries==1&&s.ramp_frames==8);CHECK(absent[0]==input[30]*7/8&&absent[1]==input[31]*7/8);CHECK(dma_at_start==input);}
 puts("HAL_FAILURE_RETURNS_COPIED_COUNT_ORIGINAL_DMA_RECOVERY_SEED_PASS");
}
static void normal_word_preservation(){Fixture f(16,8);std::vector<int32_t> input(64);const int32_t pattern[]={INT32_MIN,INT32_MAX,0x12345600,int32_t(0xabcdef00u),-1,0,0x12345678,-0x12345678};for(unsigned i=0;i<input.size();++i)input[i]=pattern[i%8];CHECK(i2sWrite(input.data(),32)==32);CHECK(f.output==std::vector<int32_t>(input.begin(),input.begin()+32));std::vector<int32_t> queued(32);CHECK(ring->fill(queued.data(),16)==16&&queued==std::vector<int32_t>(input.begin()+32,input.end()));puts("PCM_WORD_EXTREMES_16_24_32BIT_PATTERNS_PRESERVED_PASS");}
int main(){setvbuf(stdout,nullptr,_IONBF,0);startup_red();geometry_matrix();partial_and_exact_source_bounds();null_zero_and_running();start_failure_and_seed();normal_word_preservation();std::printf("PLATFORM_PRIMING_REFILL_PASS checks=%u\n",checks);}
