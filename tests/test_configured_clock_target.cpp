#include "detail/AudioClockControl.h"
#include "Types.h"
using namespace giga_bredr::detail;

constexpr AudioClockSample targetSample(uint32_t us,uint32_t inventory=8448,uint32_t rate=192000) {
    AudioClockSample s;
    s.now_us=us;
    s.decoded=100000u+uint64_t(us)*rate/1000000u;
    s.prepared=s.decoded-inventory;
    s.completed_packets=1u+us/5000u;
    s.completed_frames=s.completed_packets*(rate/200u);
    return s;
}
constexpr bool legacyMeanAndAck() {
    AudioClockControl c;
    if(!c.begin(1,1,192000,13440,28)) return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto d=c.observe(targetSample(us));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    return c.snapshot().tracking&&c.snapshot().target_frames==8448;
}
constexpr bool legacyDirectionAndStaleAck() {
    AudioClockControl c;
    if(!c.begin(1,1,192000,13440,28)) return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto d=c.observe(targetSample(us));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    for(uint32_t us=2200000;us<=4100000;us+=100000) {
        auto d=c.observe(targetSample(us,10000));
        if(!d.update) continue;
        auto stale=d; ++stale.serial;
        return d.requested_q16>0&&d.requested_q16<=10*65536&&
            !c.acknowledge(stale,true,0)&&c.acknowledge(d,true,d.requested_q16);
    }
    return false;
}
// The legacy fallback deliberately lets the old controller compile and fail
// the behavior assertion, instead of failing merely because an overload is absent.
template<class C> constexpr auto beginTarget(C& c,uint32_t rate,uint32_t ring,uint32_t target,int,uint32_t queue=28)
    -> decltype(c.begin(1,1,rate,ring,queue,AudioClockConfig{},target)) {
    return c.begin(1,1,rate,ring,queue,AudioClockConfig{},target);
}
template<class C> constexpr bool beginTarget(C& c,uint32_t rate,uint32_t ring,uint32_t,long,uint32_t queue=28) {
    return c.begin(1,1,rate,ring,queue);
}
constexpr bool configuredRate(uint32_t rate,uint32_t ring,uint32_t target,uint32_t half) {
    AudioClockControl c;
    if(!beginTarget(c,rate,ring,target,0)) return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto d=c.observe(targetSample(us,target-half,rate));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    auto s=c.snapshot();
    return s.tracking&&s.target_frames==target&&s.applied_q16<0&&s.applied_q16>=-10*65536;
}
constexpr bool configuredSurvivesRecovery() {
    AudioClockControl c;
    if(!beginTarget(c,192000,13440,11136,0)) return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto d=c.observe(targetSample(us));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    auto stopped=targetSample(2200000);stopped.recovering=true;
    if(c.observe(stopped).update||!c.snapshot().suspended) return false;
    for(uint32_t us=2300000;us<=4500000;us+=100000) {
        auto d=c.observe(targetSample(us,13000));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    return c.snapshot().tracking&&c.snapshot().target_frames==11136;
}
constexpr bool configuredBounds() {
    AudioClockControl zero,large,legacy;
    return !beginTarget(zero,192000,13440,0,0)&&
        !beginTarget(large,192000,13440,13441,0)&&
        beginTarget(legacy,192000,13440,UINT32_MAX,0);
}
constexpr bool configuredSmallQueuesAndCompressed() {
    for(uint32_t queue=1;queue<=3;++queue) {
        AudioClockControl c;
        if(!beginTarget(c,192000,13440,11136,0,queue)) return false;
        for(uint32_t us=0;us<=2100000;us+=100000) {
            auto d=c.observe(targetSample(us,10000));
            if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
        }
        if(c.snapshot().target_frames!=11136||c.snapshot().applied_q16>=0) return false;
    }
    AudioClockControl c;
    if(!beginTarget(c,192000,13440,11136,0)) return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto s=targetSample(us,11136-1920);s.compressed_units_q16=2*65536;
        auto d=c.observe(s);
        if(d.update&&!c.acknowledge(d,true,d.requested_q16)) return false;
    }
    return c.snapshot().inventory_frames==11136&&c.snapshot().target_frames==11136&&c.snapshot().applied_q16==0;
}
constexpr bool configuredDisabledAndRefusal() {
    AudioClockConfig disabled;disabled.enabled=false;
    AudioClockControl off;
    if(!off.begin(1,1,192000,13440,28,disabled,11136)) return false;
    for(uint32_t us=0;us<=3000000;us+=100000)if(off.observe(targetSample(us)).update)return false;
    if(off.snapshot().tracking||off.snapshot().decisions) return false;
    AudioClockControl c;
    if(!beginTarget(c,192000,13440,11136,0)) return false;
    for(uint32_t us=0;us<=2000000;us+=100000) {
        auto d=c.observe(targetSample(us));
        if(!d.update) continue;
        auto stale=d;++stale.serial;
        if(c.acknowledge(stale,true,0)||c.acknowledge(d,true,11*65536)||!c.acknowledge(d,false,0))return false;
    }
    if(!c.snapshot().suspended||c.snapshot().refused!=1)return false;
    // Refusal recovery must relearn filtering without replacing the target.
    c.observe(targetSample(2400000,13000));
    for(uint32_t us=2500000;us<=4700000;us+=100000) {
        auto d=c.observe(targetSample(us,13000));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16))return false;
    }
    return c.snapshot().tracking&&c.snapshot().target_frames==11136;
}
constexpr bool configuredObservationGap() {
    AudioClockControl c;
    if(!beginTarget(c,192000,13440,11136,0))return false;
    for(uint32_t us=0;us<=2100000;us+=100000) {
        auto d=c.observe(targetSample(us));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16))return false;
    }
    if(c.observe(targetSample(2500000,13000)).update||!c.snapshot().learning||c.snapshot().tracking)return false;
    for(uint32_t us=2600000;us<=4800000;us+=100000) {
        auto d=c.observe(targetSample(us,13000));
        if(d.update&&!c.acknowledge(d,true,d.requested_q16))return false;
    }
    return c.snapshot().tracking&&c.snapshot().target_frames==11136;
}
constexpr bool configuredLongBounds() {
    AudioClockControl c;
    if(!beginTarget(c,192000,13440,11136,0))return false;
    int32_t prior=0;
    for(uint32_t us=0;us<=100000000;us+=100000) {
        auto d=c.observe(targetSample(us,12672));
        if(!d.update)continue;
        if(d.requested_q16>500*65536||d.requested_q16< -500*65536||
            d.requested_q16-prior>10*65536||prior-d.requested_q16>10*65536||
            !c.acknowledge(d,true,d.requested_q16))return false;
        prior=d.requested_q16;
    }
    return c.snapshot().target_frames==11136&&c.snapshot().decisions<=100;
}
constexpr bool outputGeometryHandoff() {
    giga_bredr::OutputStats s;s.sample_rate=48000;s.initialized=s.priming=true;
    s.dma_half_frames=48;s.buffer_capacity_frames=3360;s.startup_frames=2880;
    if(realPcmClockTarget(s,48000,48,3360)!=2784)return false;
    auto bad=s;bad.initialized=false;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.priming=false;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.running=true;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.sample_rate=44100;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.dma_half_frames=44;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.buffer_capacity_frames=3356;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.startup_frames=96;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.startup_frames=95;if(realPcmClockTarget(bad,48000,48,3360))return false;
    bad=s;bad.startup_frames=3361;if(realPcmClockTarget(bad,48000,48,3360))return false;
    return !realPcmClockTarget(s,48000,0,3360)&&!realPcmClockTarget(s,0,48,3360)&&
        !realPcmClockTarget(s,48000,UINT32_MAX,3360);
}
static_assert(legacyMeanAndAck(),"Legacy controller must learn the healthy mean and accept current ACK");
static_assert(legacyDirectionAndStaleAck(),"Legacy correction direction and stale ACK must remain valid");
static_assert(sizeof(AudioClockControl)<=320,"Private controller size bound");
static_assert(sizeof(AudioClockControl)==272,"Configured selection must fit the existing ARM controller layout");
static_assert(configuredRate(192000,13440,11136,192),"LHDC configured target must replace the early mean");
static_assert(configuredRate(96000,5760,4608,96),"LDAC configured target and correction direction");
static_assert(configuredRate(44100,2648,2112,48),"AAC rounded actual geometry target");
static_assert(configuredRate(44100,3044,2508,48),"HE-AAC rounded actual geometry target");
static_assert(configuredRate(48000,2400,1824,48),"Opus configured target");
static_assert(configuredRate(8000,1200,1104,8),"Low-rate accepted geometry target");
static_assert(configuredSurvivesRecovery(),"Recovery must not relearn a different configured target");
static_assert(configuredBounds(),"Thin zero target and oversized target must reject optional tracking");
static_assert(configuredSmallQueuesAndCompressed(),"Configured tiny queues and compressed frames use one inventory credit");
static_assert(configuredDisabledAndRefusal(),"Disabled, stale/invalid ACK, refusal and gap preserve the contract");
static_assert(configuredLongBounds(),"Configured mode retains long-run slew and absolute limits");
static_assert(configuredObservationGap(),"A running >250ms observation gap preserves the configured target");
static_assert(outputGeometryHandoff(),"Actual OutputStats handoff validates every geometry/lifecycle input");
int main() { return 0; }
