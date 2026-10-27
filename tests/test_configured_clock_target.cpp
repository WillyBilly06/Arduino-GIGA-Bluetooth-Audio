#include "detail/AudioClockControl.h"
using namespace giga_bredr::detail;

constexpr AudioClockSample targetSample(uint32_t us,uint32_t inventory=8448) {
    AudioClockSample s;
    s.now_us=us;
    s.decoded=100000u+uint64_t(us)*192000u/1000000u;
    s.prepared=s.decoded-inventory;
    s.completed_packets=1u+us/5000u;
    s.completed_frames=s.completed_packets*960u;
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
static_assert(legacyMeanAndAck(),"Legacy controller must learn the healthy mean and accept current ACK");
static_assert(legacyDirectionAndStaleAck(),"Legacy correction direction and stale ACK must remain valid");
static_assert(sizeof(AudioClockControl)<=320,"Private controller size bound");
int main() { return 0; }
