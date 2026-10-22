#pragma once
#include "I2sBuffer.h"
namespace giga_bredr { namespace detail {
// Consumer-owned after DMA starts. Begin/end require the DMA IRQ owner stopped.
// Only scalar state/first transitions: PCM remains in the existing I2sRing.
struct PcmRecoveryTransition {
    uint32_t at_us=0, queued=0, real=0, silent=0;
    uint8_t kind=0; // 1 starvation,2 resume,3 stopped while holding.
};
struct PcmRecoverySnapshot {
    uint32_t epoch=0, threshold=0, half_frames=0, entries=0, resumes=0;
    uint32_t record_count=0, records_not_retained=0;
    uint64_t starved_frames=0, hold_frames=0, hold_refills=0;
    uint64_t current_hold_frames=0, maximum_hold_frames=0;
    bool active=false, recovering=false;
    PcmRecoveryTransition records[8];
};
class PcmRecovery {
public:
    bool begin(uint32_t epoch,uint32_t capacity,uint32_t half_frames) {
        stats_={};
        if(!epoch || !half_frames || half_frames>capacity/2) return false;
        uint64_t threshold=uint64_t(capacity)/2+uint64_t(half_frames)*2;
        stats_.epoch=epoch; stats_.half_frames=half_frames;
        stats_.threshold=uint32_t(threshold>capacity?capacity:threshold);
        stats_.active=true; return true;
    }
    void end(uint32_t at_us,uint32_t queued) {
        if(stats_.active && stats_.recovering) record(3,at_us,queued,0,0);
        stats_.active=false; stats_.recovering=false;
    }
    size_t refill(I2sRing& ring,int32_t* dma,uint32_t at_us) {
        if(!stats_.active || !dma) return 0;
        const uint32_t queued=uint32_t(ring.queued());
        const uint32_t frames=stats_.half_frames;
        if(stats_.recovering) {
            if(queued<stats_.threshold) {
                memset(dma,0,size_t(frames)*8);
                ++stats_.hold_refills; stats_.hold_frames+=frames;
                stats_.current_hold_frames+=frames;
                if(stats_.current_hold_frames>stats_.maximum_hold_frames)
                    stats_.maximum_hold_frames=stats_.current_hold_frames;
                return 0; // Preserve queued PCM/read cursor until the reserve is rebuilt.
            }
            stats_.recovering=false; ++stats_.resumes;
            stats_.current_hold_frames=0;
            record(2,at_us,queued,frames,0);
        }
        const uint32_t real=uint32_t(ring.fill(dma,frames));
        if(real<frames) {
            stats_.recovering=true; ++stats_.entries;
            stats_.starved_frames+=frames-real;
            stats_.current_hold_frames=0;
            record(1,at_us,queued,real,frames-real);
        }
        return real;
    }
    PcmRecoverySnapshot snapshot() const { return stats_; }
private:
    void record(uint8_t kind,uint32_t at,uint32_t queued,uint32_t real,uint32_t silent) {
        if(stats_.record_count<8) stats_.records[stats_.record_count++]={at,queued,real,silent,kind};
        else ++stats_.records_not_retained;
    }
    PcmRecoverySnapshot stats_;
};
}}
