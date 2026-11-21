// Native checks for the PCM level catch-up resampler (src/detail/CatchUpResampler.h).
#include "detail/CatchUpResampler.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
using giga_bredr::detail::CatchUpResampler;
static unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } ++checks; } while (0)

static int32_t sine(size_t k, unsigned c) {
    return int32_t(lrint(sin(2.0 * M_PI * 997.0 * double(k) / 48000.0 + c) * 1.5e9));
}

int main() {
    CatchUpResampler r; r.reset();
    // 1. Idle: exact zero-delay pass-through, across uneven block sizes.
    std::vector<int32_t> in, out;
    size_t k = 0, produced = 0; std::vector<int32_t> all_in, all_out;
    const size_t blocks[] = {960, 1, 7, 2880, 128, 3, 960};
    for (size_t n : blocks) {
        in.resize(2 * n); for (size_t i = 0; i < n; ++i) { in[2*i] = sine(k, 0); in[2*i+1] = sine(k, 1); ++k; }
        out.resize(2 * CatchUpResampler::capacityFor(n));
        r.control(0, 100, 20, 200);
        const size_t m = r.process(in.data(), n, out.data(), out.size() / 2);
        CHECK(m == n);
        all_in.insert(all_in.end(), in.begin(), in.end()); all_out.insert(all_out.end(), out.begin(), out.begin() + 2 * m);
        produced += m;
    }
    CHECK(produced == all_in.size() / 2);
    for (size_t i = 0; i < produced; ++i) { CHECK(all_out[2*i] == all_in[2*i]); CHECK(all_out[2*i+1] == all_in[2*i+1]); }

    // 2. Stretch (level low) and compress (level high): output/input ratio ~ +/-1%.
    for (int sign : {-1, +1}) {
        r.reset(); size_t tin = 0, tout = 0; int64_t maxstep = 0; int32_t prev = 0; bool have = false; size_t seen = 0;
        for (int b = 0; b < 200; ++b) {
            const size_t n = 960; in.resize(2*n);
            for (size_t i = 0; i < n; ++i) { in[2*i] = sine(k, 0); in[2*i+1] = sine(k, 1); ++k; }
            out.resize(2 * CatchUpResampler::capacityFor(n));
            r.control(sign * 10000, 100, 20, 1000); // far from target: full deviation
            CHECK(r.active());
            const size_t m = r.process(in.data(), n, out.data(), out.size() / 2);
            CHECK(m <= CatchUpResampler::capacityFor(n));
            // Skip the warm-up from zeroed history (first 3 frames of the run).
            for (size_t i = 0; i < m; ++i, ++seen) { if (have && seen > 3) { int64_t d = int64_t(out[2*i]) - prev; if (d < 0) d = -d; if (d > maxstep) maxstep = d; } prev = out[2*i]; have = true; }
            tin += n; tout += m;
        }
        const double ratio = double(tout) / double(tin);
        if (sign < 0) { CHECK(ratio > 1.0095 && ratio < 1.0105); CHECK(r.deviationPpm() == 10000); }
        else { CHECK(ratio > 0.9895 && ratio < 0.9905); CHECK(r.deviationPpm() == -10000); }
        // 997 Hz at 48 kHz, amplitude 1.5e9: max natural step ~ 1.96e8; resampling adds no jumps.
        CHECK(maxstep < 2.1e8);
    }

    // 3. Release returns to bit-exact passthrough at an integer offset.
    r.reset(); k = 0;
    for (int b = 0; b < 30; ++b) {
        const size_t n = 480; in.resize(2*n);
        for (size_t i = 0; i < n; ++i) { in[2*i] = sine(k, 0); in[2*i+1] = sine(k, 1); ++k; }
        out.resize(2 * CatchUpResampler::capacityFor(n));
        r.control(b < 10 ? -5000 : 0, 100, 20, 1000); // catch up for 10 blocks, then release
        r.process(in.data(), n, out.data(), out.size() / 2);
    }
    CHECK(!r.active());
    {   // Idle again: the first call may flush up to two held lookahead frames,
        // after which output is an exact zero-delay copy of the input.
        const size_t n = 480; in.resize(2*n);
        for (size_t i = 0; i < n; ++i) { in[2*i] = int32_t(i * 4099 + 17); in[2*i+1] = -int32_t(i * 7 + 3); }
        out.resize(2 * CatchUpResampler::capacityFor(n));
        r.control(0, 100, 20, 1000);
        const size_t m = r.process(in.data(), n, out.data(), out.size() / 2);
        CHECK(m >= n && m <= n + 2);
        const size_t lead = m - n;
        for (size_t i = 0; i < n; ++i) { CHECK(out[2*(lead+i)] == in[2*i]); CHECK(out[2*(lead+i)+1] == in[2*i+1]); }
        const size_t m2 = r.process(in.data(), n, out.data(), out.size() / 2);
        CHECK(m2 == n);
        for (size_t i = 0; i < n; ++i) CHECK(out[2*i] == in[2*i]);
    }
    // 4. Hysteresis: engage beyond 100, stay engaged at 50, release below 20.
    r.reset(); r.control(-150, 100, 20, 1000); CHECK(r.active() && r.deviationPpm() == 1500);
    r.control(-50, 100, 20, 1000); CHECK(r.active() && r.deviationPpm() == 500);
    r.control(-10, 100, 20, 1000); CHECK(r.deviationPpm() == 0);
    // 5. Extreme values saturate instead of wrapping.
    r.reset(); int32_t edge[8] = {INT32_MAX, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN, INT32_MAX};
    int32_t eo[2 * 16]; r.control(-10000, 100, 20, 1000);
    for (int b = 0; b < 4; ++b) { size_t m = r.process(edge, 4, eo, 16); CHECK(m <= 16); }
    printf("PASS catch-up resampler: bit-exact idle, +/-1%% ratio, continuity, exact release, hysteresis (%u checks)\n", checks);
    return 0;
}
