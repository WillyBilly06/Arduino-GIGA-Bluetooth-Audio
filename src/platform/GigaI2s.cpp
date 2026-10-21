#include "GigaI2s.h"
#include "I2sBuffer.h"
#include "I2sPins.h"
#include "../detail/Memory.h"
#if defined(ARDUINO_GIGA) && defined(CORE_CM7)
#include <Arduino.h>
#include <mbed.h>
#include "stm32h7xx_hal.h"
#include <new>
#ifndef GIGA_BREDR_I2S_BUFFER_MS
#define GIGA_BREDR_I2S_BUFFER_MS 20
#endif
static_assert(GIGA_BREDR_I2S_BUFFER_MS >= 2 && GIGA_BREDR_I2S_BUFFER_MS <= 100,"I2S ring latency must be 2..100 ms");

namespace giga_bredr { namespace detail {
namespace {
I2S_HandleTypeDef i2s;
DMA_HandleTypeDef dma;
OutputStats stats;
OutputTiming timing;
uint32_t timingEpoch=0;
I2sRing* ring=nullptr;
void* arena=nullptr;
int32_t* dmaWords=nullptr;
uint32_t halfFrames=0;
bool owned=false,pllOwned=false,dmaReady=false;
enum : uint32_t { Stopped=0, Running=1, Priming=2 };
uint32_t outputPhase=Stopped;
uint32_t oldVector=0,oldPriority=0,oldSpiClock=0,oldPllDiv=0,oldPllFrac=0,oldPllCfg=0,oldPllM=0;
constexpr I2sPinPlan pinPlan=i2sPinPlan(GIGA_BREDR_I2S_WS_PIN);
I2sGpioSnapshot oldPinsB,oldPinsI;
I2sGpioRegisters gpioRegisters(GPIO_TypeDef* gpio) {
  return {gpio->MODER,gpio->OTYPER,gpio->OSPEEDR,gpio->PUPDR,gpio->AFR[0],gpio->AFR[1]};
}
constexpr uint32_t pllMask=RCC_PLLCFGR_PLL2FRACEN|RCC_PLLCFGR_PLL2VCOSEL|RCC_PLLCFGR_PLL2RGE|RCC_PLLCFGR_DIVP2EN|RCC_PLLCFGR_DIVQ2EN|RCC_PLLCFGR_DIVR2EN;
struct Critical { uint32_t state; Critical():state(__get_PRIMASK()) { __disable_irq(); __DMB(); } ~Critical() { __DMB(); __set_PRIMASK(state); } };
void clean(int32_t* p, uint32_t frames) { SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(p),frames*8); __DSB(); }
void refill(unsigned half) {
  if (__atomic_load_n(&outputPhase,__ATOMIC_ACQUIRE)!=Running || !ring) return;
  stats.frames_transmitted+=halfFrames; // DMA completion, not observed wire/acoustic output.
  int32_t* p=dmaWords+half*halfFrames*2;
  const uint32_t queued=uint32_t(ring->queued());
  const uint32_t real=uint32_t(ring->fill(p,halfFrames));
  if(real!=halfFrames) ++stats.underruns;
  timing.refill(timingEpoch,micros(),halfFrames,real,queued);
  clean(p,halfFrames);
}
void halfDone(DMA_HandleTypeDef*) { refill(0); }
void fullDone(DMA_HandleTypeDef*) { refill(1); }
void dmaError(DMA_HandleTypeDef*) {
  NVIC_DisableIRQ(DMA1_Stream7_IRQn);
  DMA1_Stream7->CR &= ~DMA_SxCR_EN;
  DMA1->HIFCR=DMA_HIFCR_CHTIF7|DMA_HIFCR_CTCIF7|DMA_HIFCR_CTEIF7|DMA_HIFCR_CDMEIF7|DMA_HIFCR_CFEIF7;
  ++stats.dma_errors; stats.running=false; stats.priming=false; __atomic_store_n(&outputPhase,uint32_t(Stopped),__ATOMIC_RELEASE);
  CLEAR_BIT(SPI2->CFG1,SPI_CFG1_TXDMAEN); CLEAR_BIT(SPI2->CR1,SPI_CR1_SPE);
}
void dmaIrq() {
  // If both halves completed before service, the producer missed an entire
  // DMA cycle. Do not write a buffer that may already be transmitting again.
  if(SPI2->SR & SPI_SR_UDR) {
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
  RCC_PeriphCLKInitTypeDef clk={}; clk.PeriphClockSelection=RCC_PERIPHCLK_SPI2;
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
  if(owned) return Status::Busy;
  I2sClock clock; if(!i2sClock(sampleRate,clock)) return Status::Unsupported;
  // SPI123 shares its kernel mux. Reject even clock-enabled idle SPI users.
  if(RCC->APB1LENR&(RCC_APB1LENR_SPI2EN|RCC_APB1LENR_SPI3EN)) return Status::Busy;
  if(RCC->APB2ENR&RCC_APB2ENR_SPI1EN) return Status::Busy;
  __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_GPIOI_CLK_ENABLE();
  if(pinPlan.portB.pins) __HAL_RCC_GPIOB_CLK_ENABLE();
  if(DMA1_Stream7->CR || DMAMUX1_Channel7->CCR || NVIC_GetEnableIRQ(DMA1_Stream7_IRQn) || NVIC_GetPendingIRQ(DMA1_Stream7_IRQn)) return Status::Busy;
  if(i2sPinsBusy(gpioRegisters(GPIOI),pinPlan.portI) ||
     i2sPinsBusy(gpioRegisters(GPIOB),pinPlan.portB)) return Status::Busy;
  halfFrames=((sampleRate+999)/1000+3)&~3u; // ~1 ms, each DMA half is cache-line isolated.
  uint32_t ringFrames=((sampleRate*GIGA_BREDR_I2S_BUFFER_MS+999)/1000+3)&~3u; // Selected rate/latency only.
  if(ringFrames<halfFrames*2) ringFrames=halfFrames*2;
  const size_t dmaBytes=size_t(halfFrames)*16;
  const size_t ringOffset=(dmaBytes+sizeof(I2sRing)+31)&~size_t(31);
  const size_t bytes=ringOffset+size_t(ringFrames)*8;
  arena=allocate(bytes,MemoryKind::Dma,32);
  if(!arena) return Status::OutOfMemory;
  if(!i2sDmaAddress(reinterpret_cast<uintptr_t>(arena),bytes)) { release(arena); arena=nullptr; return Status::HardwareError; }
  dmaWords=static_cast<int32_t*>(arena); memset(arena,0,bytes);
  ring=new(static_cast<uint8_t*>(arena)+dmaBytes) I2sRing(reinterpret_cast<int32_t*>(static_cast<uint8_t*>(arena)+ringOffset),ringFrames);
  Status result=clockBegin(sampleRate);
  if(result!=Status::Ok) { ring->~I2sRing(); ring=nullptr; release(arena); arena=nullptr; return result; }
  owned=true; stats=OutputStats(); stats.sample_rate=sampleRate;
  if(!++timingEpoch) ++timingEpoch;
  timing.beginEpoch(timingEpoch);
  oldPinsI=i2sCapturePins(gpioRegisters(GPIOI),pinPlan.portI);
  oldPinsB=i2sCapturePins(gpioRegisters(GPIOB),pinPlan.portB);
  GPIO_InitTypeDef gpio={}; gpio.Pin=pinPlan.portI.pins; gpio.Mode=GPIO_MODE_AF_PP;
  gpio.Pull=GPIO_NOPULL; gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate=GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOI,&gpio);
  if(pinPlan.portB.pins) { gpio.Pin=pinPlan.portB.pins; HAL_GPIO_Init(GPIOB,&gpio); }
  __HAL_RCC_SPI2_CLK_ENABLE(); __HAL_RCC_SPI2_FORCE_RESET(); __HAL_RCC_SPI2_RELEASE_RESET();
  memset(&i2s,0,sizeof(i2s)); i2s.Instance=SPI2;
  // Avoid global weak MSP hooks: resources above are exclusively managed here.
  i2s.State=HAL_I2S_STATE_READY;
  i2s.Init.Mode=I2S_MODE_MASTER_TX; i2s.Init.Standard=I2S_STANDARD_PHILIPS;
  i2s.Init.DataFormat=I2S_DATAFORMAT_32B; i2s.Init.MCLKOutput=I2S_MCLKOUTPUT_DISABLE;
  i2s.Init.AudioFreq=sampleRate; i2s.Init.CPOL=I2S_CPOL_LOW; i2s.Init.FirstBit=I2S_FIRSTBIT_MSB;
  i2s.Init.WSInversion=I2S_WS_INVERSION_DISABLE; i2s.Init.Data24BitAlignment=I2S_DATA_24BIT_ALIGNMENT_LEFT;
  i2s.Init.MasterKeepIOState=I2S_MASTER_KEEP_IO_STATE_ENABLE;
  oldVector=NVIC_GetVector(DMA1_Stream7_IRQn); oldPriority=NVIC_GetPriority(DMA1_Stream7_IRQn);
  memset(&dma,0,sizeof(dma)); dma.Instance=DMA1_Stream7;
  dma.Init.Request=DMA_REQUEST_SPI2_TX; dma.Init.Direction=DMA_MEMORY_TO_PERIPH;
  dma.Init.PeriphInc=DMA_PINC_DISABLE; dma.Init.MemInc=DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_WORD; dma.Init.MemDataAlignment=DMA_MDATAALIGN_WORD;
  dma.Init.Mode=DMA_CIRCULAR; dma.Init.Priority=DMA_PRIORITY_HIGH; dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
  dmaReady=false;
  if(HAL_I2S_Init(&i2s)!=HAL_OK) { i2sEnd(); return Status::HardwareError; }
  if(HAL_DMA_Init(&dma)!=HAL_OK) { i2sEnd(); return Status::HardwareError; }
  dmaReady=true;
  __HAL_LINKDMA(&i2s,hdmatx,dma);
  // Reserve the vector while stopped; negotiation does not run an empty DMA.
  NVIC_SetVector(DMA1_Stream7_IRQn,reinterpret_cast<uint32_t>(dmaIrq));
  NVIC_SetPriority(DMA1_Stream7_IRQn,6); NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn);
  stats.initialized=true; stats.priming=true; stats.running=false;
  __atomic_store_n(&outputPhase,uint32_t(Priming),__ATOMIC_RELEASE);
  return Status::Ok;
}
size_t i2sWrite(const int32_t* pcm,size_t frames) {
  const uint32_t phase=__atomic_load_n(&outputPhase,__ATOMIC_ACQUIRE);
  if((phase!=Priming && phase!=Running) || !ring) return 0;
  const size_t accepted=ring->push(pcm,frames);
  if(phase==Priming && ring->prime(dmaWords,halfFrames)) {
    clean(dmaWords,halfFrames*2);
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
  NVIC_DisableIRQ(DMA1_Stream7_IRQn);
  timing.endEpoch(timingEpoch);
  stats.running=false; stats.priming=false; stats.initialized=false; __atomic_store_n(&outputPhase,uint32_t(Stopped),__ATOMIC_RELEASE);
  CLEAR_BIT(SPI2->CFG1,SPI_CFG1_TXDMAEN); CLEAR_BIT(SPI2->CR1,SPI_CR1_SPE);
  if(dmaReady) { HAL_DMA_Abort(&dma); HAL_DMA_DeInit(&dma); dmaReady=false; }
  else {
    // Do not pass an uninitialized HAL DMA handle through abort/deinit when
    // I2S/clock setup failed before DMA initialization populated its base.
    DMA1_Stream7->CR=0; DMA1_Stream7->NDTR=0; DMA1_Stream7->PAR=0;
    DMA1_Stream7->M0AR=0; DMA1_Stream7->M1AR=0; DMA1_Stream7->FCR=0x21;
    DMAMUX1_Channel7->CCR=0;
  }
  NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn); NVIC_SetVector(DMA1_Stream7_IRQn,oldVector); NVIC_SetPriority(DMA1_Stream7_IRQn,oldPriority);
  __HAL_RCC_SPI2_FORCE_RESET(); __HAL_RCC_SPI2_RELEASE_RESET(); __HAL_RCC_SPI2_CLK_DISABLE();
  i2sRestorePins(gpioRegisters(GPIOI),pinPlan.portI,oldPinsI);
  i2sRestorePins(gpioRegisters(GPIOB),pinPlan.portB,oldPinsB);
  restoreClock();
  if(ring) { ring->~I2sRing(); ring=nullptr; }
  release(arena); arena=nullptr; dmaWords=nullptr; stats.queued_frames=0; owned=false;
}
OutputStats i2sStats() { Critical guard; OutputStats copy=stats; copy.queued_frames=ring?ring->queued():0; return copy; }
OutputTimingSnapshot i2sTiming() { Critical guard; return timing.snapshot(); }
}}
#else
namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t) { return Status::Unsupported; }
size_t i2sWrite(const int32_t*,size_t) { return 0; }
void i2sEnd() {}
OutputStats i2sStats() { return OutputStats(); }
OutputTimingSnapshot i2sTiming() { return {}; }
}}
#endif
