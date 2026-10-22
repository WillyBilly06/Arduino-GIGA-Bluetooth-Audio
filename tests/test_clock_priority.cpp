#define main original_register_model_main
#include "test_clock_trim.cpp"
#undef main

// CMSIS register model for the verbatim production enter/exit adapter methods.
static uint32_t mask_primask=0,mask_basepri=0,mask_group=3,mask_dma=6,mask_uart=0,mask_pendsv=15,mask_systick=15;
static uint32_t mask_fault=0,mask_ipsr=0;static bool mask_dma_enabled=true,mask_mutate_on_sync=false;
static unsigned mask_barriers=0;
#define __NVIC_PRIO_BITS 4
enum { DMA1_Stream7_IRQn=1,UART7_IRQn=2,PendSV_IRQn=3,SysTick_IRQn=4 };
static uint32_t NVIC_GetPriorityGrouping(){return mask_group;}
static uint32_t NVIC_GetPriority(int irq){return irq==DMA1_Stream7_IRQn?mask_dma:irq==UART7_IRQn?mask_uart:irq==PendSV_IRQn?mask_pendsv:mask_systick;}
static uint32_t NVIC_GetEnableIRQ(int){return mask_dma_enabled;}
static uint32_t __get_PRIMASK(){return mask_primask;}
static void __disable_irq(){mask_primask=1;}
static void __set_PRIMASK(uint32_t v){mask_primask=v;}
static uint32_t __get_BASEPRI(){return mask_basepri;}
static uint32_t __get_FAULTMASK(){return mask_fault;}
static uint32_t __get_IPSR(){return mask_ipsr;}
static void __set_BASEPRI_MAX(uint32_t v){if(v&&(!mask_basepri||v<mask_basepri))mask_basepri=v;}
static void __set_BASEPRI(uint32_t v){mask_basepri=v;}
static void __DMB(){++mask_barriers;}
static void __DSB(){++mask_barriers;if(mask_mutate_on_sync){mask_group=4;mask_mutate_on_sync=false;}}
static void __ISB(){++mask_barriers;}
#include "priority-functions.inc"
static bool serviceable(unsigned logical){return !mask_primask&&(!mask_basepri||(logical<<4)<mask_basepri);}
static void uart_service(){ClockTrimIo io;mask_primask=mask_basepri=0;const auto old=io.enterCritical();
 CHECK(serviceable(0));CHECK(!serviceable(6));CHECK(!serviceable(15));CHECK(mask_basepri==0x60&&!mask_primask);
 io.exitCritical(old);CHECK(!mask_basepri&&!mask_primask&&serviceable(6));}
static void nested_masks(){ClockTrimIo io;
 for(uint32_t previous:{0u,0x20u,0x60u,0xa0u}){mask_primask=0;mask_basepri=previous;unsigned barriers=mask_barriers;auto first=io.enterCritical();
  CHECK(first==previous&&mask_basepri==(previous&&previous<0x60u?previous:0x60u));CHECK(serviceable(0)&&!serviceable(6));auto second=io.enterCritical();
  io.exitCritical(second);CHECK(mask_basepri==(previous&&previous<0x60u?previous:0x60u));io.exitCritical(first);
  CHECK(mask_basepri==previous&&!mask_primask&&mask_barriers>=barriers+8);
 }mask_basepri=0;
}
static void priority_contract(){ClockTrimIo io;CHECK(io.criticalSupported());
 for(unsigned which=0;which<4;++which){mask_group=3;mask_dma=6;mask_uart=0;mask_pendsv=mask_systick=15;
  switch(which){case 0:mask_group=4;break;case 1:mask_dma=5;break;case 2:mask_uart=1;break;case 3:mask_pendsv=5;break;}
  CHECK(!io.criticalSupported());
 }mask_group=3;mask_dma=6;mask_uart=0;mask_pendsv=mask_systick=15;
 Model m;I2sClockTrimState state;CHECK(state.capture(7,m.snapshot()));m.priority_supported=false;m.counter=false;
 auto r=apply(state,m);CHECK(!r.accepted&&r.status==ClockTrimCode::InvalidContext&&!r.partial);CHECK(m.trace.empty()&&!m.counter&&!count(m,'C'));
}
static void conservative_duration(){struct DelayedExit:Model{void exitCritical(uint32_t prior){Model::exitCritical(prior);tick+=1000;}}m;
 I2sClockTrimState state;CHECK(state.capture(7,m.snapshot()));auto r=state.apply(m,7,10*65536,-500*65536,500*65536);
 CHECK(r.accepted&&r.max_critical_cycles>=1000);CHECK(!m.masked);
}
static void actual_restore_guard(){
 mask_primask=mask_basepri=mask_fault=mask_ipsr=0;mask_group=3;mask_dma=6;mask_uart=0;mask_pendsv=15;mask_dma_enabled=true;
 {ClockSelectiveGuard guard;CHECK(bool(guard)&&serviceable(0)&&!serviceable(6)&&!mask_primask);}CHECK(!mask_basepri&&!mask_primask);
 mask_dma=0;{ClockSelectiveGuard guard(true);CHECK(!guard&&!mask_basepri);} // Restored old priority alone is not enough.
 mask_dma_enabled=false;{ClockSelectiveGuard guard(true);CHECK(bool(guard)&&serviceable(0)&&!serviceable(6));}CHECK(!mask_basepri);
 for(unsigned which=0;which<4;++which){mask_fault=mask_ipsr=mask_primask=0;mask_group=3;
  switch(which){case 0:mask_fault=1;break;case 1:mask_ipsr=16;break;case 2:mask_primask=1;break;case 3:mask_group=4;break;}
  ClockSelectiveGuard guard(true);CHECK(!guard&&!mask_basepri);
 }mask_fault=mask_ipsr=mask_primask=0;mask_group=3;
 for(uint32_t previous:{0x20u,0xa0u}){mask_basepri=previous;{ClockSelectiveGuard guard(true);CHECK(bool(guard)&&serviceable(0)&&!serviceable(6));}CHECK(mask_basepri==previous);}
 mask_basepri=0;mask_mutate_on_sync=true;{ClockSelectiveGuard guard(true);CHECK(!guard&&!mask_basepri&&!mask_primask);}mask_group=3;
}
int main(int argc,char**argv){if(argc==2){if(!std::strcmp(argv[1],"uart"))uart_service();else if(!std::strcmp(argv[1],"nested"))nested_masks();else if(!std::strcmp(argv[1],"contract"))priority_contract();else if(!std::strcmp(argv[1],"duration"))conservative_duration();else if(!std::strcmp(argv[1],"guard"))actual_restore_guard();else return 2;}
 else{uart_service();nested_masks();priority_contract();conservative_duration();actual_restore_guard();}
 std::printf("SELECTIVE_BASEPRI_VERBATIM_ADAPTER_AND_POST_EXIT_DURATION_PASS checks=%u\n",checks);}
