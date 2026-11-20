// Offline synthetic plant exercising the actual AudioClockControl and PLL
// fraction quantizer. No RTP, decoder, I2S hardware or physical clock model.
#include "detail/AudioClockControl.h"
#include "platform/I2sBuffer.h"
#include "platform/I2sClockTrim.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
using namespace giga_bredr::detail;

struct Result { double mean_tail_ppm,final_ppm,min_inventory,max_inventory;uint64_t missing,over_capacity;unsigned decisions,suspensions; };
static Result simulate(unsigned rate,double hz_before,double hz_after,bool jitter,bool enabled,bool fault,bool outage) {
 AudioClockControl c;AudioClockConfig cfg;cfg.enabled=enabled;
 const unsigned packet=(rate+50)/100,capacity=packet*8,initial=packet*4;
 assert(c.begin(1,1,rate,capacity,28,cfg));
 I2sClock hw;assert(i2sClock(rate,hw));
 AudioClockSample s;s.decoded=initial;s.completed_packets=1;s.completed_frames=packet;
 uint64_t generated=0;double source_phase=0,output_phase=0,tail=0;
 uint64_t tail_count=0;std::deque<uint32_t> arrivals;
 uint64_t over=0;unsigned decisions=0,suspensions=0;double applied=0;
 double mininv=1e100,maxinv=0;
 for(uint32_t ms=0;ms<=300000;++ms){
  double source_hz=rate+(ms<150000?hz_before:hz_after);
  source_phase+=source_hz/1000.;
  while(source_phase>=packet){source_phase-=packet;++generated;
   const uint32_t delay=jitter?uint32_t((generated*17)%5):0;
   arrivals.push_back(ms+delay);
  }
  while(!(outage&&ms>=60000&&ms<60300)&&!arrivals.empty()&&arrivals.front()<=ms){arrivals.pop_front();
   s.decoded+=packet;++s.completed_packets;s.completed_frames+=packet;}
  output_phase+=double(rate)*(1.+applied/1000000.)/1000.;
  auto output=uint64_t(output_phase);output_phase-=double(output);
  const uint64_t available=s.decoded-s.prepared;
  if(output>available){s.prepared+=available;s.zeros+=output-available;}else s.prepared+=output;
  auto inventory=s.decoded-s.prepared;
  mininv=std::min(mininv,double(inventory));maxinv=std::max(maxinv,double(inventory));
  if(inventory>capacity)++over;
  s.now_us=ms*1000;s.healthy=!(fault&&ms==120000);
  auto d=c.observe(s);if(!s.healthy)++suspensions;
  if(!s.healthy&&enabled)assert(c.snapshot().suspended&&!d.update);
  if(d.update){auto q=i2sClockQuantize(uint16_t(hw.pll_n),uint16_t(hw.pll_frac),d.requested_q16,d.lower_q16,d.upper_q16);
   assert(q.representable&&q.applied_q16>=-500*65536&&q.applied_q16<=500*65536);
   assert(std::fabs(double(q.applied_q16)/65536.-applied)<=10.00001);
   assert(c.acknowledge(d,true,q.applied_q16));applied=double(q.applied_q16)/65536.;++decisions;}
  if(ms>=270000){tail+=applied;++tail_count;}
 }
 return {tail/tail_count,applied,mininv,maxinv,s.zeros,over,decisions,suspensions};
}
int main(){
 struct Case{unsigned rate;double a,b;bool jitter,enabled,fault,outage;};
 for(auto v:{Case{96000,-9,-9,false,true,false,false},Case{96000,10,10,false,true,false,false},
             Case{192000,-9,-9,false,true,false,false},Case{192000,10,10,false,true,false,false},
             Case{48000,-5,5,true,true,false,false},Case{44100,-4,4,true,true,false,false},
             Case{96000,-9,10,true,true,true,false},Case{96000,-9,-9,false,false,false,false},
             Case{96000,57.6,57.6,false,true,false,false},Case{96000,-57.6,-57.6,false,true,false,false},
             Case{96000,0,0,true,true,false,true}}){
  auto r=simulate(v.rate,v.a,v.b,v.jitter,v.enabled,v.fault,v.outage);
  if(v.enabled&&!v.fault&&!v.outage&&v.a==v.b&&std::fabs(v.a)<=v.rate*0.0005){assert(std::fabs(r.mean_tail_ppm-v.a/v.rate*1000000.)<15.0);assert(!r.missing&&!r.over_capacity);}
  if(v.enabled&&!v.fault&&!v.outage&&std::fabs(v.a)>v.rate*0.0005){assert(std::fabs(r.mean_tail_ppm)>=490.0 && std::fabs(r.mean_tail_ppm)<=500.0 && r.mean_tail_ppm*v.a>0);}
  printf("rate=%u source_delta_hz=%.1f->%.1f jitter=%d enabled=%d fault=%d outage=%d tail_mean_ppm=%.3f final_ppm=%.3f inventory=%.0f..%.0f missing_frames=%llu over_capacity_ms=%llu decisions=%u fault_samples=%u\n",v.rate,v.a,v.b,v.jitter,v.enabled,v.fault,v.outage,r.mean_tail_ppm,r.final_ppm,r.min_inventory,r.max_inventory,(unsigned long long)r.missing,(unsigned long long)r.over_capacity,r.decisions,r.suspensions);
 }
 for(unsigned rate:{8000u,11025u,12000u,16000u,22050u,24000u,32000u,44100u,48000u,88200u,96000u,176400u,192000u}){
  I2sClock hw;assert(i2sClock(rate,hw));auto low=simulate(rate,-double(rate)/10000.,-double(rate)/10000.,true,true,false,false);
  auto high=simulate(rate,double(rate)/10000.,double(rate)/10000.,true,true,false,false);
  assert(std::fabs(low.mean_tail_ppm+100.)<15. && std::fabs(high.mean_tail_ppm-100.)<15.);
  assert(!low.missing&&!high.missing&&!low.over_capacity&&!high.over_capacity);
  const double step=double(i2sClockCoefficient(uint16_t(hw.pll_n),uint16_t(hw.pll_frac),uint16_t(hw.pll_frac+1)))/65536.;
  const double modeled_kernel=4000000.*(hw.pll_n+hw.pll_frac/8192.)/8.;
  const double programmed_hz=modeled_kernel/(64.*hw.divider);
  printf("matrix rate=%u target_ppm=-100/+100 tail_ppm=%.3f/%.3f pll_step_ppm=%.6f register_rate_hz=%.6f missing=%llu/%llu overflow_ms=%llu/%llu\n",rate,low.mean_tail_ppm,high.mean_tail_ppm,step,programmed_hz,(unsigned long long)low.missing,(unsigned long long)high.missing,(unsigned long long)low.over_capacity,(unsigned long long)high.over_capacity);
 }
}
