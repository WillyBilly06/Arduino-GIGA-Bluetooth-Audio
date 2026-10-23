#pragma once
#include <stdint.h>
#include <limits.h>
namespace giga_bredr { namespace detail {
struct AudioClockConfig { bool enabled=true;int32_t limit_ppm=500,slew_ppm=10; };
struct AudioClockSample {
 uint32_t stream_epoch=1,output_epoch=1,now_us=0;
 uint64_t decoded=0,discarded=0,prepared=0,zeros=0,completed_packets=0,completed_frames=0;
 uint32_t compressed_units_q16=0;
 bool running=true,healthy=true,recovering=false;
};
struct AudioClockDecision {
 uint32_t stream_epoch=0,output_epoch=0,serial=0;
 int32_t requested_q16=0,lower_q16=0,upper_q16=0;
 bool update=false;
};
struct AudioClockSnapshot {
 int32_t applied_q16=0;
 uint32_t decisions=0,accepted=0,refused=0,rejected_ack=0,invalid_samples=0;
 bool tracking=false,learning=false,suspended=false;
 uint64_t inventory_frames=0,target_frames=0,frames_per_packet_q16=0;
};
// Serialized worker-only scalar control. PCM, payloads and the actuator are
// external. No allocation, catch-up loop, radio callback or clock-domain mix.
class AudioClockControl {
public:
 bool begin(uint32_t stream,uint32_t output,uint32_t rate,uint32_t pcmCapacity,
            uint32_t queueCapacity,const AudioClockConfig& config=AudioClockConfig()) {
  *this=AudioClockControl{};config_=config;
  if(!stream||!output||!rate||rate>192000||!pcmCapacity||!queueCapacity||queueCapacity>64||
     config.limit_ppm<0||config.limit_ppm>500||config.slew_ppm<0||config.slew_ppm>10)return false;
  stream_=stream;output_=output;rate_=rate;pcmCapacity_=pcmCapacity;queueCapacity_=queueCapacity;active_=true;return true;
 }
 AudioClockSnapshot snapshot()const{return stats_;}
 AudioClockDecision observe(const AudioClockSample& s) {
  if(!active_||!config_.enabled)return {};
  const bool counters=s.discarded<=s.decoded && s.prepared<=s.decoded-s.discarded && s.completed_frames<=s.decoded;
  const bool forward=!haveCounters_ || (s.decoded>=lastA_&&s.discarded>=lastD_&&s.prepared>=lastP_&&
                   s.zeros>=lastZ_&&s.completed_packets>=lastF_&&s.completed_frames>=lastG_);
  const bool same=s.stream_epoch==stream_&&s.output_epoch==output_;
  const bool missing=haveCounters_&&s.zeros!=lastZ_;
  if(!same||!counters||!forward){++stats_.invalid_samples;suspend();remember(s);return {};}
  if(!s.running||!s.healthy||s.recovering||missing){suspend();remember(s);return {};}
  if(!stats_.frames_per_packet_q16&&s.completed_packets&&s.completed_frames)
   stats_.frames_per_packet_q16=ratio(s.completed_frames,s.completed_packets);
  if(!stats_.frames_per_packet_q16){remember(s);return {};}
  const uint64_t units=s.compressed_units_q16;
  const uint64_t f=stats_.frames_per_packet_q16;
  if(units&&f>UINT64_MAX/units){++stats_.invalid_samples;suspend();remember(s);return {};}
  const uint64_t compressed=(f*units)>>32;
  const uint64_t exact=s.decoded-s.discarded-s.prepared;
  if(exact>UINT32_MAX||compressed>UINT32_MAX-exact){++stats_.invalid_samples;suspend();remember(s);return {};}
  stats_.inventory_frames=exact+compressed;
  if(!haveTime_){
   haveTime_=true;lastTime_=windowStart_=s.now_us;lastInventory_=stats_.inventory_frames;
   windowF_=s.completed_packets;windowG_=s.completed_frames;stats_.learning=true;stats_.tracking=stats_.suspended=false;
   remember(s);return {};
  }
  const uint32_t dt=s.now_us-lastTime_;
  if(dt>250000u){suspend();remember(s);stats_.learning=true;stats_.suspended=false;return {};}
  area_+=lastInventory_*dt;elapsed_+=dt;lastInventory_=stats_.inventory_frames;lastTime_=s.now_us;
  remember(s);
  if(elapsed_<1000000u)return {};
  const uint64_t mean=q16mean(area_,elapsed_);
  if(stats_.learning){
   learnArea_+=area_;learnElapsed_+=elapsed_;
   if(learnElapsed_>=2000000u){
    uint64_t target=q16mean(learnArea_,learnElapsed_);
    const uint64_t measured=target;
    const uint64_t capacity=targetCeilingQ16();
    if(target>capacity)target=capacity;
    targetQ16_=target;filterQ16_=queueCapacity_>=4?measured:target;stats_.target_frames=target>>16;
    stats_.tracking=true;stats_.learning=false;previousError_=0;
   }
  }else if(stats_.tracking){
   filterQ16_+= (int64_t(mean)-int64_t(filterQ16_))/4;
  }
  // Batch means include valid zero-frame entries. Never update from one
  // packet in a hot loop or give a retained fragment another decoded credit.
  const uint64_t packetDelta=s.completed_packets-windowF_,frameDelta=s.completed_frames-windowG_;
  if(packetDelta){const uint64_t next=ratio(frameDelta,packetDelta);
   stats_.frames_per_packet_q16+= (int64_t(next)-int64_t(stats_.frames_per_packet_q16))/8;}
  // A packetization contraction can reduce the estimated usable capacity
  // without a lifecycle/error event. Never retain an unreachable old target.
  if(stats_.tracking&&queueCapacity_>=4){const uint64_t ceiling=targetCeilingQ16();
   if(targetQ16_>ceiling){targetQ16_=ceiling;stats_.target_frames=ceiling>>16;}}
  windowF_=s.completed_packets;windowG_=s.completed_frames;
  area_=elapsed_=0;windowStart_=s.now_us;
  if(!stats_.tracking||pending_.update)return {};
  if(haveDecision_&&uint32_t(s.now_us-lastDecision_)<1000000u)return {};
  // Error is nominal microseconds, clipped before gain arithmetic. Incremental
  // PI uses actual accepted quantized trim as its integral/anti-windup state.
  int64_t delta=int64_t(filterQ16_)-int64_t(targetQ16_);
  const int64_t maximumDelta=int64_t(rate_)*65536/10;
  if(delta>maximumDelta)delta=maximumDelta;if(delta< -maximumDelta)delta=-maximumDelta;
  int64_t error=delta*1000000/(int64_t(rate_)*65536);
  if(error>100000)error=100000;if(error< -100000)error=-100000;
  // Kp=.2 ppm/us, Ki=.01 ppm/us per1s decision, four-second occupancy filter.
  int64_t request=int64_t(stats_.applied_q16)+(error-previousError_)*65536/5+error*65536/100;
  const int32_t limit=config_.limit_ppm*65536,step=config_.slew_ppm*65536;
  const int32_t lower=stats_.applied_q16-step< -limit?-limit:stats_.applied_q16-step;
  const int32_t upper=stats_.applied_q16+step>limit?limit:stats_.applied_q16+step;
  if(request<lower)request=lower;if(request>upper)request=upper;
  previousError_=error;lastDecision_=s.now_us;haveDecision_=true;
  ++serial_;if(!serial_)++serial_;
  pending_={stream_,output_,serial_,int32_t(request),lower,upper,true};++stats_.decisions;return pending_;
 }
 bool acknowledge(const AudioClockDecision& d,bool accepted,int32_t applied_q16) {
  if(!pending_.update||!d.update||d.stream_epoch!=stream_||d.output_epoch!=output_||d.serial!=pending_.serial){++stats_.rejected_ack;return false;}
  if(accepted&&(applied_q16<pending_.lower_q16||applied_q16>pending_.upper_q16)){++stats_.rejected_ack;return false;}
  pending_.update=false;
  if(!accepted){++stats_.refused;suspend();return true;}
  stats_.applied_q16=applied_q16;++stats_.accepted;return true;
 }
private:
 uint64_t targetCeilingQ16()const{
  // Estimated inventory headroom, not guaranteed free physical packet slots:
  // PCM need not be full and frames-per-packet is an average. Tiny queues
  // retain the exact prior policy; this does not promise burst absorption.
  const uint32_t reserve=queueCapacity_>=4?(queueCapacity_+3)/4:0;
  return uint64_t(pcmCapacity_)*65536u+stats_.frames_per_packet_q16*(queueCapacity_-reserve);
 }
 static uint64_t q16mean(uint64_t area,uint64_t time){return (area/time)*65536+(area%time)*65536/time;}
 static uint64_t ratio(uint64_t frames,uint64_t packets){
  const uint64_t whole=frames/packets;if(whole>UINT32_MAX)return uint64_t(UINT32_MAX)*65536;
  uint64_t remainder=frames%packets,fraction=0;
  // Fixed16-bit long division avoids overflow of remainder*65536.
  for(unsigned bit=0;bit<16;++bit){fraction<<=1;
   if(remainder>=packets-remainder){remainder-=packets-remainder;fraction|=1;}
   else remainder+=remainder;
  }
  return whole*65536+fraction;
 }
 void suspend(){stats_.tracking=stats_.learning=false;stats_.suspended=true;haveTime_=false;area_=elapsed_=learnArea_=learnElapsed_=0;previousError_=0;pending_.update=false;}
 void remember(const AudioClockSample& s){lastA_=s.decoded;lastD_=s.discarded;lastP_=s.prepared;lastZ_=s.zeros;lastF_=s.completed_packets;lastG_=s.completed_frames;haveCounters_=true;}
 AudioClockConfig config_;AudioClockSnapshot stats_;AudioClockDecision pending_;
 uint32_t stream_=0,output_=0,rate_=0,pcmCapacity_=0,queueCapacity_=0,lastTime_=0,windowStart_=0,lastDecision_=0,serial_=0;
 uint64_t lastA_=0,lastD_=0,lastP_=0,lastZ_=0,lastF_=0,lastG_=0,windowF_=0,windowG_=0;
 uint64_t area_=0,elapsed_=0,learnArea_=0,learnElapsed_=0,lastInventory_=0,targetQ16_=0,filterQ16_=0;
 int64_t previousError_=0;
 bool active_=false,haveCounters_=false,haveTime_=false,haveDecision_=false;
};
}}
