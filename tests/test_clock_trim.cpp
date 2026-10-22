#include "platform/I2sClockTrim.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
using namespace giga_bredr::detail;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);std::abort();}}while(0)
struct Trace {char kind;uint32_t cycles;bool masked;};
struct Model {
 I2sClockRegisters r;uint32_t tick=0x12345678,step=8,reads=0,basepri_value=0;bool masked=false,counter=true;
 bool fail_counter=false,stall_after_clear=false,ignore_clear=false,ignore_fraction=false,ignore_set=false,priority_supported=true;
 unsigned unmasked=0;std::function<void(Model&)> after_clear,after_set;std::vector<Trace> trace;
 Model(){r.output_epoch=7;r.owned=r.running=r.interrupts_enabled=r.spi_clock_enabled=true;r.core_hz=480000000;r.reference_hz=4000000;
  r.cr=I2sClockMasks::RequiredCr;r.pllckselr=2u|(4u<<12);r.pllcfgr=0x80000000u|I2sClockMasks::FracEnable|0x80u|I2sClockMasks::PEnable;
  r.pll2divr=97u|(7u<<9)|(7u<<16)|(7u<<24);r.pll2fracr=0xa5000001u|(2490u<<3);r.spi_mux=0x1000;r.instance=2;r.spi_i2scfgr=0x12345678;r.foreign[0]=0x76543210;}
 I2sClockRegisters snapshot(){auto v=r;v.interrupts_enabled=!masked;return v;}
 bool interruptsEnabled(){return !masked;}
 bool criticalSupported(){return priority_supported;}
 bool counterEnabled(){return counter;}
 void enableCounter(){trace.push_back({'E',tick,masked});if(!fail_counter)counter=true;}
 uint32_t cycles(){++reads;if(counter)tick+=step;return tick;}
 uint32_t basepri(){return basepri_value;}
 void raiseBasepri(uint32_t value){if(value&&(!basepri_value||value<basepri_value))basepri_value=value;masked=basepri_value&&basepri_value<=0x60u;}
 void restoreBasepri(uint32_t value){basepri_value=value;masked=basepri_value&&basepri_value<=0x60u;}
 void syncMask(){}
 uint32_t enterCritical(){const uint32_t old=i2sClockEnterCritical(*this);trace.push_back({'K',tick,masked});return old;}
 void exitCritical(uint32_t old){i2sClockExitCritical(*this,old);trace.push_back({'U',tick,masked});++unmasked;
  if(unmasked==1&&after_clear)after_clear(*this);if(unmasked==2&&after_set)after_set(*this);}
 void setFractionEnabled(bool enabled){trace.push_back({enabled?'S':'C',tick,masked});
  if(enabled){if(!ignore_set)r.pllcfgr|=I2sClockMasks::FracEnable;}
  else{if(!ignore_clear)r.pllcfgr&=~I2sClockMasks::FracEnable;if(stall_after_clear)step=0;}}
 void writeFraction(uint16_t value){trace.push_back({'W',tick,masked});if(!ignore_fraction)r.pll2fracr=(r.pll2fracr&~I2sClockMasks::Fraction)|(uint32_t(value)<<3);}
};
static unsigned count(const Model&m,char c){unsigned n=0;for(auto t:m.trace)n+=t.kind==c;return n;}
static ClockTrimResult apply(I2sClockTrimState& s,Model&m,int32_t request=10*65536,int32_t low=-500*65536,int32_t high=500*65536){return s.apply(m,7,request,low,high);}
static void quantizer(){
 for(uint16_t n:{uint16_t(4),uint16_t(90),uint16_t(98),uint16_t(512)})for(uint16_t f:{uint16_t(0),uint16_t(1),uint16_t(2490),uint16_t(8190),uint16_t(8191)})for(int ppm:{-700,-500,-11,-1,0,1,11,500,700}){
  auto q=i2sClockQuantize(n,f,ppm*65536,-500*65536,500*65536);CHECK(q.representable&&q.fraction<=8191);CHECK(q.applied_q16>=-500*65536&&q.applied_q16<=500*65536);
  auto singleton=i2sClockQuantize(n,f,q.applied_q16,q.applied_q16,q.applied_q16);CHECK(singleton.representable&&singleton.fraction==q.fraction&&singleton.applied_q16==q.applied_q16);
 }
 CHECK(!i2sClockQuantize(98,2490,1,1,2).representable);
 CHECK(!i2sClockQuantize(98,0,-65536,-500*65536,-1).representable);
 CHECK(!i2sClockQuantize(98,8191,65536,1,500*65536).representable);
 CHECK(!i2sClockQuantize(0,0,0,-1,1).representable);
 CHECK(!i2sClockQuantize(98,2490,0,10,-10).representable);
 auto extreme=i2sClockQuantize(98,2490,INT32_MIN,INT32_MIN,INT32_MAX);CHECK(extreme.representable&&extreme.applied_q16>=-500*65536);
 puts("Q16_INTERVAL_QUANTIZATION_SINGLETON_AND_FRACN_INTEGER_N_LIMITS_PASS");
}
static void sequence(){
 Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));auto before=m.r;uint32_t start=m.tick;
 auto result=apply(s,m);CHECK(result.accepted&&result.status==ClockTrimCode::Applied&&result.applied_q16!=10*65536);
 CHECK(result.fraction==((m.r.pll2fracr&I2sClockMasks::Fraction)>>3));CHECK(count(m,'C')==1&&count(m,'W')==1&&count(m,'S')==1);
 CHECK(m.r.cr==before.cr&&m.r.pllckselr==before.pllckselr&&m.r.pll2divr==before.pll2divr&&m.r.spi_mux==before.spi_mux);
 CHECK((m.r.pllcfgr&~I2sClockMasks::FracEnable)==(before.pllcfgr&~I2sClockMasks::FracEnable));
 CHECK((m.r.pll2fracr&~I2sClockMasks::Fraction)==(before.pll2fracr&~I2sClockMasks::Fraction));
 uint32_t clear=0,write=0,set=0;unsigned order=0;
 for(auto t:m.trace){if(t.kind=='C'){CHECK(order==0&&t.masked);clear=t.cycles;order=1;}if(t.kind=='W'){CHECK(order==1&&t.masked);write=t.cycles;order=2;}if(t.kind=='S'){CHECK(order==2&&t.masked);set=t.cycles;order=3;}}
 CHECK(order==3&&uint32_t(write-clear)>=360&&uint32_t(m.tick-set)>=360&&!m.masked);
 CHECK(result.wait_cycles[0]>=360&&result.wait_cycles[1]>=360&&result.max_critical_cycles>0);
 CHECK(result.duration_valid&&result.duration_cycles>=720&&result.duration_cycles<=uint32_t(m.tick-start));
 CHECK(s.releaseAllowed(m.snapshot()));auto oldWrites=count(m,'W');auto nochange=apply(s,m,result.applied_q16,result.applied_q16,result.applied_q16);
 CHECK(nochange.accepted&&nochange.status==ClockTrimCode::Unchanged&&count(m,'W')==oldWrites);
 puts("REAL_MASKS_CLEAR_WAIT_WRITE_SET_WAIT_READBACK_PRESERVE_OTHER_FIELDS_PASS");
}
static void reject_before_write(){
 for(unsigned which=0;which<15;++which){Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));
  switch(which){case 0:m.r.cr&=~(1u<<26);break;case 1:m.r.cr&=~(1u<<27);break;case 2:m.r.cr&=~(1u<<17);break;case 3:m.r.pllckselr^=1;break;case 4:m.r.pllckselr^=1u<<12;break;case 5:m.r.pllcfgr^=1u<<6;break;case 6:m.r.pllcfgr^=1u<<5;break;case 7:m.r.pllcfgr^=I2sClockMasks::PEnable;break;case 8:m.r.pll2divr^=1u<<16;break;case 9:m.r.pll2fracr^=8;break;case 10:m.r.spi_mux^=1u<<12;break;case 11:m.r.instance=1;break;case 12:m.r.foreign[3]^=1;break;case 13:m.r.foreign_consumer=true;break;case 14:m.r.spi_i2scfgr^=1;break;}
  auto result=apply(s,m);CHECK(!result.accepted&&result.status==ClockTrimCode::ForeignClock&&s.faulted());CHECK(!count(m,'C')&&!count(m,'W')&&!count(m,'S'));CHECK(!s.releaseAllowed(m.snapshot()));
 }
 Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));auto wrong=s.apply(m,8,0,-65536,65536);CHECK(!wrong.accepted&&wrong.status==ClockTrimCode::StaleEpoch&&!s.faulted()&&m.trace.empty());
 m.r.running=false;auto inactive=apply(s,m);CHECK(!inactive.accepted&&inactive.status==ClockTrimCode::Inactive&&!count(m,'C'));
 m.r.running=true;m.masked=true;auto irq=apply(s,m);CHECK(!irq.accepted&&!count(m,'C'));m.masked=false;
 CHECK(!apply(s,m,0,65536,-65536).accepted&&!count(m,'C'));
 puts("FOREIGN_FIELDS_ON_RDY_HSE_EPOCH_CONTEXT_REFUSE_ALL_WRITES_PASS");
}
static void failures(){
 for(unsigned which=0;which<8;++which){Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));
  switch(which){case 0:m.counter=false;m.fail_counter=true;break;case 1:m.step=0;break;case 2:m.stall_after_clear=true;break;case 3:m.ignore_clear=true;break;case 4:m.ignore_fraction=true;break;case 5:m.ignore_set=true;break;case 6:m.after_clear=[](Model&v){v.r.foreign[0]^=1;};break;case 7:m.after_set=[](Model&v){v.r.cr&=~(1u<<27);};break;}
  auto result=apply(s,m);CHECK(!result.accepted&&s.faulted()&&m.reads<600&&!m.masked);
  CHECK(s.releaseAllowed(m.snapshot())==(which<2)); // No clock write: safe owned teardown remains possible.
  unsigned writes=count(m,'W'),clears=count(m,'C');auto second=apply(s,m);CHECK(!second.accepted&&count(m,'W')==writes&&count(m,'C')==clears);
  if(which==0||which==1)CHECK(!count(m,'C'));else CHECK(result.partial&&count(m,'C')==1);
  if(which==4)CHECK(!count(m,'S'));
 }
 Model late;I2sClockTrimState ls;CHECK(ls.capture(7,late.snapshot()));late.after_clear=[](Model&v){v.step=20000;};auto irq=apply(ls,late);CHECK(irq.accepted&&irq.wall_overruns>0&&ls.releaseAllowed(late.snapshot()));
 Model wrap;I2sClockTrimState ws;CHECK(ws.capture(7,wrap.snapshot()));wrap.tick=0xffffff80;CHECK(apply(ws,wrap).accepted&&wrap.tick<0xffffff80);
 Model enable;enable.counter=false;enable.tick=0x87654321;I2sClockTrimState es;CHECK(es.capture(7,enable.snapshot()));CHECK(apply(es,enable).accepted&&enable.tick>0x87654321&&count(enable,'E')==1);
 puts("DWT_NO_RESET_ADVANCE_STALL_WRAP_PARTIAL_FAILURE_NO_REPAIR_RELEASE_GUARD_PASS");
}
static void stop_races(){
 Model before;I2sClockTrimState bs;CHECK(bs.capture(7,before.snapshot()));before.after_clear=[](Model&m){m.r.running=false;};auto a=apply(bs,before);
 CHECK(!a.accepted&&a.partial&&count(before,'W')==0&&!bs.releaseAllowed(before.snapshot()));
 Model after;I2sClockTrimState as;CHECK(as.capture(7,after.snapshot()));after.after_set=[](Model&m){m.r.running=false;};auto b=apply(as,after);
 CHECK(!b.accepted&&b.partial&&b.applied_q16==0&&count(after,'W')==1&&!as.releaseAllowed(after.snapshot()));
 Model stopped;I2sClockTrimState ss;CHECK(ss.capture(7,stopped.snapshot()));stopped.r.running=false;CHECK(ss.releaseAllowed(stopped.snapshot()));
 puts("STOP_DURING_UNMASKED_WAITS_REFUSES_COMMIT_SAFE_OWNED_STOP_DISTINCT_PASS");
}
static void consumers_and_release(){
 I2sClockConsumerRegisters registers;registers.mux[1]=1u<<12;registers.enable[4]=1u<<14;
 CHECK(!i2sClockForeignConsumer(registers,2));
 for(const auto&consumer:I2sPll2Consumers){auto value=registers;value.mux[consumer.mux]&=~(((1u<<consumer.width)-1u)<<consumer.shift);value.mux[consumer.mux]|=uint32_t(consumer.pll2)<<consumer.shift;
  value.enable[consumer.gate]|=consumer.enable;CHECK(i2sClockForeignConsumer(value,2));
  value.enable[consumer.gate]&=~consumer.enable;CHECK(!i2sClockForeignConsumer(value,2));
  value.enable[consumer.gate]|=consumer.enable;value.mux[consumer.mux]^=1u<<consumer.shift;CHECK(!i2sClockForeignConsumer(value,2));
 }
 for(unsigned own=1;own<=3;++own)for(unsigned other=1;other<=3;++other){I2sClockConsumerRegisters v;v.mux[1]=1u<<12;v.enable[other==1?6:4]=1u<<(other==1?12:other==2?14:15);CHECK(i2sClockForeignConsumer(v,own)==(own!=other));}
 Model m;I2sClockTrimState state;CHECK(state.capture(7,m.snapshot()));m.r.running=false;CHECK(state.releaseAllowed(m.snapshot()));m.r.spi_clock_enabled=false;m.r.spi_i2scfgr=0;
 CHECK(state.releaseAllowedAfterStop(m.snapshot()));auto foreign=m.snapshot();foreign.foreign[8]^=1;CHECK(!state.releaseAllowedAfterStop(foreign));
 m.r.spi_mux=0;m.r.cr&=~((1u<<26)|(1u<<27));CHECK(state.releaseAllowedAfterDisable(m.snapshot(),0));
 foreign=m.snapshot();foreign.pll2divr^=1;CHECK(!state.releaseAllowedAfterDisable(foreign,0));
 puts("ALL21_PLL2_CONSUMER_GROUPS_OWN_SPI_PRECISE_GATES_SAFE_TEARDOWN_PHASES_PASS");
}
int main(int argc,char**argv){
 if(argc==2&&!std::strcmp(argv[1],"stop")){stop_races();return 0;}
 if(argc==2&&!std::strcmp(argv[1],"irq")){Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));m.after_clear=[](Model&v){v.step=20000;};auto r=apply(s,m);CHECK(r.accepted&&r.wall_overruns>0&&s.releaseAllowed(m.snapshot()));return 0;}
 if(argc==2&&!std::strcmp(argv[1],"prewrite")){Model m;I2sClockTrimState s;CHECK(s.capture(7,m.snapshot()));m.counter=false;m.fail_counter=true;CHECK(!apply(s,m).accepted&&s.releaseAllowed(m.snapshot()));return 0;}
 quantizer();sequence();reject_before_write();failures();stop_races();consumers_and_release();std::printf("CLOCK_TRIM_MODEL_PASS checks=%u\n",checks);return 0;
}
