#define main model_reference_main
#include "test_clock_trim.cpp"
#undef main
struct Rcc{uint32_t CR=0,PLLCFGR=0,PLLCKSELR=0,PLL2DIVR=0,PLL2FRACR=0,mux=0;}rcc;
static Rcc*RCC=&rcc;
static I2sClockTrimState clockTrim;static Model hardware;
static bool trimRequested=false,trimRelinquished=false,trimCaptured=false,pllOwned=true;
static uint32_t oldSpiClock=0,oldPllDiv=0x12345678,oldPllFrac=0x100,oldPllM=2u<<12,oldPllCfg=0;
static constexpr uint32_t pllMask=I2sClockMasks::Pll2Config;
static unsigned muxWrites=0,offWrites=0,flagReads=0,clearAfter=2,foreignAt=0;
enum:uint32_t{RCC_PLLCKSELR_DIVM2=0x3f000,RCC_FLAG_PLL2RDY=1};
static I2sClockRegisters clockRegisters(){auto r=hardware.snapshot();r.owned=pllOwned;r.running=false;r.spi_clock_enabled=false;r.spi_i2scfgr=0;
 r.cr=rcc.CR;r.pllcfgr=rcc.PLLCFGR;r.pllckselr=rcc.PLLCKSELR;r.pll2divr=rcc.PLL2DIVR;r.pll2fracr=rcc.PLL2FRACR;r.spi_mux=rcc.mux;return r;}
#define __NVIC_PRIO_BITS 4
enum{DMA1_Stream7_IRQn=1,UART7_IRQn=2,PendSV_IRQn=3};
static uint32_t NVIC_GetPriorityGrouping(){return hardware.priority_supported?3:4;}
static uint32_t NVIC_GetPriority(int irq){return irq==DMA1_Stream7_IRQn?0:irq==UART7_IRQn?0:15;}
static uint32_t NVIC_GetEnableIRQ(int){return 0;}
static uint32_t __get_PRIMASK(){return 0;}static uint32_t __get_FAULTMASK(){return 0;}static uint32_t __get_IPSR(){return 0;}
static uint32_t __get_BASEPRI(){return hardware.basepri();}
static void __set_BASEPRI_MAX(uint32_t v){hardware.raiseBasepri(v);}static void __set_BASEPRI(uint32_t v){hardware.restoreBasepri(v);}
static void __DSB(){}static void __ISB(){}
#include "priority-functions.inc"
static void relinquishClock(){trimRelinquished=true;clockTrim.relinquish();pllOwned=false;}
static bool readyFlag(){++flagReads;if(foreignAt&&flagReads==foreignAt)hardware.r.foreign[0]^=1;if(clearAfter&&flagReads>=clearAfter)rcc.CR&=~(1u<<27);return (rcc.CR&(1u<<27))!=0;}
#define __HAL_RCC_SPI123_CONFIG(v) do{++muxWrites;rcc.mux=(v);}while(0)
#define __HAL_RCC_PLL2_DISABLE() do{++offWrites;rcc.CR&=~(1u<<26);}while(0)
#define __HAL_RCC_GET_FLAG(v) readyFlag()
#define MODIFY_REG(reg,mask,value) ((reg)=((reg)&~(mask))|(value))
#define __NOP() do{}while(0)
#include "release-function.inc"
static void setup(){hardware=Model{};clockTrim=I2sClockTrimState{};CHECK(clockTrim.capture(7,hardware.snapshot()));trimCaptured=true;trimRequested=true;trimRelinquished=false;pllOwned=true;muxWrites=offWrites=flagReads=foreignAt=0;clearAfter=2;}
static void copyRegisters(){rcc={hardware.r.cr,hardware.r.pllcfgr,hardware.r.pllckselr,hardware.r.pll2divr,hardware.r.pll2fracr,hardware.r.spi_mux};hardware.r.running=false;hardware.r.spi_clock_enabled=false;hardware.r.spi_i2scfgr=0;}
static void restored(){CHECK(!pllOwned&&!trimRelinquished&&rcc.mux==oldSpiClock&&rcc.PLL2DIVR==oldPllDiv&&rcc.PLL2FRACR==oldPllFrac);CHECK((rcc.PLLCKSELR&RCC_PLLCKSELR_DIVM2)==oldPllM&&!(rcc.CR&((1u<<26)|(1u<<27))));}
int main(){
 setup();copyRegisters();trimRequested=false;uint32_t counter=hardware.tick;restoreClock();restored();CHECK(hardware.tick==counter&&count(hardware,'E')==0);
 setup();hardware.counter=false;hardware.fail_counter=true;CHECK(!apply(clockTrim,hardware).accepted);copyRegisters();restoreClock();restored();
 setup();CHECK(apply(clockTrim,hardware).accepted);copyRegisters();restoreClock();restored();
 setup();copyRegisters();auto before=rcc;hardware.r.foreign[0]^=1;restoreClock();CHECK(trimRelinquished&&!muxWrites&&!offWrites&&rcc.PLL2DIVR==before.PLL2DIVR&&rcc.PLL2FRACR==before.PLL2FRACR&&rcc.mux==before.mux);
 setup();hardware.stall_after_clear=true;CHECK(!apply(clockTrim,hardware).accepted);copyRegisters();before=rcc;restoreClock();CHECK(trimRelinquished&&!muxWrites&&!offWrites&&rcc.PLL2DIVR==before.PLL2DIVR);
 setup();copyRegisters();before=rcc;foreignAt=1;restoreClock();CHECK(trimRelinquished&&muxWrites==1&&offWrites==1&&rcc.PLL2DIVR==before.PLL2DIVR&&rcc.PLL2FRACR==before.PLL2FRACR);
 setup();copyRegisters();before=rcc;clearAfter=0;restoreClock();CHECK(trimRelinquished&&flagReads<=100001&&rcc.PLL2DIVR==before.PLL2DIVR);
 puts("VERBATIM_RESTORE_DISABLED_UNCHANGED_PREWRITEFAULT_SAFE_END_FOREIGN_PARTIAL_NO_REPAIR_PASS");std::printf("CLOCK_RELEASE_PASS checks=%u\n",checks);
}
