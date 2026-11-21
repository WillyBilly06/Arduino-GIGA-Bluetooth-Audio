// Native checks for the packed 24-bit I2sRing mode used by D2 SRAM rings.
#include "platform/I2sBuffer.h"
#include "platform/PcmRecovery.h"
#include <stdio.h>
#include <stdint.h>
using namespace giga_bredr::detail;
static unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } ++checks; } while (0)

int main() {
    // Storage for 7 packed frames = 42 bytes; give 11 words (44 bytes).
    int32_t storage[11] = {};
    I2sRing ring(storage, 7, true);
    CHECK(ring.packed());
    const int32_t in[] = {INT32_MIN, INT32_MAX, -256, 256, 0x12345678, -0x12345678, 0x7fffff00, -0x7fffff00,
                          1, -1, 0x000000ff, int32_t(0xffffff00u), 0x40000000, -0x40000000};
    // Expected: low 8 bits dropped, sign and upper 24 bits exact.
    auto expect = [](int32_t v) { return int32_t(uint32_t(v) & 0xffffff00u); };
    CHECK(ring.push(in, 7) == 7);
    CHECK(ring.push(in, 1) == 0); // full
    CHECK(ring.queued() == 7);
    int32_t dma[16];
    CHECK(ring.fill(dma, 5) == 5);
    for (unsigned i = 0; i < 10; ++i) CHECK(dma[i] == expect(in[i]));
    // Wrap: 2 left + push 4 more crosses the end of the 7-frame store.
    CHECK(ring.push(in, 4) == 4);
    CHECK(ring.queued() == 6);
    CHECK(ring.fill(dma, 8) == 6); // 6 real + 2 zero frames
    for (unsigned i = 0; i < 4; ++i) CHECK(dma[i] == expect(in[10 + i]));
    for (unsigned i = 0; i < 8; ++i) CHECK(dma[4 + i] == expect(in[i]));
    for (unsigned i = 12; i < 16; ++i) CHECK(dma[i] == 0);
    // Nothing written past the packed capacity (42 bytes): last word untouched.
    storage[10] = 0x5a5a5a5a;
    for (int round = 0; round < 20; ++round) { ring.push(in, 7); ring.fill(dma, 3); }
    CHECK((uint32_t(storage[10]) & 0xffff0000u) == 0x5a5a0000u);
    // Geometry: packed rings need 6 bytes per frame.
    I2sBufferGeometry a, b;
    CHECK(i2sBufferGeometry(192000, 190, a, false) && i2sBufferGeometry(192000, 190, b, true));
    CHECK(a.ring_frames == b.ring_frames && a.ring_offset == b.ring_offset);
    CHECK(a.total_bytes - a.ring_offset == size_t(a.ring_frames) * 8);
    CHECK(b.total_bytes - b.ring_offset == size_t(b.ring_frames) * 6);
    CHECK(b.total_bytes <= D2ArenaBytes && a.total_bytes > D2ArenaBytes);
    // Unpacked default is bit-exact.
    int32_t plain[8] = {}; I2sRing u(plain, 4);
    CHECK(!u.packed() && u.push(in, 4) == 4 && u.fill(dma, 4) == 4);
    for (unsigned i = 0; i < 8; ++i) CHECK(dma[i] == in[i]);
    // Recovery resume threshold: with setResumeFrames, playback restarts once
    // that much PCM is queued instead of rebuilding the whole startup reserve.
    for (int early = 0; early < 2; ++early) {
        int32_t mem[2 * 64] = {}; I2sRing rr(mem, 64);
        PcmRecovery rec; CHECK(rec.begin(1, 64, 4, 40));
        if (early) rec.setResumeFrames(12);
        int32_t pcmIn[2 * 64]; for (int i = 0; i < 128; ++i) pcmIn[i] = 1000 + i;
        int32_t dmaHalf[2 * 4];
        CHECK(rr.push(pcmIn, 40) == 40);
        for (int i = 0; i < 10; ++i) rec.refill(rr, dmaHalf, 0);   // drain the 40 frames
        rec.refill(rr, dmaHalf, 0); CHECK(rec.recovering());       // starved
        CHECK(rr.push(pcmIn, 12) == 12);
        rec.refill(rr, dmaHalf, 0);
        CHECK(rec.recovering() == !early);                          // 12 queued: resume only when early
        CHECK(rr.push(pcmIn, 28) == 28);
        rec.refill(rr, dmaHalf, 0); CHECK(!rec.recovering());       // full reserve always resumes
    }
    printf("PASS packed 24-bit I2S ring: sign/precision, wrap, bounds, geometry (%u checks)\n", checks);
    return 0;
}
