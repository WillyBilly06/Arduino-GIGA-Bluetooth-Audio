#pragma once
#include "I2sBuffer.h"
namespace giga_bredr { namespace detail {
// Consumer-owned after DMA starts. Begin/end require the DMA IRQ owner stopped.
// Only scalar state/first transitions: PCM remains in the existing I2sRing.
struct PcmRecoveryTransition {
    uint32_t at_us=0, queued=0, real=0, silent=0;
    uint8_t kind=0; // 1 starvation,2 resume,3 stopped while holding.
    // silent counts absent input frames, including a possible nonzero decay.
};
struct PcmRecoverySnapshot {
    uint32_t epoch=0, threshold=0, half_frames=0, entries=0, resumes=0;
    uint32_t record_count=0, records_not_retained=0;
    uint64_t starved_frames=0, hold_frames=0, hold_refills=0;
    uint64_t current_hold_frames=0, maximum_hold_frames=0;
    bool active=false, recovering=false;
    PcmRecoveryTransition records[8];
    uint32_t ramp_frames=0;
    // Counts are stereo PCM frames, not interleaved sample words. Original
    // starved/hold totals count absent input, not necessarily literal zeros.
    uint64_t synthesized_frames=0; // Absent input, prepared L or R nonzero.
    uint64_t zero_frames=0;        // Absent input, both prepared words zero.
    uint64_t mixed_frames=0;       // Real input processed by resume envelope.
};
class PcmRecovery {
public:
    bool begin(uint32_t epoch,uint32_t capacity,uint32_t half_frames) {
        stats_={};
        seeded_=missing_=false; progress_=0;
        last_[0]=last_[1]=anchor_[0]=anchor_[1]=0;
        if(!epoch || !half_frames || half_frames>capacity/2) return false;
        uint64_t threshold=uint64_t(capacity)/2+uint64_t(half_frames)*2;
        stats_.epoch=epoch; stats_.half_frames=half_frames;
        stats_.threshold=uint32_t(threshold>capacity?capacity:threshold);
        // The actual DMA geometry rounds ~1 ms to four frames. Use its nearest
        // power of two (ties up), bounded to 2..256 frames, for shift-only gain.
        shift_=1;
        while(shift_<8 && (1u<<(shift_+1))<=half_frames) ++shift_;
        if(shift_<8 && half_frames>=(3u<<(shift_-1))) ++shift_;
        stats_.ramp_frames=1u<<shift_;
        stats_.active=true; return true;
    }
    // Before starting DMA, seed from the END of BOTH primed halves: the other
    // half will play before the first completed half is refilled. No start fade.
    void seedPrimed(const int32_t* dma,uint32_t frames) {
        if(!stats_.active || seeded_ || !dma || frames!=stats_.half_frames*2) return;
        last_[0]=dma[2*frames-2]; last_[1]=dma[2*frames-1];
        seeded_=true; missing_=false; progress_=stats_.ramp_frames;
    }
    void end(uint32_t at_us,uint32_t queued) {
        if(stats_.active && stats_.recovering) record(3,at_us,queued,0,0);
        stats_.active=false; stats_.recovering=false;
        seeded_=false;
    }
    size_t refill(I2sRing& ring,int32_t* dma,uint32_t at_us) {
        if(!stats_.active || !dma) return 0;
        const uint32_t queued=uint32_t(ring.queued());
        const uint32_t frames=stats_.half_frames;
        if(stats_.recovering) {
            if(queued<stats_.threshold) {
                memset(dma,0,size_t(frames)*8);
                shape(dma,frames,true);
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
        shape(dma,real,false);
        shape(dma+size_t(real)*2,frames-real,true);
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
    // Symmetric truncation toward zero; all arithmetic stays within int64_t
    // even for INT32_MIN -> INT32_MAX. No signed negative shift or division.
    int32_t blend(int32_t target,int32_t anchor,uint32_t gain) const {
        const int64_t sum=int64_t(target)*gain+
            int64_t(anchor)*(stats_.ramp_frames-gain);
        return int32_t(sum<0?-((-sum)>>shift_):(sum>>shift_));
    }
    void shape(int32_t* pcm,uint32_t frames,bool missing) {
        if(!frames) return;
        if(!seeded_) { if(missing) stats_.zero_frames+=frames; return; }
        if(missing!=missing_) {
            missing_=missing; progress_=0;
            anchor_[0]=last_[0]; anchor_[1]=last_[1];
        }
        uint32_t n=stats_.ramp_frames-progress_;
        if(n>frames) n=frames;
        uint32_t synthesized=0;
        for(uint32_t i=0;i<n;++i) {
            const uint32_t gain=++progress_;
            pcm[2*i]=blend(pcm[2*i],anchor_[0],gain);
            pcm[2*i+1]=blend(pcm[2*i+1],anchor_[1],gain);
            if(missing && (pcm[2*i] || pcm[2*i+1])) ++synthesized;
        }
        if(missing) {
            stats_.synthesized_frames+=synthesized;
            stats_.zero_frames+=frames-synthesized;
        }
        else stats_.mixed_frames+=n;
        // Uninterrupted real PCM takes no per-sample envelope loop.
        last_[0]=pcm[2*frames-2]; last_[1]=pcm[2*frames-1];
    }
    void record(uint8_t kind,uint32_t at,uint32_t queued,uint32_t real,uint32_t silent) {
        if(stats_.record_count<8) stats_.records[stats_.record_count++]={at,queued,real,silent,kind};
        else ++stats_.records_not_retained;
    }
    PcmRecoverySnapshot stats_;
    int32_t last_[2]={},anchor_[2]={};
    uint32_t progress_=0;
    uint8_t shift_=1;
    bool seeded_=false,missing_=false;
};
}}
