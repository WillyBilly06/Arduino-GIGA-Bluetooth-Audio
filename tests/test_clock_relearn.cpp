#include "detail/AudioClockControl.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
using namespace giga_bredr::detail;
constexpr int32_t PPM=65536;
static AudioClockSample base(uint32_t now=0){AudioClockSample s;s.now_us=now;s.decoded=96000000;s.prepared=s.decoded-1000;s.completed_packets=100000;s.completed_frames=96000000;s.compressed_units_q16=4u*65536u;return s;}
static void packet(AudioClockSample& s,unsigned frames){++s.completed_packets;s.completed_frames+=frames;s.decoded+=frames;s.prepared+=frames;}
static void learn(AudioClockControl& c,AudioClockSample& s,unsigned frames){for(unsigned i=0;i<2500;++i){s.now_us+=1000;if(i%10==0)packet(s,frames);auto d=c.observe(s);if(d.update)assert(c.acknowledge(d,true,3*PPM));}assert(c.snapshot().tracking);}
static void recovery(){AudioClockControl c;assert(c.begin(1,1,96000,7680,28));auto s=base();assert(!c.observe(s).update);assert(c.snapshot().frames_per_packet_q16==0);learn(c,s,960);auto before=c.snapshot();assert(before.frames_per_packet_q16==960u*65536&&before.applied_q16==3*PPM&&before.accepted>0);
 s.now_us+=1000;s.healthy=false;assert(!c.observe(s).update);auto fault=c.snapshot();assert(fault.suspended&&!fault.tracking&&!fault.learning&&fault.frames_per_packet_q16==0&&fault.inventory_frames==0&&fault.target_frames==0&&fault.accepted==before.accepted&&fault.decisions==before.decisions&&fault.applied_q16==before.applied_q16);
 s.healthy=true;s.now_us+=1000;assert(!c.observe(s).update);assert(c.snapshot().learning&&c.snapshot().frames_per_packet_q16==0);
 s.now_us+=1000;packet(s,0);assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);
 s.now_us+=1000;packet(s,480);assert(!c.observe(s).update);assert(c.snapshot().frames_per_packet_q16==240u*65536);assert(c.snapshot().inventory_frames==1000+4*240);assert(c.snapshot().applied_q16==3*PPM);
 for(unsigned i=0;i<1900;++i){s.now_us+=1000;if(i%10==0)packet(s,240);assert(!c.observe(s).update);}assert(c.snapshot().learning&&!c.snapshot().tracking);learn(c,s,240);assert(c.snapshot().frames_per_packet_q16==240u*65536);
 // Missing samples and recovering state both invalidate the new cohort.
 auto accepted=c.snapshot().accepted;++s.zeros;s.now_us+=1000;assert(!c.observe(s).update);assert(c.snapshot().suspended&&c.snapshot().frames_per_packet_q16==0&&c.snapshot().accepted==accepted&&c.snapshot().applied_q16==3*PPM);
 s.now_us+=1000;s.recovering=true;packet(s,0);assert(!c.observe(s).update&&c.snapshot().suspended);s.recovering=false;s.now_us+=1000;assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,120);c.observe(s);assert(c.snapshot().frames_per_packet_q16==120u*65536);
}
static void identity(){AudioClockControl c;assert(c.begin(1,1,48000,960,16));auto s=base(0xffff0000u);c.observe(s);learn(c,s,480);assert(c.snapshot().applied_q16==3*PPM);auto before=c.snapshot();auto foreign=s;foreign.stream_epoch=2;foreign.completed_packets=2;foreign.completed_frames=480;foreign.now_us+=1000;assert(!c.observe(foreign).update);assert(c.snapshot().suspended&&c.snapshot().frames_per_packet_q16==0&&c.snapshot().applied_q16==before.applied_q16);
 // Foreign counters cannot seed a returning owner, even when smaller.
 s.now_us+=2000;assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,240);c.observe(s);assert(c.snapshot().frames_per_packet_q16==240u*65536);
 auto bad=s;bad.now_us+=1000;--bad.completed_packets;assert(!c.observe(bad).update&&c.snapshot().frames_per_packet_q16==0&&c.snapshot().invalid_samples>=2);s.now_us+=2000;assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,320);c.observe(s);assert(c.snapshot().frames_per_packet_q16==320u*65536);
 // A long scheduling gap also retires the estimator before learning again.
 s.now_us+=300000;assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,160);c.observe(s);assert(c.snapshot().frames_per_packet_q16==160u*65536);
 auto otherOutput=s;otherOutput.output_epoch=2;otherOutput.now_us+=1000;c.observe(otherOutput);assert(c.snapshot().suspended&&c.snapshot().frames_per_packet_q16==0);s.now_us+=2000;c.observe(s);assert(c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,200);c.observe(s);assert(c.snapshot().frames_per_packet_q16==200u*65536);
}
static void acquiring_gap(){
 AudioClockControl c;assert(c.begin(1,1,48000,960,16));auto s=base();assert(!c.observe(s).update);
 s.now_us+=1000;packet(s,0);assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);
 // No positive estimate yet: a long gap still invalidates zero-frame fragments.
 s.now_us+=300000;packet(s,480);assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==0);
 s.now_us+=1000;packet(s,240);assert(!c.observe(s).update&&c.snapshot().frames_per_packet_q16==240u*65536);
 // A fresh packet after a short unhealthy-to-healthy transition is credited.
 s.healthy=false;s.now_us+=1000;c.observe(s);s.healthy=true;s.now_us+=1000;packet(s,300);c.observe(s);assert(c.snapshot().frames_per_packet_q16==300u*65536);
 // But a long gap after fault uses the first returned sample as baseline.
 s.healthy=false;s.now_us+=1000;c.observe(s);s.healthy=true;s.now_us+=300000;packet(s,900);c.observe(s);assert(c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,200);c.observe(s);assert(c.snapshot().frames_per_packet_q16==200u*65536);
}
static void gap_boundary(){
 AudioClockControl c;assert(c.begin(1,1,48000,960,16));auto s=base();c.observe(s);s.now_us+=250000;packet(s,240);c.observe(s);assert(c.snapshot().frames_per_packet_q16==240u*65536);
 s.now_us+=250001;packet(s,240);c.observe(s);assert(c.snapshot().frames_per_packet_q16==0&&c.snapshot().learning);
}
static void counter_rollover(){
 AudioClockControl c;assert(c.begin(1,1,48000,960,16));auto s=base();s.completed_packets=UINT64_MAX-2;s.completed_frames=1000;c.observe(s);s.now_us+=1000;packet(s,20);c.observe(s);assert(c.snapshot().frames_per_packet_q16==20u*65536);
 s.now_us+=1000;s.completed_packets=0;assert(!c.observe(s).update&&c.snapshot().suspended&&c.snapshot().frames_per_packet_q16==0&&c.snapshot().invalid_samples==1);
 s.now_us+=1000;c.observe(s);assert(c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,240);c.observe(s);assert(c.snapshot().frames_per_packet_q16==240u*65536);
}
static void refused(){AudioClockControl c;assert(c.begin(1,1,48000,960,16));auto s=base();c.observe(s);AudioClockDecision old{};for(unsigned i=0;i<2500&&!old.update;++i){s.now_us+=1000;if(i%10==0)packet(s,480);old=c.observe(s);}assert(old.update);assert(c.acknowledge(old,true,4*PPM));AudioClockDecision d{};for(unsigned i=0;i<2000&&!d.update;++i){s.now_us+=1000;if(i%10==0)packet(s,480);d=c.observe(s);}assert(d.update);assert(c.acknowledge(d,false,123*PPM));auto snap=c.snapshot();assert(snap.applied_q16==4*PPM&&snap.refused==1&&snap.frames_per_packet_q16==0&&snap.suspended);assert(!c.acknowledge(d,true,0));assert(c.snapshot().rejected_ack==snap.rejected_ack+1);s.now_us+=1000;c.observe(s);assert(c.snapshot().frames_per_packet_q16==0);s.now_us+=1000;packet(s,160);c.observe(s);assert(c.snapshot().frames_per_packet_q16==160u*65536&&c.snapshot().applied_q16==4*PPM);
 AudioClockDecision fresh{};for(unsigned i=0;i<2500&&!fresh.update;++i){s.now_us+=1000;if(i%10==0)packet(s,160);fresh=c.observe(s);}assert(fresh.update&&fresh.serial!=d.serial);const auto before=c.snapshot();assert(!c.acknowledge(d,true,0)&&c.snapshot().applied_q16==before.applied_q16);assert(c.acknowledge(fresh,true,4*PPM));}
int main(){recovery();identity();acquiring_gap();gap_boundary();counter_rollover();refused();std::puts("PASS fresh cohorts, 960-to-240, zero frames, recovery, wrap, epoch, invalid counters, acquisition gap and refused/stale ACK; synthetic actual controller");}
