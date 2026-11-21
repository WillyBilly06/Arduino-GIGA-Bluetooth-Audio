// Native checks for RTP packet-loss concealment (src/detail/LossConcealment.h).
#include "detail/LossConcealment.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
using giga_bredr::detail::LossConcealment;
static unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } ++checks; } while (0)

int main() {
    int32_t store[2 * 64];
    LossConcealment c; c.attach(store, 64, 192000);
    CHECK(c.fadeFrames() == 480);
    // Sequence: first packet establishes, in-order fills nothing.
    CHECK(c.onPacket(100) == 0);
    c.packetFrames(2880);
    CHECK(c.onPacket(101) == 0);
    // One lost packet -> exactly one packet of frames.
    CHECK(c.onPacket(103) == 2880);
    // Duplicate / backward fill nothing and keep the reference.
    CHECK(c.onPacket(103) == 0);
    CHECK(c.onPacket(90) == 0);
    CHECK(c.onPacket(104) == 0);
    // 16-bit wrap: 65535 -> 1 loses packet 0.
    c.reset(); c.packetFrames(2880);
    CHECK(c.onPacket(65535) == 0);
    CHECK(c.onPacket(1) == 2880);
    // Gap limits: 400 ms at 192 kHz is 76800 frames = 26 whole LHDC packets.
    c.reset(); c.packetFrames(2880); c.onPacket(0);
    CHECK(c.onPacket(27) == 26u * 2880u);  // 26 lost, 390 ms
    CHECK(c.onPacket(55) == 0);            // 27 lost, 405 ms: interruption
    CHECK(c.unfilledGap());
    CHECK(c.onPacket(56) == 0 && !c.unfilledGap());
    CHECK(c.onPacket(58) == 2880 && !c.unfilledGap());
    c.reset(); c.packetFrames(128); c.onPacket(0);
    CHECK(c.onPacket(65) == 64u * 128u);   // 64-packet cap
    CHECK(c.onPacket(131) == 0);           // 65 lost
    // Unknown packet size: nothing to fill.
    c.reset(); c.onPacket(0); CHECK(c.onPacket(5) == 0);

    // Synthesis: no history -> silence of the requested length.
    int32_t out[2 * 400];
    for (auto& v : out) v = 12345;
    c.reset(); c.synthesize(out, 0, 400, 400);
    for (unsigned i = 0; i < 800; ++i) CHECK(out[i] == 0);
    // History ramp 0..63 (L) and negative (R): first fill sample continues the
    // last real sample, the replay turns without jumps, envelope ends at zero.
    int32_t real[2 * 100];
    for (int i = 0; i < 100; ++i) { real[2 * i] = (i - 36) * 1000000; real[2 * i + 1] = -(i - 36) * 1000000; }
    c.remember(real, 100); CHECK(c.rememberedFrames() == 64); // tail kept: real[36..99]
    const uint32_t total = 400;
    c.synthesize(out, 0, total, total);
    CHECK(out[0] == 63 * 1000000 && out[1] == -63 * 1000000); // full gain, last sample
    for (uint32_t i = 1; i < total; ++i) {
        const int64_t step = int64_t(out[2 * i]) - out[2 * i - 2];
        CHECK(llabs(step) <= 1000000 + 63 * 1000000 / int64_t(total) + 2); // no wrap discontinuity
        CHECK(out[2 * i + 1] == -out[2 * i]);
        CHECK(llabs(out[2 * i]) <= 63 * 1000000);
    }
    CHECK(llabs(out[2 * (total - 1)]) <= 63 * 1000000 / int64_t(total) + 1); // fades to ~0
    // Chunked synthesis equals one-shot synthesis.
    int32_t chunked[2 * 400];
    c.synthesize(chunked, 0, 150, total); c.synthesize(chunked + 300, 150, 250, total);
    for (unsigned i = 0; i < 800; ++i) CHECK(chunked[i] == out[i]);
    // Fade-in: starts at zero, reaches unity after fadeFrames, then untouched.
    int32_t in[2 * 600];
    for (auto& v : in) v = 1 << 20;
    c.beginFadeIn(); c.applyFadeIn(in, 200); c.applyFadeIn(in + 400, 400);
    CHECK(in[0] == 0 && in[1] == 0);
    for (unsigned i = 1; i < 480; ++i) CHECK(in[2 * i] >= in[2 * i - 2] && in[2 * i] < (1 << 20));
    for (unsigned i = 480; i < 600; ++i) CHECK(in[2 * i] == (1 << 20));
    // Extreme values stay in range (symmetric scaling, no overflow).
    int32_t edge[4] = {INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX};
    c.remember(edge, 2); c.synthesize(out, 0, 4, 4);
    CHECK(out[0] <= 0 && out[1] >= 0);
    printf("PASS RTP loss concealment: exact gap fill, wrap/duplicate/limits, continuous faded replay, fade-in (%u checks)\n", checks);
    return 0;
}
