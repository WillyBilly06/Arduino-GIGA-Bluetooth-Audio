#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#ifdef CLOCK_RED
// The absent-controller control supplies the declared interface but never trims.
// Behavioral assertions below must fail; this is not a production implementation.
namespace giga_bredr {namespace detail {
struct AudioClockConfig{bool enabled=true;int32_t limit_ppm=500,slew_ppm=10;};
struct AudioClockSample{uint32_t stream_epoch=1,output_epoch=1,now_us=0;uint64_t decoded=0,discarded=0,prepared=0,zeros=0,completed_packets=0,completed_frames=0;uint32_t compressed_units_q16=0;bool running=true,healthy=true,recovering=false;};
struct AudioClockDecision{uint32_t stream_epoch=0,output_epoch=0,serial=0;int32_t requested_q16=0,lower_q16=0,upper_q16=0;bool update=false;};
struct AudioClockSnapshot{int32_t applied_q16=0;uint32_t decisions=0,accepted=0,refused=0,rejected_ack=0,invalid_samples=0;bool tracking=false,learning=false,suspended=false;uint64_t inventory_frames=0,target_frames=0,frames_per_packet_q16=0;};
class AudioClockControl{public:bool begin(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const AudioClockConfig& =AudioClockConfig()){return true;}AudioClockDecision observe(const AudioClockSample&){return{};}bool acknowledge(const AudioClockDecision&,bool,int32_t){return false;}AudioClockSnapshot snapshot()const{return{};}};
}}
#else
#include "detail/AudioClockControl.h"
#endif
using namespace giga_bredr::detail;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"CLOCK_CONTROL line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static constexpr int32_t PPM=65536;
static AudioClockSample sample(uint32_t us,unsigned units=8,uint32_t origin=0){AudioClockSample s;s.now_us=origin+us;s.decoded=1000+uint64_t(us/1000)*48;s.prepared=s.decoded-1000;s.completed_packets=1+us/10000;s.completed_frames=s.completed_packets*480;s.compressed_units_q16=units*65536u;return s;}
static void warm(AudioClockControl& c,uint32_t origin=0){for(uint32_t us=0;us<=2100000;us+=1000){auto d=c.observe(sample(us,8,origin));if(d.update)CHECK(c.acknowledge(d,true,0));}CHECK(c.snapshot().tracking);}
static void sign_and_ack(){AudioClockControl c;CHECK(c.begin(1,1,48000,960,16));warm(c);AudioClockDecision d;
 for(uint32_t us=2101000;us<=4000000;us+=1000){auto x=c.observe(sample(us,10));if(x.update){d=x;break;}}
 CHECK(d.update&&d.requested_q16>0&&d.requested_q16<=10*PPM&&d.lower_q16>=-10*PPM&&d.upper_q16<=10*PPM);
 auto stale=d;stale.serial++;CHECK(!c.acknowledge(stale,true,5*PPM));CHECK(c.snapshot().applied_q16==0);
 CHECK(!c.acknowledge(d,true,11*PPM));CHECK(c.snapshot().applied_q16==0);
 CHECK(c.acknowledge(d,true,8*PPM));CHECK(c.snapshot().applied_q16==8*PPM);CHECK(!c.acknowledge(d,true,8*PPM));
 CHECK(!c.observe(sample(4000000,10)).update); // A pending/finished second is never replayed.
 AudioClockControl low;CHECK(low.begin(1,1,48000,960,16));warm(low);
 for(uint32_t us=2101000;us<=4000000;us+=1000){auto x=low.observe(sample(us,6));if(x.update){CHECK(x.requested_q16<0&&x.requested_q16>=-10*PPM);break;}}
}
static void accounting_and_zero_fragments(){AudioClockControl c;CHECK(c.begin(1,1,48000,960,16));warm(c);auto s=sample(2102000);s.discarded=500;auto d=c.observe(s);(void)d;CHECK(c.snapshot().inventory_frames==500+8*480);
 s=sample(2103000);s.discarded=s.decoded+1;c.observe(s);CHECK(c.snapshot().suspended&&c.snapshot().invalid_samples);
 CHECK(c.begin(2,3,48000,960,16));s=sample(0);s.stream_epoch=2;s.output_epoch=3;s.completed_packets=2;s.completed_frames=480;s.compressed_units_q16=3*65536;CHECK(!c.observe(s).update);CHECK(c.snapshot().frames_per_packet_q16==240*65536u);CHECK(c.snapshot().inventory_frames==1000+3*240);
 s.now_us=1000;s.stream_epoch=1;CHECK(!c.observe(s).update);CHECK(c.snapshot().suspended);
}
static void lifetime_gap_recovery_refusal(){AudioClockControl c;CHECK(c.begin(1,1,48000,960,16));warm(c);auto s=sample(2101000);s.recovering=true;CHECK(!c.observe(s).update&&c.snapshot().suspended);
 for(uint32_t us=2102000;us<4102000;us+=1000)CHECK(!c.observe(sample(us)).update);
 CHECK(c.snapshot().learning||c.snapshot().tracking);
 s=sample(4403000);CHECK(!c.observe(s).update&&c.snapshot().learning); // Bounded gap restarts learning.
 s=sample(4404000);s.healthy=false;CHECK(!c.observe(s).update&&c.snapshot().suspended);
 CHECK(c.begin(1,1,48000,960,16));warm(c);AudioClockDecision d;
 for(uint32_t us=2101000;us<=4000000;us+=1000){auto x=c.observe(sample(us,10));if(x.update){d=x;break;}}
 CHECK(d.update&&c.acknowledge(d,false,123*PPM));CHECK(c.snapshot().applied_q16==0&&c.snapshot().refused==1&&c.snapshot().suspended);
 CHECK(c.begin(2,2,48000,960,16));CHECK(!c.acknowledge(d,true,8*PPM));CHECK(c.snapshot().applied_q16==0);
}
static void limits_wrap_disabled(){AudioClockControl c;CHECK(c.begin(1,1,48000,960,16));warm(c);
 for(uint32_t us=2101000;us<=100000000;us+=1000){auto d=c.observe(sample(us,16));if(d.update){CHECK(d.requested_q16<=500*PPM&&d.requested_q16>=-500*PPM);CHECK(d.upper_q16-c.snapshot().applied_q16<=10*PPM);CHECK(c.acknowledge(d,true,d.requested_q16));}}
 CHECK(c.snapshot().applied_q16<=500*PPM&&c.snapshot().decisions<=100);
 AudioClockControl wrap;CHECK(wrap.begin(1,1,48000,960,16));warm(wrap,0xffff0000u);CHECK(wrap.snapshot().tracking);
 AudioClockConfig disabled;disabled.enabled=false;CHECK(c.begin(1,1,48000,960,16,disabled));for(uint32_t us=0;us<5000000;us+=1000)CHECK(!c.observe(sample(us,16)).update);CHECK(c.snapshot().applied_q16==0);
 AudioClockConfig invalid;invalid.limit_ppm=501;CHECK(!c.begin(1,1,48000,960,16,invalid));invalid.limit_ppm=500;invalid.slew_ppm=11;CHECK(!c.begin(1,1,48000,960,16,invalid));
 CHECK(sizeof(AudioClockControl)<=320);
}
static void arithmetic_extremes(){AudioClockControl c;CHECK(c.begin(1,1,48000,960,16));auto s=sample(0);
 s.decoded=UINT64_MAX-4;s.prepared=s.decoded-1000;s.completed_packets=UINT64_MAX/2+3;s.completed_frames=s.completed_packets/2;s.compressed_units_q16=65536;
 c.observe(s);CHECK(c.snapshot().frames_per_packet_q16==32768);CHECK(c.snapshot().inventory_frames==1000);
}
int main(){sign_and_ack();accounting_and_zero_fragments();lifetime_gap_recovery_refusal();limits_wrap_disabled();arithmetic_extremes();printf("AUDIO_CLOCK_CONTROL_PASS checks=%u bytes=%zu\n",checks,sizeof(AudioClockControl));}
