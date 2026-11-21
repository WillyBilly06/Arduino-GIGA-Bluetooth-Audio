/* aptX / aptX HD decoding must recover after a lost packet (the A2DP adapter
 * feeds one codeword at a time to aptx_decode_sync). Plain aptx_decode never
 * re-acquires the codeword parity sequence, which silenced a whole PipeWire
 * aptX HD stream after one discontinuity. */
#include "../src/vendor/codecs/aptx/freeaptx.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The vendored codec allocates through the library allocator. */
void* gbr_malloc(size_t n) { return malloc(n); }
void gbr_free(void* p) { free(p); }

static unsigned checks = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } ++checks; } while (0)

static int run(int hd, int use_sync, size_t *tail_nonzero) {
    const size_t frames = 48000 * 2, block = hd ? 6 : 4, per_packet = 100;
    unsigned char *pcm = malloc(frames * 6), *coded = malloc(frames / 4 * block + 64);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c) {
            int32_t v = (int32_t)lrint(sin(2.0 * 3.14159265358979323846 * 997.0 * (double)i / 48000.0 + c) * 4000000.0);
            pcm[6 * i + 3 * c] = (unsigned char)v; pcm[6 * i + 3 * c + 1] = (unsigned char)(v >> 8); pcm[6 * i + 3 * c + 2] = (unsigned char)(v >> 16);
        }
    struct aptx_context *enc = aptx_init(hd), *dec = aptx_init(hd);
    size_t written = 0;
    aptx_encode(enc, pcm, frames * 6, coded, frames / 4 * block + 64, &written);
    const size_t blocks = written / block, packets = blocks / per_packet;
    size_t nonzero_after = 0;
    for (size_t p = 0; p < packets; ++p) {
        if (p == packets / 3) continue; /* lost packet */
        const unsigned char *in = coded + p * per_packet * block;
        for (size_t used = 0; used < per_packet * block;) {
            unsigned char out[24]; size_t wrote = 0, got;
            if (use_sync) { size_t dropped = 0; int synced = 0; got = aptx_decode_sync(dec, in + used, block, out, sizeof out, &wrote, &synced, &dropped); }
            else got = aptx_decode(dec, in + used, block, out, sizeof out, &wrote);
            if (!got) break;
            used += got;
            if (p > packets / 3 + 2)
                for (size_t k = 0; k + 2 < wrote; k += 3) if (out[k] | out[k + 1] | out[k + 2]) { ++nonzero_after; break; }
        }
    }
    *tail_nonzero = nonzero_after;
    aptx_finish(enc); aptx_finish(dec); free(pcm); free(coded);
    return 0;
}

int main(void) {
    for (int hd = 0; hd < 2; ++hd) {
        size_t sync_ok = 0, plain_ok = 0;
        CHECK(run(hd, 1, &sync_ok) == 0);
        CHECK(run(hd, 0, &plain_ok) == 0);
        printf("%s: codewords with audio after the loss: sync=%zu plain=%zu\n", hd ? "aptX HD" : "aptX", sync_ok, plain_ok);
        CHECK(sync_ok > 10000);  /* resynchronized: keeps producing audio */
        CHECK(plain_ok * 10 < sync_ok); /* the old path stays mostly broken after the loss */
    }
    printf("PASS aptX/aptX HD resync after packet loss (%u checks)\n", checks);
    return 0;
}
