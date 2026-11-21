#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace giga_bredr { namespace detail {
// Level catch-up for interleaved stereo int32 PCM between decoder and I2S ring.
//
// Sources driven by an OS clock (Linux/PipeWire, VMs) can deliver a few
// percent slow or fast for seconds and drift hundreds of ppm. The PLL clock
// controller follows slow drift but rebuilds a lost reserve over minutes.
// This stage engages only when the PCM level is far from its target and
// resamples by at most +/-1% (cubic Catmull-Rom) to restore the reserve in
// seconds. While idle it is an exact zero-delay pass-through; while active it
// holds back up to two frames of lookahead, flushed on return to idle. Leaving
// catch-up eases the phase back onto an integer first, so the return to
// bit-exact output has no discontinuity.
class CatchUpResampler {
public:
    static constexpr uint32_t MaximumDeviationPpm = 10000; // 1%
    static constexpr uint32_t History = 3;

    void reset() {
        memset(hist_, 0, sizeof hist_);
        pos_ = uint64_t(History) << 32; step_ = uint64_t(1) << 32;
        active_ = locking_ = false; deviation_ppm_ = 0;
    }
    // error_frames = level - target. enter/exit/full are frame thresholds:
    // engage beyond enter, release inside exit, full deviation at |error|>=full.
    void control(int64_t error_frames, uint32_t enter, uint32_t exit, uint32_t full) {
        const int64_t magnitude = error_frames < 0 ? -error_frames : error_frames;
        if (!active_ && magnitude > int64_t(enter)) { active_ = true; locking_ = false; }
        if (active_ && magnitude < int64_t(exit)) { active_ = false; locking_ = true; }
        if (active_) {
            int64_t ppm = full ? -error_frames * int64_t(MaximumDeviationPpm) / int64_t(full) : 0;
            if (ppm > int64_t(MaximumDeviationPpm)) ppm = MaximumDeviationPpm;
            if (ppm < -int64_t(MaximumDeviationPpm)) ppm = -int64_t(MaximumDeviationPpm);
            deviation_ppm_ = int32_t(ppm); // >0: stretch (more output), <0: compress
            step_ = (uint64_t(1000000) << 32) / uint64_t(1000000 + ppm);
        } else if (locking_) {
            deviation_ppm_ = 0;
            step_ = (uint64_t(1) << 32) + (uint64_t(1) << 20); // ~244 ppm compress until the phase is integral
            if (!(pos_ & 0xffffffffu)) { locking_ = false; step_ = uint64_t(1) << 32; }
        } else { deviation_ppm_ = 0; step_ = uint64_t(1) << 32; }
    }
    bool active() const { return active_ || locking_; }
    int32_t deviationPpm() const { return deviation_ppm_; }
    // Upper bound of output frames for n input frames.
    static size_t capacityFor(size_t n) { return n + n / 64 + 4; }

    // Returns output frames written (<= cap). Input is consumed entirely.
    size_t process(const int32_t* in, size_t n, int32_t* out, size_t cap) {
        if (!n) return 0;
        size_t produced = 0;
        const uint64_t limit = uint64_t(n) << 32; // floor(pos) <= n keeps i+2 inside hist+input
        if (!active_ && !locking_ && !(pos_ & 0xffffffffu)) {
            // Idle, integral phase: exact pass-through of everything not yet
            // output (pending lookahead frames first), no added delay.
            for (uint32_t i = uint32_t(pos_ >> 32); i <= uint32_t(n) + 2u && produced < cap; ++i, ++produced)
                for (unsigned c = 0; c < 2; ++c) out[2 * produced + c] = at(in, n, i, c);
            pos_ = uint64_t(n + History) << 32;
        }
        while (pos_ <= limit + 0xffffffffu && (pos_ >> 32) <= n && produced < cap) {
            const uint32_t i = uint32_t(pos_ >> 32);
            const uint32_t frac = uint32_t(pos_);
            for (unsigned c = 0; c < 2; ++c) {
                const int32_t s1 = at(in, n, i, c);
                if (!frac) { out[2 * produced + c] = s1; continue; }
                const float s0 = float(at(in, n, i - 1, c)), f1 = float(s1);
                const float s2 = float(at(in, n, i + 1, c)), s3 = float(at(in, n, i + 2, c));
                const float t = float(frac) * (1.0f / 4294967296.0f);
                const float a = 0.5f * (s3 - s0) + 1.5f * (f1 - s2);
                const float b = s0 - 2.5f * f1 + 2.0f * s2 - 0.5f * s3;
                const float d = 0.5f * (s2 - s0);
                const float y = ((a * t + b) * t + d) * t + f1;
                out[2 * produced + c] = y >= 2147483520.0f ? INT32_MAX : y <= -2147483648.0f ? INT32_MIN : int32_t(y);
            }
            ++produced;
            const uint32_t before = uint32_t(pos_);
            pos_ += step_;
            if (locking_ && uint32_t(pos_) < before && step_ > (uint64_t(1) << 32)) {
                pos_ &= ~uint64_t(0xffffffffu); // wrapped past an integer: lock exactly onto it
                locking_ = false; step_ = uint64_t(1) << 32;
            }
        }
        // Keep the last three virtual frames (indices n, n+1, n+2) as history.
        int32_t next[2 * History];
        for (uint32_t k = 0; k < History; ++k) for (unsigned c = 0; c < 2; ++c) next[2 * k + c] = at(in, n, uint32_t(n) + k, c);
        memcpy(hist_, next, sizeof hist_);
        pos_ -= limit;
        return produced;
    }

private:
    // Virtual stream: hist_[0..2] then in[0..n-1].
    int32_t at(const int32_t* in, size_t n, uint32_t k, unsigned c) const {
        if (k < History) return hist_[2 * k + c];
        const size_t j = k - History;
        return j < n ? in[2 * j + c] : in[2 * (n - 1) + c];
    }
    int32_t hist_[2 * History] = {};
    uint64_t pos_ = uint64_t(1) << 32, step_ = uint64_t(1) << 32;
    int32_t deviation_ppm_ = 0;
    bool active_ = false, locking_ = false;
};
}}
