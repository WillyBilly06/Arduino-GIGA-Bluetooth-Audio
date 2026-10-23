// Estimated headroom is a controller target bound, not guaranteed free slots.
#include "detail/AudioClockControl.h"
#include <cstdio>
using namespace giga_bredr::detail;
static unsigned checks=0,failures=0;
static void check(bool ok,const char* reason){++checks;if(!ok){std::printf("FAIL %s\n",reason);++failures;}}
static AudioClockSample sample(uint32_t now,unsigned units,uint32_t frames=960,uint32_t pcm=4224){
 AudioClockSample s;s.now_us=now;s.completed_packets=100+now/5000;
 s.completed_frames=s.completed_packets*frames;s.decoded=s.completed_frames+frames;
 s.prepared=s.decoded-pcm;s.compressed_units_q16=units*65536u;return s;
}
static AudioClockDecision observe(AudioClockControl& c,const AudioClockSample& s){
 auto d=c.observe(s);if(d.update)check(c.acknowledge(d,true,d.requested_q16),"matching bounded acknowledgment");return d;
}
static void learn(AudioClockControl& c,unsigned units){
 check(c.begin(1,1,192000,3840,32),"begin LHDC192/Q32");
 for(uint32_t now=0;now<=2100000;now+=1000)observe(c,sample(now,units));
 check(c.snapshot().tracking,"initial learning reaches tracking");
}
static void high_initial_and_relearn(){
 AudioClockControl c;learn(c,28);auto s=c.snapshot();
 check(s.target_frames==26880,"high initial target leaves eight estimated packet units");
 check(s.inventory_frames==31104,"clamp retains the measured inventory");
 AudioClockControl relearn;learn(relearn,20);
 auto bad=sample(2101000,31);bad.healthy=false;observe(relearn,bad);
 check(relearn.snapshot().suspended,"new error suspends existing tracking");
 for(uint32_t now=2102000;now<=4302000;now+=1000)observe(relearn,sample(now,31));
 s=relearn.snapshot();check(s.tracking,"healthy observations relearn after error");
 check(s.target_frames==26880,"high-backlog relearn preserves estimated headroom");
 check(s.inventory_frames==33984,"relearn clamp preserves measured inventory");
 check(!s.invalid_samples&&!s.refused,"valid relearn preserves accounting and acknowledgment");
}
static void packetization_contraction(){
 AudioClockControl c;learn(c,20);
 for(uint32_t now=2101000;now<=60200000;now+=1000){
  const uint64_t extra=(now-2100000)/2500;auto s=sample(now,20);
  s.completed_packets=520+extra;s.completed_frames=520*960+extra*480;
  s.decoded=s.completed_frames+960;s.prepared=s.decoded-4224;observe(c,s);
 }
 const auto s=c.snapshot();const uint64_t ceiling=uint64_t(3840)*65536+s.frames_per_packet_q16*24;
 check(s.frames_per_packet_q16<481*65536u,"new packetization converges to the smaller frame span");
 check(s.target_frames*65536<=ceiling,"tracking target contracts with usable frame capacity");
 check(!s.invalid_samples&&!s.refused,"contraction preserves monotonic accounting");
}
static void small_queue_boundaries(){
 // Q4..8 reserve1,2,2,2,2 packets. In particular Q5 cannot round down.
 const uint64_t expected[]={400,400,500,600,700};
 for(unsigned q=4;q<=8;++q){AudioClockControl c;check(c.begin(1,1,48000,100,q),"small queue begins");
  for(uint32_t now=0;now<=2100000;now+=1000)observe(c,sample(now,q,100,200));
  const auto s=c.snapshot();check(s.tracking,"small queue learns");
  check(s.target_frames==expected[q-4],"Q4..8 use ceil-quarter reservation");
  check(s.inventory_frames==200+100*q,"small queue measured inventory remains unclipped");
 }
}
static void tiny_legacy_and_disabled(){
 for(unsigned q=1;q<=3;++q){AudioClockControl c;check(c.begin(1,1,48000,960,q),"tiny queue begins");
  AudioClockDecision first;
  for(uint32_t now=0;now<=2100000;now+=1000){auto d=observe(c,sample(now,q+1,480,1000));if(d.update&&!first.update)first=d;}
  const auto s=c.snapshot();check(s.target_frames==960+480*q,"Q1..3 retain full-capacity legacy target");
  check(s.inventory_frames==1000+480*(q+1),"tiny measured inventory retained");
  check(first.update&&first.requested_q16==0,"Q1..3 retain legacy clamped initial filter");
 }
 for(unsigned q=1;q<=8;++q){AudioClockControl c;AudioClockConfig cfg;cfg.enabled=false;
  check(c.begin(1,1,48000,960,q,cfg),"disabled queue begins");
  for(uint32_t now=0;now<=5000000;now+=250000)check(!c.observe(sample(now,q+1,480,1000)).update,"disabled control emits no decision");
  const auto s=c.snapshot();check(!s.tracking&&!s.learning&&!s.suspended&&!s.decisions&&!s.accepted&&!s.target_frames&&!s.applied_q16,"disabled state remains inert");
 }
}
int main(){high_initial_and_relearn();packetization_contraction();small_queue_boundaries();tiny_legacy_and_disabled();
 std::printf("CLOCK_QUEUE_HEADROOM checks=%u failures=%u\n",checks,failures);return failures?1:0;}
