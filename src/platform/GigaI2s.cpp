#include "GigaI2s.h"
#include "I2sBuffer.h"
#include "I2sPins.h"
#include "SchedulerWitness.h"
#include "AudioSupplyRuntime.h"
#include "../detail/Memory.h"
#if defined(ARDUINO_GIGA) && defined(CORE_CM7)
#include <Arduino.h>
#include <mbed.h>
#include "stm32h7xx_hal.h"
#include <new>
#ifndef GIGA_BREDR_I2S_BUFFER_MS
#define GIGA_BREDR_I2S_BUFFER_MS 20
#endif
static_assert(GIGA_BREDR_I2S_BUFFER_MS >= 2 && GIGA_BREDR_I2S_BUFFER_MS <= 320,"I2S ring capacity must be 2..320 ms");

namespace giga_bredr { namespace detail {
namespace {
I2S_HandleTypeDef i2s;
DMA_HandleTypeDef dma;
OutputStats stats;
OutputTiming timing;
PcmRecovery recovery;
uint32_t timingEpoch=0;
I2sRing* ring=nullptr;
void* arena=nullptr;
int32_t* dmaWords=nullptr;
uint32_t halfFrames=0;
uint32_t ringCapacity=0;
uint32_t primeFrames=0;
uint64_t sourcePrepared=0,sourceMissing=0;
I2sClockTrimState clockTrim;
ClockTrimResult clockTrimLast;
bool trimRequested=false,trimCaptured=false,trimRelinquished=false;
bool owned=false,pllOwned=false,dmaReady=false;
enum : uint32_t { Stopped=0, Running=1, Priming=2 };
uint32_t outputPhase=Stopped;
uint32_t oldVector=0,oldPriority=0,oldSpiClock=0,oldPllDiv=0,oldPllFrac=0,oldPllCfg=0,oldPllM=0;
I2sRoutePlan pinPlan;
I2sGpioSnapshot oldPins[4];
GPIO_TypeDef* gpioPort(uint8_t port) {
  switch(port){case 0:return GPIOA;case 1:return GPIOB;case 2:return GPIOC;case 3:return GPIOD;case 6:return GPIOG;case 8:return GPIOI;default:return nullptr;}
}
void gpioClock(uint8_t port) {
  switch(port){case 0:__HAL_RCC_GPIOA_CLK_ENABLE();break;case 1:__HAL_RCC_GPIOB_CLK_ENABLE();break;case 2:__HAL_RCC_GPIOC_CLK_ENABLE();break;case 3:__HAL_RCC_GPIOD_CLK_ENABLE();break;case 6:__HAL_RCC_GPIOG_CLK_ENABLE();break;case 8:__HAL_RCC_GPIOI_CLK_ENABLE();break;}
}
SPI_TypeDef* spiInstance(uint8_t id){return id==1?SPI1:id==2?SPI2:SPI3;}
void spiClockEnable(){switch(pinPlan.peripheral){case 1:__HAL_RCC_SPI1_CLK_ENABLE();break;case 2:__HAL_RCC_SPI2_CLK_ENABLE();break;case 3:__HAL_RCC_SPI3_CLK_ENABLE();break;}}
void spiReset(){switch(pinPlan.peripheral){case 1:__HAL_RCC_SPI1_FORCE_RESET();__HAL_RCC_SPI1_RELEASE_RESET();break;case 2:__HAL_RCC_SPI2_FORCE_RESET();__HAL_RCC_SPI2_RELEASE_RESET();break;case 3:__HAL_RCC_SPI3_FORCE_RESET();__HAL_RCC_SPI3_RELEASE_RESET();break;}}
void spiClockDisable(){switch(pinPlan.peripheral){case 1:__HAL_RCC_SPI1_CLK_DISABLE();break;case 2:__HAL_RCC_SPI2_CLK_DISABLE();break;case 3:__HAL_RCC_SPI3_CLK_DISABLE();break;}}
I2sGpioRegisters gpioRegisters(GPIO_TypeDef* gpio) {
  return {gpio->MODER,gpio->OTYPER,gpio->OSPEEDR,gpio->PUPDR,gpio->AFR[0],gpio->AFR[1]};
}
constexpr uint32_t pllMask=RCC_PLLCFGR_PLL2FRACEN|RCC_PLLCFGR_PLL2VCOSEL|RCC_PLLCFGR_PLL2RGE|RCC_PLLCFGR_DIVP2EN|RCC_PLLCFGR_DIVQ2EN|RCC_PLLCFGR_DIVR2EN;
struct Critical { uint32_t state; Critical():state(__get_PRIMASK()) { __disable_irq(); __DMB(); } ~Critical() { __DMB(); __set_PRIMASK(state); } };
static_assert(RCC_PLLCFGR_PLL2FRACEN==I2sClockMasks::FracEnable&&RCC_PLL2FRACR_FRACN2==I2sClockMasks::Fraction,"H747 trim masks");
static_assert(RCC_PLLCFGR_DIVP2EN==I2sClockMasks::PEnable&&pllMask==I2sClockMasks::Pll2Config,"H747 PLL2 ownership mask");
static_assert((RCC_CR_HSEON|RCC_CR_HSERDY|RCC_CR_PLL2ON|RCC_CR_PLL2RDY)==I2sClockMasks::RequiredCr,"H747 lock/source mask");
static_assert((RCC_PLLCKSELR_PLLSRC|RCC_PLLCKSELR_DIVM2)==I2sClockMasks::SourceAndM&&RCC_D2CCIP1R_SPI123SEL==I2sClockMasks::SpiMux,"H747 source/mux masks");
static_assert(RCC_PLLSOURCE_HSE==2u&&RCC_PLL2VCIRANGE_2==0x80u&&RCC_PLL2VCOWIDE==0u&&RCC_SPI123CLKSOURCE_PLL2==0x1000u,"H747 supported clock profile");
static_assert(RCC_D1CCIPR_FMCSEL_Pos==0&&RCC_D1CCIPR_QSPISEL_Pos==4&&RCC_D1CCIPR_DSISEL_Pos==8&&RCC_D1CCIPR_SDMMCSEL_Pos==16,"H747 D1 consumers");
static_assert(RCC_D2CCIP1R_SAI1SEL_Pos==0&&RCC_D2CCIP1R_SAI23SEL_Pos==6&&RCC_D2CCIP1R_SPI45SEL_Pos==16&&RCC_D2CCIP1R_SPDIFSEL_Pos==20&&RCC_D2CCIP1R_FDCANSEL_Pos==28,"H747 D2 consumers");
static_assert(RCC_D2CCIP2R_USART16SEL_Pos==3&&RCC_D2CCIP2R_USART28SEL_Pos==0&&RCC_D2CCIP2R_LPTIM1SEL_Pos==28,"H747 UART/LPTIM consumers");
static_assert(RCC_D3CCIPR_LPUART1SEL_Pos==0&&RCC_D3CCIPR_SPI6SEL_Pos==28&&RCC_D3CCIPR_SAI4ASEL_Pos==21&&RCC_D3CCIPR_SAI4BSEL_Pos==24&&RCC_D3CCIPR_ADCSEL_Pos==16&&RCC_D3CCIPR_LPTIM2SEL_Pos==10&&RCC_D3CCIPR_LPTIM345SEL_Pos==13,"H747 D3 consumers");
I2sClockRegisters clockRegisters(){
 I2sClockRegisters r;r.output_epoch=timingEpoch;r.core_hz=SystemCoreClock;r.reference_hz=HSE_VALUE==16000000u?4000000u:0;
 r.owned=owned&&pllOwned&&i2s.Instance==spiInstance(pinPlan.peripheral);
 r.running=stats.initialized&&stats.running&&__atomic_load_n(&outputPhase,__ATOMIC_ACQUIRE)==Running&&
     i2s.Instance&&(i2s.Instance->CR1&SPI_CR1_SPE)&&(i2s.Instance->CFG1&SPI_CFG1_TXDMAEN);
 r.interrupts_enabled=!__get_PRIMASK()&&!__get_BASEPRI()&&!__get_FAULTMASK()&&!__get_IPSR();
 r.cr=RCC->CR;r.pllckselr=RCC->PLLCKSELR;r.pllcfgr=RCC->PLLCFGR;r.pll2divr=RCC->PLL2DIVR;r.pll2fracr=RCC->PLL2FRACR;
 r.spi_mux=RCC->D2CCIP1R&RCC_D2CCIP1R_SPI123SEL;r.instance=pinPlan.peripheral;
 I2sClockConsumerRegisters consumers;
 consumers.mux[0]=RCC->D1CCIPR;consumers.mux[1]=RCC->D2CCIP1R;consumers.mux[2]=RCC->D2CCIP2R;consumers.mux[3]=RCC->D3CCIPR;
 consumers.enable[0]=RCC->AHB1ENR;consumers.enable[1]=RCC->AHB2ENR;consumers.enable[2]=RCC->AHB3ENR;consumers.enable[3]=RCC->AHB4ENR;
 consumers.enable[4]=RCC->APB1LENR;consumers.enable[5]=RCC->APB1HENR;consumers.enable[6]=RCC->APB2ENR;consumers.enable[7]=RCC->APB3ENR;consumers.enable[8]=RCC->APB4ENR;
 r.spi_clock_enabled=pinPlan.peripheral==1?(consumers.enable[6]&RCC_APB2ENR_SPI1EN):pinPlan.peripheral==2?(consumers.enable[4]&RCC_APB1LENR_SPI2EN):(consumers.enable[4]&RCC_APB1LENR_SPI3EN);
 // Do not access a gated peripheral. After our stop its I2S word is irrelevant
 // to PLL ownership; the stopped release guard instead requires its gate off.
 r.spi_i2scfgr=r.spi_clock_enabled&&i2s.Instance?i2s.Instance->I2SCFGR:0;
 r.foreign_consumer=i2sClockForeignConsumer(consumers,pinPlan.peripheral);
 i2sClockConsumerFingerprint(consumers,pinPlan.peripheral,r.foreign);
 // CPU-clock spectator fields catch a same-core clock-tree change. Tracking
 // never writes these words and its lower bound uses the supported480MHz max.
 r.foreign[13]=RCC->CFGR;r.foreign[14]=RCC->D1CFGR;
 r.foreign[15]=RCC->PLL1DIVR;r.foreign[16]=RCC->PLL1FRACR;
 r.foreign[17]=RCC->PLLCFGR&(RCC_PLLCFGR_PLL1FRACEN|RCC_PLLCFGR_PLL1VCOSEL|RCC_PLLCFGR_PLL1RGE|RCC_PLLCFGR_DIVP1EN);
 r.foreign[18]=RCC->PLLCKSELR&RCC_PLLCKSELR_DIVM1;r.foreign[19]=RCC->CR&(RCC_CR_PLL1ON|RCC_CR_PLL1RDY);
 return r;
}
bool clockPrioritySupported(bool stopped=false){
 return i2sClockPrioritySupported(NVIC_GetPriorityGrouping(),__NVIC_PRIO_BITS,
   NVIC_GetPriority(DMA1_Stream7_IRQn),NVIC_GetPriority(UART7_IRQn),
   NVIC_GetPriority(PendSV_IRQn),stopped&&!NVIC_GetEnableIRQ(DMA1_Stream7_IRQn));
}
struct ClockTrimIo {
 // Disabled for ClockSelectiveGuard and every Io except this apply call.
 bool restore_trace=false,restore_counter_valid=false;
 uint8_t restore_seen=0,restore_pending=0;
 uint32_t restore_mark=0,restore_icsr=0,restore_basepri=0;
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
 uint32_t scheduler_host_before=0;
#endif
 I2sClockRegisters snapshot(){return clockRegisters();}
 bool interruptsEnabled(){return !__get_PRIMASK()&&!__get_BASEPRI()&&!__get_FAULTMASK()&&!__get_IPSR();}
 bool clockGuardContextValid(){return !__get_PRIMASK()&&__get_BASEPRI()==I2sClockBasepri&&!__get_FAULTMASK()&&!__get_IPSR()&&clockPrioritySupported();}
 bool counterEnabled(){return (CoreDebug->DEMCR&CoreDebug_DEMCR_TRCENA_Msk)&&(DWT->CTRL&DWT_CTRL_CYCCNTENA_Msk);}
 void enableCounter(){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;__DSB();__ISB();}
 uint32_t cycles(){return DWT->CYCCNT;}
 bool criticalSupported(){return clockPrioritySupported();}
 uint32_t basepri(){return __get_BASEPRI();}
 void raiseBasepri(uint32_t value){__set_BASEPRI_MAX(value);}
 void restoreBasepri(uint32_t value){
  if(restore_trace){
   if(restore_seen!=255u)++restore_seen;
   restore_icsr=SCB->ICSR;
   restore_pending=uint8_t((NVIC_GetPendingIRQ(UART7_IRQn)?1u:0u)|(NVIC_GetPendingIRQ(DMA1_Stream7_IRQn)?2u:0u));
   restore_basepri=__get_BASEPRI();restore_counter_valid=counterEnabled();
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
   scheduler_host_before=schedulerHostProgress(); // No DWT read; before the OLD mark.
#endif
   restore_mark=DWT->CYCCNT; // After first sync; snapshot/observer cost precedes this mark.
  }
  __set_BASEPRI(value);
 }
 void syncMask(){__DSB();__ISB();}
 uint32_t enterCritical(){return i2sClockEnterCritical(*this);}
 void exitCritical(uint32_t prior){i2sClockExitCritical(*this,prior);}
 void setFractionEnabled(bool enabled){if(enabled)SET_BIT(RCC->PLLCFGR,RCC_PLLCFGR_PLL2FRACEN);else CLEAR_BIT(RCC->PLLCFGR,RCC_PLLCFGR_PLL2FRACEN);__DSB();}
 void writeFraction(uint16_t fraction){MODIFY_REG(RCC->PLL2FRACR,RCC_PLL2FRACR_FRACN2,uint32_t(fraction)<<RCC_PLL2FRACR_FRACN2_Pos);__DSB();}
};
// Only tracking's new guard uses BASEPRI. Existing brief counter snapshots keep
// their original PRIMASK Critical. No wait, allocation or scheduling call here.
struct ClockSelectiveGuard {
 ClockTrimIo io;uint32_t prior=0;bool held=false;
 explicit ClockSelectiveGuard(bool stopped=false){
  if(__get_PRIMASK()||__get_FAULTMASK()||__get_IPSR()||!clockPrioritySupported(stopped))return;
  prior=io.enterCritical();held=true;
  if(!clockPrioritySupported(stopped)){io.exitCritical(prior);held=false;}
 }
 ~ClockSelectiveGuard(){if(held)io.exitCritical(prior);}
 explicit operator bool()const{return held;}
};
void relinquishClock(){trimRelinquished=true;clockTrim.relinquish();pllOwned=false;}
void clean(int32_t* p, uint32_t frames) { SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(p),frames*8); __DSB(); }
void refill(unsigned half) {
  if (__atomic_load_n(&outputPhase,__ATOMIC_ACQUIRE)!=Running || !ring) return;
  stats.frames_transmitted+=halfFrames; // DMA completion, not observed wire/acoustic output.
  int32_t* p=dmaWords+half*halfFrames*2;
  const uint32_t queued=uint32_t(ring->queued());
  const uint32_t at=micros();
  const bool wasRecovering=recovery.recovering();
  const uint32_t real=uint32_t(recovery.refill(*ring,p,at));
  if(real<halfFrames && !wasRecovering)
    audio_supply::firstStarvation(timingEpoch,at,queued,halfFrames,real);
  sourcePrepared+=real;sourceMissing+=halfFrames-real;
  if(real!=halfFrames) ++stats.underruns;
  timing.refill(timingEpoch,at,halfFrames,real,queued);
  clean(p,halfFrames);
}
void halfDone(DMA_HandleTypeDef*) { refill(0); }
void fullDone(DMA_HandleTypeDef*) { refill(1); }
void dmaError(DMA_HandleTypeDef*) {
  NVIC_DisableIRQ(DMA1_Stream7_IRQn);
  DMA1_Stream7->CR &= ~DMA_SxCR_EN;
  DMA1->HIFCR=DMA_HIFCR_CHTIF7|DMA_HIFCR_CTCIF7|DMA_HIFCR_CTEIF7|DMA_HIFCR_CDMEIF7|DMA_HIFCR_CFEIF7;
  recovery.end(micros(),ring?uint32_t(ring->queued()):0);
  ++stats.dma_errors; stats.running=false; stats.priming=false; __atomic_store_n(&outputPhase,uint32_t(Stopped),__ATOMIC_RELEASE);
  CLEAR_BIT(i2s.Instance->CFG1,SPI_CFG1_TXDMAEN); CLEAR_BIT(i2s.Instance->CR1,SPI_CR1_SPE);
}
void dmaIrq() {
  // If both halves completed before service, the producer missed an entire
  // DMA cycle. Do not write a buffer that may already be transmitting again.
  if(i2s.Instance->SR & SPI_SR_UDR) {
    DMA1_Stream7->CR &= ~DMA_SxCR_EN;
    dmaError(&dma); return;
  }
  const uint32_t flags=DMA1->HISR;
  if ((flags&DMA_HISR_HTIF7) && (flags&DMA_HISR_TCIF7)) {
    DMA1_Stream7->CR &= ~DMA_SxCR_EN;
    DMA1->HIFCR=DMA_HIFCR_CHTIF7|DMA_HIFCR_CTCIF7|DMA_HIFCR_CTEIF7|DMA_HIFCR_CDMEIF7|DMA_HIFCR_CFEIF7;
    dmaError(&dma); return;
  }
  HAL_DMA_IRQHandler(&dma);
}
void restoreClock() {
  if(trimRequested){
    // Tracking never repairs foreign/partially unknown clock state. Own SPI/DMA
    // have already stopped; only a still-matching PLL may use normal restoration.
    if(!pllOwned||trimRelinquished||!trimCaptured||!clockTrim.releaseAllowedAfterStop(clockRegisters())){relinquishClock();return;}
    {
      ClockSelectiveGuard guard(true);if(!guard){relinquishClock();return;}
      if(!clockTrim.releaseAllowedAfterStop(clockRegisters())){relinquishClock();return;}
      __HAL_RCC_SPI123_CONFIG(oldSpiClock);__HAL_RCC_PLL2_DISABLE();
    }
    for(unsigned i=0;i<100000&&__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY);++i)__NOP();
    {
      ClockSelectiveGuard guard(true);if(!guard){relinquishClock();return;}
      if(__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY)||!clockTrim.releaseAllowedAfterDisable(clockRegisters(),oldSpiClock)){relinquishClock();return;}
      RCC->PLL2DIVR=oldPllDiv;RCC->PLL2FRACR=oldPllFrac;
      MODIFY_REG(RCC->PLLCKSELR,RCC_PLLCKSELR_DIVM2,oldPllM);MODIFY_REG(RCC->PLLCFGR,pllMask,oldPllCfg);
    }
    pllOwned=false;return;
  }
  __HAL_RCC_SPI123_CONFIG(oldSpiClock);
  if(!pllOwned) return;
  __HAL_RCC_PLL2_DISABLE();
  // A bounded wait; never restore divider registers while PLL2 is still ready.
  for(unsigned i=0;i<100000 && __HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY);++i) __NOP();
  if(!__HAL_RCC_GET_FLAG(RCC_FLAG_PLL2RDY)) {
    RCC->PLL2DIVR=oldPllDiv; RCC->PLL2FRACR=oldPllFrac;
    MODIFY_REG(RCC->PLLCKSELR,RCC_PLLCKSELR_DIVM2,oldPllM);
    MODIFY_REG(RCC->PLLCFGR,pllMask,oldPllCfg);
  }
  pllOwned=false;
}
Status clockBegin(uint32_t rate) {
  I2sClock clock; if(!i2sClock(rate,clock)) return Status::Unsupported;
  // The PLL source is shared with PLL1: never change it. This build is tested
  // against GIGA's 16 MHz HSE setup, and rejects HSI/fallback clock trees.
  if((RCC->PLLCKSELR&RCC_PLLCKSELR_PLLSRC)!=RCC_PLLSOURCE_HSE || HSE_VALUE!=16000000u) return Status::Unsupported;
  if(RCC->CR&RCC_CR_PLL2ON) return Status::Busy;
  oldSpiClock=__HAL_RCC_GET_SPI123_SOURCE();
  oldPllDiv=RCC->PLL2DIVR; oldPllFrac=RCC->PLL2FRACR;
  oldPllM=RCC->PLLCKSELR&RCC_PLLCKSELR_DIVM2; oldPllCfg=RCC->PLLCFGR&pllMask;
  pllOwned=true;
  RCC_PeriphCLKInitTypeDef clk={}; clk.PeriphClockSelection=RCC_PERIPHCLK_SPI123;
  clk.Spi123ClockSelection=RCC_SPI123CLKSOURCE_PLL2;
  // 4 MHz VCI, VCO = kernel*8 (~361/393 MHz). Fractional PLL error < 1 ppm.

  clk.PLL2.PLL2M=4; clk.PLL2.PLL2N=clock.pll_n;
  clk.PLL2.PLL2FRACN=clock.pll_frac; clk.PLL2.PLL2P=8;
  clk.PLL2.PLL2Q=8; clk.PLL2.PLL2R=8;
  clk.PLL2.PLL2RGE=RCC_PLL2VCIRANGE_2; clk.PLL2.PLL2VCOSEL=RCC_PLL2VCOWIDE;
  if(HAL_RCCEx_PeriphCLKConfig(&clk)!=HAL_OK) { restoreClock(); return Status::HardwareError; }
  return Status::Ok;
}
}
Status i2sBegin(uint32_t sampleRate) {
  return i2sBegin(sampleRate, GIGA_BREDR_I2S_BUFFER_MS);
}
Status i2sBegin(uint32_t sampleRate, uint16_t bufferMs) {
  return i2sBegin(sampleRate,bufferMs,I2sPins());
}
Status i2sBegin(uint32_t sampleRate, uint16_t bufferMs,const I2sPins& pins) {
  return i2sBegin(sampleRate,bufferMs,pins,0);
}
Status i2sBegin(uint32_t sampleRate, uint16_t bufferMs,const I2sPins& pins,uint16_t startupMs) {
  if(owned) return Status::Busy;
  if(bufferMs<2 || bufferMs>MaximumPcmBufferMs || startupMs>bufferMs) return Status::InvalidArgument;
  I2sRoutePlan selected;if(!i2sRoutePlan(pins,selected))return Status::InvalidArgument;
  I2sClock clock; if(!i2sClock(sampleRate,clock)) return Status::Unsupported;
  uint32_t div=0,odd=0;if(!i2sPrescaler(sampleRate,pins.mclk!=-1,div,odd))return Status::Unsupported;
  I2sBufferGeometry geometry; if(!i2sBufferGeometry(sampleRate,bufferMs,geometry)) return Status::InvalidArgument;
  const uint32_t requested=startupMs?uint32_t(((uint64_t(sampleRate)*startupMs+999u)/1000u+3u)&~uint64_t(3u)):0;
  const uint32_t threshold=i2sPrimeFrames(geometry.ring_frames,geometry.half_frames,requested);
  if(!threshold) return Status::InvalidArgument;
  // SPI123 shares its kernel mux. Reject even clock-enabled idle SPI users.
  if(RCC->APB1LENR&(RCC_APB1LENR_SPI2EN|RCC_APB1LENR_SPI3EN)) return Status::Busy;
  if(RCC->APB2ENR&RCC_APB2ENR_SPI1EN) return Status::Busy;
  if(DMA1_Stream7->CR || DMAMUX1_Channel7->CCR || NVIC_GetEnableIRQ(DMA1_Stream7_IRQn) || NVIC_GetPendingIRQ(DMA1_Stream7_IRQn)) return Status::Busy;
  for(uint8_t i=0;i<selected.count;++i)if(i2sPinsBusy(gpioRegisters(gpioPort(selected.pins[i].port)),I2sPortPins(1u<<selected.pins[i].pin)))return Status::Busy;
  __HAL_RCC_DMA1_CLK_ENABLE();
  halfFrames=geometry.half_frames; // ~1 ms, each DMA half is cache-line isolated.
  primeFrames=threshold;
  const uint32_t ringFrames=geometry.ring_frames;
  const size_t dmaBytes=geometry.dma_bytes, ringOffset=geometry.ring_offset, bytes=geometry.total_bytes;
  arena=allocate(bytes,MemoryKind::Dma,32);
  if(!arena) return Status::OutOfMemory;
  if(!i2sDmaAddress(reinterpret_cast<uintptr_t>(arena),bytes)) { release(arena); arena=nullptr; return Status::HardwareError; }
  dmaWords=static_cast<int32_t*>(arena); memset(arena,0,bytes);
  ring=new(static_cast<uint8_t*>(arena)+dmaBytes) I2sRing(reinterpret_cast<int32_t*>(static_cast<uint8_t*>(arena)+ringOffset),ringFrames);
  clockTrim=I2sClockTrimState{};clockTrimLast={};trimRequested=trimCaptured=trimRelinquished=false;
  Status result=clockBegin(sampleRate);
  if(result!=Status::Ok) { ring->~I2sRing(); ring=nullptr; release(arena); arena=nullptr; return result; }
  pinPlan=selected;oldVector=NVIC_GetVector(DMA1_Stream7_IRQn);oldPriority=NVIC_GetPriority(DMA1_Stream7_IRQn);
  owned=true; stats=OutputStats(); stats.sample_rate=sampleRate;
  stats.dma_half_frames=halfFrames;stats.buffer_capacity_frames=ringFrames;stats.startup_frames=primeFrames;
  ringCapacity=ringFrames;sourcePrepared=sourceMissing=0;
  if(!++timingEpoch) ++timingEpoch;
  timing.beginEpoch(timingEpoch);
  recovery.begin(timingEpoch,ringFrames,halfFrames,primeFrames);
  for(uint8_t i=0;i<pinPlan.count;++i) {
    const auto& pin=pinPlan.pins[i];gpioClock(pin.port);
    oldPins[i]=i2sCapturePins(gpioRegisters(gpioPort(pin.port)),I2sPortPins(1u<<pin.pin));
    GPIO_InitTypeDef gpio={};gpio.Pin=1u<<pin.pin;gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL;gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;gpio.Alternate=pin.af;HAL_GPIO_Init(gpioPort(pin.port),&gpio);
  }
  spiClockEnable();spiReset();
  memset(&i2s,0,sizeof(i2s)); i2s.Instance=spiInstance(pinPlan.peripheral);
  // Avoid global weak MSP hooks: resources above are exclusively managed here.
  i2s.State=HAL_I2S_STATE_READY;
  i2s.Init.Mode=I2S_MODE_MASTER_TX; i2s.Init.Standard=I2S_STANDARD_PHILIPS;
  i2s.Init.DataFormat=I2S_DATAFORMAT_32B; i2s.Init.MCLKOutput=pins.mclk==-1?I2S_MCLKOUTPUT_DISABLE:I2S_MCLKOUTPUT_ENABLE;
  i2s.Init.AudioFreq=sampleRate; i2s.Init.CPOL=I2S_CPOL_LOW; i2s.Init.FirstBit=I2S_FIRSTBIT_MSB;
  i2s.Init.WSInversion=I2S_WS_INVERSION_DISABLE; i2s.Init.Data24BitAlignment=I2S_DATA_24BIT_ALIGNMENT_LEFT;
  i2s.Init.MasterKeepIOState=I2S_MASTER_KEEP_IO_STATE_ENABLE;
  memset(&dma,0,sizeof(dma)); dma.Instance=DMA1_Stream7;
  dma.Init.Request=pinPlan.peripheral==1?DMA_REQUEST_SPI1_TX:pinPlan.peripheral==2?DMA_REQUEST_SPI2_TX:DMA_REQUEST_SPI3_TX; dma.Init.Direction=DMA_MEMORY_TO_PERIPH;
  dma.Init.PeriphInc=DMA_PINC_DISABLE; dma.Init.MemInc=DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD; dma.Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
  dma.Init.Mode=DMA_CIRCULAR; dma.Init.Priority=DMA_PRIORITY_HIGH; dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
  dmaReady=false;
  if(HAL_I2S_Init(&i2s)!=HAL_OK) { i2sEnd(); return Status::HardwareError; }
  if((i2s.Instance->I2SCFGR&(SPI_I2SCFGR_I2SDIV|SPI_I2SCFGR_ODD))!=((div<<SPI_I2SCFGR_I2SDIV_Pos)|(odd?SPI_I2SCFGR_ODD:0u))) {i2sEnd();return Status::HardwareError;}
  if(HAL_DMA_Init(&dma)!=HAL_OK) { i2sEnd(); return Status::HardwareError; }
  dmaReady=true;
  __HAL_LINKDMA(&i2s,hdmatx,dma);
  // Reserve the vector while stopped; negotiation does not run an empty DMA.
  NVIC_SetVector(DMA1_Stream7_IRQn,reinterpret_cast<uint32_t>(dmaIrq));
  NVIC_SetPriority(DMA1_Stream7_IRQn,6); NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn);
  stats.initialized=true; stats.priming=true; stats.running=false;
  __atomic_store_n(&outputPhase,uint32_t(Priming),__ATOMIC_RELEASE);
  // Read-only acquisition fingerprint: no DWT or trim write in disabled mode.
  trimCaptured=clockTrim.capture(timingEpoch,clockRegisters());
  clockTrimLast=clockTrim.snapshot().last;
  return Status::Ok;
}
size_t i2sWrite(const int32_t* pcm,size_t frames) {
  const uint32_t phase=__atomic_load_n(&outputPhase,__ATOMIC_ACQUIRE);
  if((phase!=Priming && phase!=Running) || !ring) return 0;
  size_t accepted=ring->push(pcm,frames);
  if(phase==Priming && ring->prime(dmaWords,halfFrames,primeFrames)) {
    // Priming freed both DMA halves before any IRQ consumer can run. Refill
    // that room from this caller before starting a deliberately small ring.
    if(pcm && accepted<frames) accepted+=ring->push(pcm+accepted*2,frames-accepted);
    sourcePrepared+=uint64_t(halfFrames)*2;
    recovery.seedPrimed(dmaWords,halfFrames*2);
    clean(dmaWords,halfFrames*2);
    stats.initial_queued_frames=uint32_t(ring->queued());
    // H747 HAL passes Size unchanged to HAL_DMA_Start_IT, in transfer elements.
    // Both halves contain real PCM; the queue retains its startup jitter reserve.
    if(HAL_I2S_Transmit_DMA(&i2s,reinterpret_cast<uint16_t*>(dmaWords),halfFrames*4)!=HAL_OK) {
      dmaError(&dma); return accepted;
    }
    dma.XferHalfCpltCallback=halfDone; dma.XferCpltCallback=fullDone; dma.XferErrorCallback=dmaError;
    stats.running=true; stats.priming=false;
    __atomic_store_n(&outputPhase,uint32_t(Running),__ATOMIC_RELEASE);
    NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn); NVIC_EnableIRQ(DMA1_Stream7_IRQn);
  }
  return accepted;
}
void i2sEnd() {
  if(!owned) return;
  if(trimRequested&&(!trimCaptured||!clockTrim.releaseAllowed(clockRegisters())))relinquishClock();
  NVIC_DisableIRQ(DMA1_Stream7_IRQn);
  recovery.end(micros(),ring?uint32_t(ring->queued()):0);
  timing.endEpoch(timingEpoch);
  stats.running=false; stats.priming=false; stats.initialized=false; __atomic_store_n(&outputPhase,uint32_t(Stopped),__ATOMIC_RELEASE);
  CLEAR_BIT(i2s.Instance->CFG1,SPI_CFG1_TXDMAEN); CLEAR_BIT(i2s.Instance->CR1,SPI_CR1_SPE);
  if(dmaReady) { HAL_DMA_Abort(&dma); HAL_DMA_DeInit(&dma); dmaReady=false; }
  else {
    // Do not pass an uninitialized HAL DMA handle through abort/deinit when
    // I2S/clock setup failed before DMA initialization populated its base.
    DMA1_Stream7->CR=0; DMA1_Stream7->NDTR=0; DMA1_Stream7->PAR=0;
    DMA1_Stream7->M0AR=0; DMA1_Stream7->M1AR=0; DMA1_Stream7->FCR=0x21;
    DMAMUX1_Channel7->CCR=0;
  }
  NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn); NVIC_SetVector(DMA1_Stream7_IRQn,oldVector); NVIC_SetPriority(DMA1_Stream7_IRQn,oldPriority);
  spiReset();spiClockDisable();
  for(uint8_t i=0;i<pinPlan.count;++i) {const auto& pin=pinPlan.pins[i];i2sRestorePins(gpioRegisters(gpioPort(pin.port)),I2sPortPins(1u<<pin.pin),oldPins[i]);}
  restoreClock();
  clockTrim.invalidate();
  if(ring) { ring->~I2sRing(); ring=nullptr; }
  release(arena); arena=nullptr; dmaWords=nullptr; stats.queued_frames=0;halfFrames=ringCapacity=primeFrames=0;owned=false;
  stats.dma_half_frames=stats.buffer_capacity_frames=stats.startup_frames=stats.initial_queued_frames=0;
}
OutputStats i2sStats() { Critical guard; OutputStats copy=stats; copy.queued_frames=ring?ring->queued():0; return copy; }
OutputTimingSnapshot i2sTiming() { Critical guard; return timing.snapshot(); }
PcmRecoverySnapshot i2sRecovery() { Critical guard; return recovery.snapshot(); }
OutputSourceSnapshot i2sSourceSnapshot(){Critical guard;OutputSourceSnapshot out;
 out.output_epoch=timingEpoch;out.sample_rate=stats.sample_rate;out.ring_capacity=ringCapacity;
 out.half_frames=stats.initialized?halfFrames:0;
 out.prepared=sourcePrepared;out.missing=sourceMissing;out.initialized=stats.initialized;out.running=stats.running;
 out.recovering=recovery.snapshot().recovering;return out;
}
ClockTrimResult i2sClockTrim(uint32_t expected,int32_t requested,int32_t lower,int32_t upper){
 ClockTrimResult result;result.output_epoch=timingEpoch;result.requested_q16=requested;
 if(!expected||expected!=timingEpoch){result.status=ClockTrimCode::StaleEpoch;return result;}
 if(!owned||!stats.initialized||!stats.running){result.status=ClockTrimCode::Inactive;return result;}
 trimRequested=true;
 if(!trimCaptured){result.status=ClockTrimCode::Unsupported;clockTrimLast=result;return result;}
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
 schedulerBegin(expected);
#endif
 ClockTrimIo io;io.restore_trace=true;result=clockTrim.apply(io,expected,requested,lower,upper);
 // Derive after the original apply endpoint; no new end read or gate change.
 result.restore_seen=io.restore_seen;result.restore_pending=io.restore_pending;
 result.restore_icsr=io.restore_icsr;result.restore_basepri=io.restore_basepri;
 result.restore_flags=uint8_t((io.restore_seen?1u:0u)|(io.restore_counter_valid?2u:0u)|8u);
 if(io.restore_seen==1u&&io.restore_counter_valid&&result.duration_valid&&(result.witness_mask&0x9fu)==0x1fu){
  const uint32_t before=uint32_t(io.restore_mark-result.restore_t2),after=uint32_t(result.restore_t3-io.restore_mark);
  if(uint64_t(before)+uint64_t(after)==uint64_t(result.wait1_parts[2])){
   result.restore_split[0]=before;result.restore_split[1]=after;result.restore_flags=7u;
  }
 }
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
 schedulerEnd(result,io.restore_mark,io.scheduler_host_before);
#endif
 clockTrimLast=result;return result;
}
I2sClockTrimSnapshot i2sClockTrimStats(){Critical guard;auto out=clockTrim.snapshot();out.last=clockTrimLast;
 out.requested=out.requested||trimRequested;out.relinquished=out.relinquished||trimRelinquished;return out;
}
}}
#else
namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t) { return Status::Unsupported; }
Status i2sBegin(uint32_t, uint16_t bufferMs) { return bufferMs<2 || bufferMs>MaximumPcmBufferMs ? Status::InvalidArgument : Status::Unsupported; }
Status i2sBegin(uint32_t rate,uint16_t bufferMs,const I2sPins& pins) {return i2sBegin(rate,bufferMs,pins,0);}
Status i2sBegin(uint32_t,uint16_t bufferMs,const I2sPins& pins,uint16_t startupMs) {I2sRoutePlan plan;return bufferMs<2 || bufferMs>MaximumPcmBufferMs || startupMs>bufferMs || !i2sRoutePlan(pins,plan)?Status::InvalidArgument:Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t) { return 0; }
void i2sEnd() {}
OutputStats i2sStats() { return OutputStats(); }
OutputTimingSnapshot i2sTiming() { return {}; }
PcmRecoverySnapshot i2sRecovery() { return {}; }
OutputSourceSnapshot i2sSourceSnapshot(){return{};}
ClockTrimResult i2sClockTrim(uint32_t epoch,int32_t requested,int32_t,int32_t){ClockTrimResult out;out.output_epoch=epoch;out.requested_q16=requested;out.status=ClockTrimCode::Unsupported;return out;}
I2sClockTrimSnapshot i2sClockTrimStats(){return{};}
}}
#endif
