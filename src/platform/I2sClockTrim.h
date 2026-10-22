#pragma once
#include <stdint.h>
#include <stddef.h>
namespace giga_bredr { namespace detail {
// CMSIS raw grouping3 gives four preemption bits on this H747. BASEPRI masks
// DMA6 and the lower-priority RTOS exceptions while UART0 remains serviceable.
constexpr uint32_t I2sClockBasepri=0x60u;
inline bool i2sClockPrioritySupported(uint32_t grouping,uint32_t bits,uint32_t dma,
 uint32_t uart,uint32_t pendsv,bool dma_stopped=false){
 return grouping==3u&&bits==4u&&(dma==6u||dma_stopped)&&uart==0u&&
        pendsv>=6u&&pendsv<16u;
}
template<class Io>uint32_t i2sClockEnterCritical(Io&io){
 const uint32_t prior=io.basepri();io.raiseBasepri(I2sClockBasepri);io.syncMask();return prior;
}
template<class Io>void i2sClockExitCritical(Io&io,uint32_t prior){
 io.syncMask();io.restoreBasepri(prior);io.syncMask();
}
// STM32H747 CMSIS masks, statically checked by the MCU adapter. Only these two
// fields may be written by tracking. Other RCC words are read-only fingerprints.
struct I2sClockMasks {
 static constexpr uint32_t FracEnable=0x10u,Fraction=0xfff8u,PEnable=0x80000u;
 static constexpr uint32_t Pll2Config=0x3800f0u,RequiredCr=0x0c030000u;
 static constexpr uint32_t SourceAndM=0x3f003u,SpiMux=0x7000u;
};
struct I2sClockConsumerRegisters {
 // D1CCIPR,D2CCIP1R,D2CCIP2R,D3CCIPR; then AHB1/2/3/4,
 // APB1L/1H/2/3/4 enable words. Values are read, never changed here.
 uint32_t mux[4]={},enable[9]={};
};
struct I2sClockConsumer {
 uint8_t mux,shift,width,pll2,gate;uint32_t enable;
};
constexpr I2sClockConsumer I2sPll2Consumers[]={
 {0,0,2,2,2,1u<<12},{0,4,2,2,2,1u<<14},{0,16,1,1,2,1u<<16},{0,16,1,1,1,1u<<9},{0,8,1,1,7,1u<<4},
 {1,0,3,1,6,1u<<22},{1,6,3,1,6,(1u<<23)|(1u<<24)},{1,16,3,1,6,(1u<<13)|(1u<<20)},
 {1,20,2,1,4,1u<<16},{1,28,2,2,5,1u<<8},
 {2,3,3,1,6,(1u<<4)|(1u<<5)},{2,0,3,1,4,(1u<<17)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<30)|(1u<<31)},
 {2,28,3,1,4,1u<<9},{3,0,3,1,8,1u<<3},{3,28,3,1,8,1u<<5},
 {3,21,3,1,8,1u<<21},{3,24,3,1,8,1u<<21},{3,16,2,0,0,1u<<5},{3,16,2,0,3,1u<<24},
 {3,10,3,1,8,1u<<9},{3,13,3,1,8,(1u<<10)|(1u<<11)|(1u<<12)}
};
inline bool i2sClockForeignConsumer(const I2sClockConsumerRegisters&r,unsigned own_spi){
 if(own_spi<1||own_spi>3)return true;
 if(((r.mux[1]>>12)&7u)==1u){
  if(own_spi!=1&&(r.enable[6]&(1u<<12)))return true;
  if(own_spi!=2&&(r.enable[4]&(1u<<14)))return true;
  if(own_spi!=3&&(r.enable[4]&(1u<<15)))return true;
 }
 for(const auto& c:I2sPll2Consumers)if((r.enable[c.gate]&c.enable)&&((r.mux[c.mux]>>c.shift)&((1u<<c.width)-1u))==c.pll2)return true;
 return false;
}
inline void i2sClockConsumerFingerprint(const I2sClockConsumerRegisters&r,unsigned own_spi,uint32_t* destination){
 uint32_t mux_mask[4]={},gate_mask[9]={};
 for(const auto&c:I2sPll2Consumers){mux_mask[c.mux]|=((1u<<c.width)-1u)<<c.shift;gate_mask[c.gate]|=c.enable;}
 gate_mask[6]|=1u<<12;gate_mask[4]|=(1u<<14)|(1u<<15);
 if(own_spi==1)gate_mask[6]&=~(1u<<12);
 if(own_spi==2)gate_mask[4]&=~(1u<<14);
 if(own_spi==3)gate_mask[4]&=~(1u<<15);
 for(unsigned i=0;i<4;++i)destination[i]=r.mux[i]&mux_mask[i];
 for(unsigned i=0;i<9;++i)destination[4+i]=r.enable[i]&gate_mask[i];
}
enum class ClockTrimCode:uint8_t { Applied,Unchanged,Inactive,StaleEpoch,InvalidArgument,
 Unrepresentable,Unsupported,ForeignClock,TimerUnavailable,TimerStalled,ReadbackFault,InvalidContext };
struct ClockTrimResult {
 ClockTrimCode status=ClockTrimCode::Inactive;
 uint32_t output_epoch=0,duration_cycles=0,wait_cycles[2]={},max_critical_cycles=0;
 int32_t requested_q16=0,applied_q16=0;
 uint16_t fraction=0,readback_fraction=0;
 uint8_t wall_overruns=0;
 bool accepted=false,partial=false,duration_valid=false;
};
struct ClockTrimQuantized {uint16_t fraction=0;int32_t applied_q16=0;bool representable=false;};
constexpr int32_t I2sClockLimitQ16=500*65536;
constexpr int64_t I2sClockPpmScale=1000000ll*65536;
inline int64_t i2sClockRound(int64_t numerator,int64_t denominator){
 return numerator>=0?(numerator+denominator/2)/denominator:-((-numerator+denominator/2)/denominator);
}
inline int64_t i2sClockCoefficient(uint16_t n,uint16_t nominal,uint16_t fraction){
 return i2sClockRound((int64_t(fraction)-nominal)*I2sClockPpmScale,int64_t(n)*8192+nominal);
}
inline ClockTrimQuantized i2sClockQuantize(uint16_t n,uint16_t nominal,int32_t requested,int32_t lower,int32_t upper){
 ClockTrimQuantized out;
 if(n<4||n>512||nominal>8191||lower>upper)return out;
 const int32_t low=lower < -I2sClockLimitQ16?-I2sClockLimitQ16:lower;
 const int32_t high=upper > I2sClockLimitQ16?I2sClockLimitQ16:upper;
 if(low>high)return out;
 // Search the same rounded Q16 representation returned to the controller.
 // This also keeps an accepted fraction representable in its singleton interval.
 unsigned left=0,right=8192;
 for(unsigned i=0;i<14&&left<right;++i){unsigned mid=(left+right)/2;if(i2sClockCoefficient(n,nominal,uint16_t(mid))<low)left=mid+1;else right=mid;}
 const unsigned first=left;left=0;right=8192;
 for(unsigned i=0;i<14&&left<right;++i){unsigned mid=(left+right)/2;if(i2sClockCoefficient(n,nominal,uint16_t(mid))<=high)left=mid+1;else right=mid;}
 if(first>=left||first>=8192)return out;
 int32_t clipped=requested<low?low:requested>high?high:requested;
 int64_t target=int64_t(nominal)+i2sClockRound(int64_t(clipped)*(int64_t(n)*8192+nominal),I2sClockPpmScale);
 if(target<int64_t(first))target=first;
 if(target>=int64_t(left))target=left-1;
 out.fraction=uint16_t(target);out.applied_q16=int32_t(i2sClockCoefficient(n,nominal,out.fraction));out.representable=true;return out;
}
struct I2sClockRegisters {
 uint32_t output_epoch=0,cr=0,pllckselr=0,pllcfgr=0,pll2divr=0,pll2fracr=0;
 uint32_t spi_mux=0,instance=0,spi_i2scfgr=0,core_hz=0,reference_hz=0;
 uint32_t foreign[20]={};
 bool owned=false,running=false,interrupts_enabled=false,foreign_consumer=false,spi_clock_enabled=false;
};
struct I2sClockTrimSnapshot {
 ClockTrimResult last;
 uint32_t attempts=0,accepted=0,faults=0,maximum_duration_cycles=0,maximum_critical_cycles=0;
 bool acquired=false,requested=false,faulted=false,relinquished=false;
};
class I2sClockTrimState {
 public:
 bool capture(uint32_t epoch,const I2sClockRegisters& r){
  if(captured_&&epoch==epoch_)return false;
  *this=I2sClockTrimState{};
  if(!epoch||r.output_epoch!=epoch||!valid(r))return false;
  epoch_=epoch;baseline_=r;fraction_=nominal_=uint16_t((r.pll2fracr&I2sClockMasks::Fraction)>>3);
  n_=uint16_t((r.pll2divr&0x1ffu)+1);captured_=true;telemetry_.acquired=true;
  telemetry_.last.output_epoch=epoch;telemetry_.last.fraction=telemetry_.last.readback_fraction=fraction_;return true;
 }
 bool faulted()const{return telemetry_.faulted;}
 bool requested()const{return telemetry_.requested;}
 void invalidate(){captured_=false;telemetry_.acquired=false;}
 void relinquish(){telemetry_.relinquished=true;telemetry_.faulted=true;captured_=false;telemetry_.acquired=false;}
 I2sClockTrimSnapshot snapshot()const{return telemetry_;}
 bool releaseAllowed(const I2sClockRegisters&r)const{return captured_&&!uncertain_clock_&&matches(r,fraction_,true,false);}
 bool releaseAllowedAfterStop(const I2sClockRegisters&r)const{return captured_&&!uncertain_clock_&&matches(r,fraction_,true,false,1,baseline_.spi_mux);}
 bool releaseAllowedAfterDisable(const I2sClockRegisters&r,uint32_t restored_mux)const{return captured_&&!uncertain_clock_&&matches(r,fraction_,true,false,2,restored_mux);}
 template<class Io>ClockTrimResult apply(Io& io,uint32_t expected,int32_t requested,int32_t lower,int32_t upper){
  ClockTrimResult result;result.output_epoch=epoch_;result.requested_q16=requested;result.applied_q16=applied_;result.fraction=fraction_;
  if(!captured_)return result;
  if(!expected||expected!=epoch_){result.status=ClockTrimCode::StaleEpoch;return result;}
  if(faulted()){result.status=telemetry_.last.status;return result;}
  if(lower>upper){result.status=ClockTrimCode::InvalidArgument;return result;}
  const auto quantized=i2sClockQuantize(n_,nominal_,requested,lower,upper);
  if(!quantized.representable){result.status=ClockTrimCode::Unrepresentable;return result;}
  ++telemetry_.attempts;
  auto current=io.snapshot();
  if(!current.running){result.status=ClockTrimCode::Inactive;return result;}
  if(!io.interruptsEnabled()||!io.criticalSupported()){result.status=ClockTrimCode::InvalidContext;return result;}
  uint32_t start=io.cycles();result.duration_valid=io.counterEnabled();
  auto finish=[&](ClockTrimCode code,bool fault)->ClockTrimResult {
   result.status=code;
   if(fault){telemetry_.faulted=true;++telemetry_.faults;uncertain_clock_=uncertain_clock_||result.partial||code==ClockTrimCode::ForeignClock;}
   auto readback=io.snapshot();result.readback_fraction=uint16_t((readback.pll2fracr&I2sClockMasks::Fraction)>>3);
   if(io.counterEnabled()&&result.duration_valid)result.duration_cycles=uint32_t(io.cycles()-start);else result.duration_valid=false;
   telemetry_.last=result;
   if(result.duration_cycles>telemetry_.maximum_duration_cycles)telemetry_.maximum_duration_cycles=result.duration_cycles;
   if(result.max_critical_cycles>telemetry_.maximum_critical_cycles)telemetry_.maximum_critical_cycles=result.max_critical_cycles;
   return result;
  };
  if(!matches(current,fraction_,true))return finish(ClockTrimCode::ForeignClock,true);
  telemetry_.requested=true;
  if(!io.counterEnabled())io.enableCounter(); // Never resets or disables CYCCNT.
  if(!io.counterEnabled())return finish(ClockTrimCode::TimerUnavailable,true);
  if(!result.duration_valid)start=io.cycles();result.duration_valid=true;
  uint32_t advance=0;
  auto timer=wait(io,1,advance,result.wall_overruns);
  if(timer!=ClockTrimCode::Applied)return finish(timer,true);
  if(!matches(io.snapshot(),fraction_,true))return finish(ClockTrimCode::ForeignClock,true);
  if(quantized.fraction==fraction_){result.status=ClockTrimCode::Unchanged;result.accepted=true;++telemetry_.accepted;return finish(ClockTrimCode::Unchanged,false);}
  ClockTrimCode error=ClockTrimCode::Applied;
  uint32_t critical_start=io.cycles();uint32_t prior=io.enterCritical();
  if(prior||!io.criticalSupported())error=ClockTrimCode::InvalidContext;
  else if(!matches(io.snapshot(),fraction_,true))error=ClockTrimCode::ForeignClock;
  else{
   result.partial=true;io.setFractionEnabled(false);
   if(!matches(io.snapshot(),fraction_,false))error=ClockTrimCode::ReadbackFault;
  }
  io.exitCritical(prior);result.max_critical_cycles=uint32_t(io.cycles()-critical_start);
  if(error!=ClockTrimCode::Applied)return finish(error,true);
  // Supported H747 CPU maximum, not the potentially stale SystemCoreClock.
  // At <=480MHz and4MHz reference,360 core cycles span at least3 references.
  const uint32_t minimum=360u;
  error=wait(io,minimum,result.wait_cycles[0],result.wall_overruns);if(error!=ClockTrimCode::Applied)return finish(error,true);
  if(!matches(io.snapshot(),fraction_,false))return finish(ClockTrimCode::ForeignClock,true);
  critical_start=io.cycles();prior=io.enterCritical();
  if(prior||!io.criticalSupported())error=ClockTrimCode::InvalidContext;
  else if(!matches(io.snapshot(),fraction_,false))error=ClockTrimCode::ForeignClock;
  else{
   io.writeFraction(quantized.fraction);
   if(!matches(io.snapshot(),quantized.fraction,false))error=ClockTrimCode::ReadbackFault;
   else{
    io.setFractionEnabled(true);
    if(!matches(io.snapshot(),quantized.fraction,true))error=ClockTrimCode::ReadbackFault;
   }
  }
  io.exitCritical(prior);const uint32_t second=uint32_t(io.cycles()-critical_start);if(second>result.max_critical_cycles)result.max_critical_cycles=second;
  if(error!=ClockTrimCode::Applied)return finish(error,true);
  error=wait(io,minimum,result.wait_cycles[1],result.wall_overruns);if(error!=ClockTrimCode::Applied)return finish(error,true);
  if(!matches(io.snapshot(),quantized.fraction,true))return finish(ClockTrimCode::ForeignClock,true);
  fraction_=quantized.fraction;applied_=quantized.applied_q16;result.fraction=fraction_;result.applied_q16=applied_;result.accepted=true;
  ++telemetry_.accepted;return finish(ClockTrimCode::Applied,false);
 }
 private:
 static bool valid(const I2sClockRegisters&r){
  return r.owned&&!r.foreign_consumer&&r.reference_hz==4000000u&&r.core_hz>=64000000u&&r.core_hz<=480000000u&&
   (r.cr&I2sClockMasks::RequiredCr)==I2sClockMasks::RequiredCr&&
   (r.pllckselr&I2sClockMasks::SourceAndM)==(2u|(4u<<12))&&
   (r.pllcfgr&0xf0u)==(I2sClockMasks::FracEnable|0x80u)&&(r.pllcfgr&I2sClockMasks::PEnable)&&
   (r.pll2divr&0x1ffu)>=3u&&(r.pll2divr&0xfe00u)==(7u<<9)&&
   (r.pll2divr&0x7f0000u)==(7u<<16)&&(r.pll2divr&0x7f000000u)==(7u<<24)&&
   (r.spi_mux&I2sClockMasks::SpiMux)==0x1000u&&r.instance>=1&&r.instance<=3&&r.spi_clock_enabled;
 }
 bool matches(const I2sClockRegisters&r,uint16_t fraction,bool enabled,bool require_running=true,unsigned phase=0,uint32_t released_mux=0)const{
  if((require_running&&!r.running)||!r.owned||r.foreign_consumer||r.output_epoch!=epoch_||r.core_hz!=baseline_.core_hz||r.reference_hz!=baseline_.reference_hz||
     (r.cr&I2sClockMasks::RequiredCr)!=(phase==2?0x30000u:I2sClockMasks::RequiredCr)||
     (r.pllckselr&I2sClockMasks::SourceAndM)!=(baseline_.pllckselr&I2sClockMasks::SourceAndM)||
     (r.pllcfgr&I2sClockMasks::Pll2Config)!=((baseline_.pllcfgr&I2sClockMasks::Pll2Config&~I2sClockMasks::FracEnable)|(enabled?I2sClockMasks::FracEnable:0u))||
     r.pll2divr!=baseline_.pll2divr||r.pll2fracr!=((baseline_.pll2fracr&~I2sClockMasks::Fraction)|(uint32_t(fraction)<<3))||
     r.spi_mux!=(phase?released_mux:baseline_.spi_mux)||r.instance!=baseline_.instance||
     r.spi_clock_enabled!=(phase==0)||(phase==0&&r.spi_i2scfgr!=baseline_.spi_i2scfgr))return false;
  for(unsigned i=0;i<20;++i)if(r.foreign[i]!=baseline_.foreign[i])return false;
  return true;
 }
 template<class Io>ClockTrimCode wait(Io&io,uint32_t minimum,uint32_t&elapsed,uint8_t&overruns)const{
  const uint32_t start=io.cycles();const uint32_t maximum=baseline_.core_hz/50000u; //20us at the validated configured clock.
  for(unsigned poll=0;poll<256;++poll){
   if(!io.counterEnabled()||!io.interruptsEnabled())return ClockTrimCode::TimerUnavailable;
   elapsed=uint32_t(io.cycles()-start);
   // IRQ preemption can extend wall time. A completed minimum remains valid;
   // record the overrun rather than mistaking a long interrupt for a stall.
   if(elapsed>=minimum){if(elapsed>maximum)++overruns;return ClockTrimCode::Applied;}
  }
  return ClockTrimCode::TimerStalled;
 }
 I2sClockRegisters baseline_;I2sClockTrimSnapshot telemetry_;
 uint32_t epoch_=0;uint16_t n_=0,nominal_=0,fraction_=0;int32_t applied_=0;bool captured_=false,uncertain_clock_=false;
};
}}
