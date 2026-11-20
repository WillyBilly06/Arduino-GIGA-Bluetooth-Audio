/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* SSC frame decoder: header parsing and the per-frame control flow.
 * Mirrors ssc_encode in libScalable_Encoder.so (opus 1.1 celt_encode_with_ec with Samsung changes);
 * see docs/notes for the derivation of every condition below. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ssc_decoder.h"
#include "ssc_internal.h"
#include "ssc_tables.h"

#define LM 3
#define M 8
#define C 2
#define N SSC_FRAME
#define NB SSC_NB_BANDS
#define DB_SHIFT 10
#define NEG28 ((opus_val16)(-28 * (1 << DB_SHIFT)))

#ifdef SSC_DEBUG_TELL
/* Debug: bits consumed vs budget of the last frame (host tests only). */
int ssc_debug_tell, ssc_debug_total_bits, ssc_debug_storage_bits;
#endif

#ifdef SSC_PROFILE
/* Stage markers for benchmarking (0 = frame start, then one call per finished stage). */
void ssc_prof_mark(int stage);
#define PROF(s) ssc_prof_mark(s)
#else
#define PROF(s) ((void)0)
#endif

static const unsigned char spread_icdf[4] = {25, 23, 2, 0};
static const unsigned char trim_icdf[11] = {126, 124, 119, 109, 87, 41, 19, 9, 4, 2, 0};
static const signed char tf_select_table_LM3[8] = {0, -2, 0, -3, 3, 0, 1, -1};

/* The state is 3 blocks of ~8 KB (this struct, with the per-frame scratch) and ~7.4 KB (one postfilter
 * per channel), so it fits a fragmented heap (no PSRAM on the ESP32). Only the postfilter history, the
 * MDCT overlap and the energies persist across frames; the band shapes and spectra are decoded straight
 * into the frame part of each channel's postfilter buffer, and every other temporary is in `scratch`. */
struct ssc_decoder {
    int is96k;
    const ssc_alloc_mode *am;
    ssc_band_mode bm;
    uint32_t rng;
    opus_val16 oldBandE[C * NB];
    float oldLogE[C * NB], oldLogE2[C * NB];
    float ola[C][108];
    unsigned char collapse_masks[C * NB];
    int out_valid;               /* the frame part of pf[] holds a good decoded frame */
    ssc_postfilter_state *pf[C]; /* also holds each channel's band shapes, spectrum and output frame */
    ssc_alloc_fn alloc;
    ssc_free_fn release;
    void *alloc_ctx;
    ssc_scratch scratch;         /* per-frame only; lent out between frames (ssc_output_buffer) */
};

typedef char ssc_output_fits[sizeof(ssc_scratch) >= SSC_OUTPUT_BYTES ? 1 : -1];

int ssc_parse_frame_info(const uint8_t *f, int len, ssc_frame_info *h) {
    static const int frate[7] = {88000, 96000, 128000, 192000, 229000, 328000, 256000};
    static const int erate[7] = {291000, 308000, 442000, 584000, 886000, 250000, 152000};
    int code;
    if (len < 6 || f[0] != 0xFF || (f[1] & 0xF0) != 0xE0) return -1;
    code = f[3] & 0x1F;
    if (code >= 0x10 && code <= 0x16) {
        h->bitrate = frate[code - 0x10];
        h->hq = (code == 0x15 || code == 0x16);
        h->seq = f[2] >> 4;
        h->end_band = NB; /* bits 2-3 of byte 2 are side information only */
    } else if (code <= 0x06) {
        h->bitrate = erate[code];
        h->hq = 1;
        h->seq = ((f[1] & 0xF) << 4) | (f[2] >> 4);
        h->end_band = (f[3] == 0xE6) ? 18 : NB - ((f[2] >> 2) & 3);
    } else {
        return -1;
    }
    return 0;
}

int ssc_decoder_size(void) {
    return (int)(sizeof(ssc_decoder) + C * sizeof(ssc_postfilter_state));
}

void ssc_decoder_reset(ssc_decoder *st) {
    int i, c;
    st->rng = 0;
    memset(st->oldBandE, 0, sizeof st->oldBandE);
    for (i = 0; i < C * NB; i++) st->oldLogE[i] = st->oldLogE2[i] = -28.f;
    memset(st->ola, 0, sizeof st->ola);
    st->out_valid = 0;
    for (c = 0; c < C; c++) ssc_postfilter_init(st->pf[c]);
}

static void *default_alloc(void *ctx, unsigned size) {
    (void)ctx;
    return malloc(size);
}

static void default_free(void *ctx, void *p) {
    (void)ctx;
    free(p);
}

void ssc_decoder_destroy(ssc_decoder *st) {
    int c;
    if (!st) return;
    for (c = 0; c < C; c++)
        if (st->pf[c]) st->release(st->alloc_ctx, st->pf[c]);
    st->release(st->alloc_ctx, st);
}

ssc_decoder *ssc_decoder_create_with_allocator(int sample_rate, ssc_alloc_fn alloc, ssc_free_fn release,
                                               void *ctx) {
    ssc_decoder *st;
    int c, ok;
    if (sample_rate != 44100 && sample_rate != 48000 && sample_rate != 96000) return NULL;
    if (!alloc || !release) return NULL;
    st = (ssc_decoder *)alloc(ctx, sizeof(ssc_decoder));
    if (!st) return NULL;
    memset(st, 0, sizeof *st);
    st->alloc = alloc;
    st->release = release;
    st->alloc_ctx = ctx;
    ok = 1;
    for (c = 0; c < C; c++) {
        st->pf[c] = (ssc_postfilter_state *)alloc(ctx, sizeof(ssc_postfilter_state));
        ok = ok && st->pf[c];
    }
    if (!ok) {
        ssc_decoder_destroy(st);
        return NULL;
    }
    st->is96k = sample_rate == 96000;
    st->am = st->is96k ? &ssc_alloc_mode_96k : &ssc_alloc_mode_44k;
    st->bm.eBands = st->am->eBands;
    st->bm.logN = st->am->logN;
    st->bm.cache_index = st->am->cache_index;
    st->bm.cache_bits = st->am->cache_bits;
    ssc_decoder_reset(st);
    return st;
}

ssc_decoder *ssc_decoder_create(int sample_rate) {
    return ssc_decoder_create_with_allocator(sample_rate, default_alloc, default_free, NULL);
}

/* Decodes into each channel's postfilter buffer (864 samples, 16-bit PCM units). */
static int decode_frame(ssc_decoder *st, const uint8_t *frame, int len) {
    float *X = ssc_postfilter_buffer(st->pf[0]), *Y = ssc_postfilter_buffer(st->pf[1]);
    const short *eBands = st->am->eBands;
    ssc_frame_info h;
    ec_dec dec;
    int payload, i, c, end;
    int32_t total_bits, tell, total_bits8, total_boost, bits, balance;
    int silence, isTransient, shortBlocks, intra, spread, alloc_trim;
    int pf_on = 0, pf_T = 0, pf_qg = 0;
    int tf_res[NB], offsets[NB], fine_quant[NB], pulses[NB], fine_priority[NB];
    int dynalloc_logp, codedBands, intensity = 0, dual_stereo = 0, anti_collapse_rsv, anti_collapse_on = 0;
    float logE[C * NB];

    PROF(0);
    st->out_valid = 0;
    if (ssc_parse_frame_info(frame, len, &h)) return -1;
    end = h.end_band;
    payload = len - 4;
    total_bits = 8 * (payload < 1275 ? payload : 1275); /* encoder clamps the budget, not the buffer */
    ec_dec_init(&dec, (unsigned char *)frame + 4, (opus_uint32)payload);

    /* silence */
    tell = ec_tell(&dec);
    if (tell >= total_bits) silence = 1;
    else if (tell == 1) silence = ec_dec_bit_logp(&dec, 15);
    else silence = 0;
    if (silence) {
        tell = total_bits;
        dec.nbits_total += total_bits - ec_tell(&dec);
    }

    /* postfilter (no tapset symbol in SSC) */
    if (tell + 16 <= total_bits) {
        if (ec_dec_bit_logp(&dec, 1)) {
            int octave = (int)ec_dec_uint(&dec, 6);
            pf_T = (16 << octave) + (int)ec_dec_bits(&dec, 4 + octave) - 1;
            pf_qg = (int)ec_dec_bits(&dec, 3);
            pf_on = 1;
        }
        tell = ec_tell(&dec);
    }

    /* transient */
    isTransient = 0;
    if (tell + 3 <= total_bits) isTransient = ec_dec_bit_logp(&dec, 3);
    shortBlocks = isTransient ? M : 0;

    /* coarse energy */
    intra = (ec_tell(&dec) + 3 <= total_bits) ? ec_dec_bit_logp(&dec, 3) : 0;
    ssc_unquant_coarse_energy(NB, 0, end, st->oldBandE, intra, &dec, C, total_bits);
    PROF(1); /* header, postfilter params, coarse energy */

    /* time-frequency resolution: not coded for hq rates. Budget is the full storage. */
    if (!h.hq) {
        uint32_t budget = dec.storage * 8, t = (uint32_t)ec_tell(&dec);
        int logp = isTransient ? 2 : 4, curr = 0, tf_changed = 0, tf_select = 0;
        int tf_select_rsv = (t + logp + 1 <= budget);
        budget -= tf_select_rsv;
        for (i = 0; i < NB; i++) {
            if (t + logp <= budget) {
                curr ^= ec_dec_bit_logp(&dec, logp);
                t = (uint32_t)ec_tell(&dec);
                tf_changed |= curr;
            }
            tf_res[i] = curr;
            logp = isTransient ? 4 : 5;
        }
        if (tf_select_rsv && tf_select_table_LM3[4 * isTransient + 0 + tf_changed] !=
                                 tf_select_table_LM3[4 * isTransient + 2 + tf_changed])
            tf_select = ec_dec_bit_logp(&dec, 1);
        for (i = 0; i < NB; i++) tf_res[i] = tf_select_table_LM3[4 * isTransient + 2 * tf_select + tf_res[i]];
    } else {
        for (i = 0; i < NB; i++) tf_res[i] = 0;
    }

    /* spreading */
    spread = 2;
    if (ec_tell(&dec) + 4 <= total_bits) spread = ec_dec_icdf(&dec, spread_icdf, 5);

    /* dynamic allocation */
    dynalloc_logp = 6;
    total_bits8 = total_bits << 3;
    total_boost = 0;
    tell = (int32_t)ec_tell_frac(&dec);
    for (i = 0; i < NB; i++) offsets[i] = 0;
    for (i = 0; i < end; i++) {
        int quanta = (eBands[i + 1] - eBands[i]) << 4;
        int loop_logp = dynalloc_logp, boost = 0;
        if (quanta < 48) quanta = 48;
        while (tell + (loop_logp << 3) < total_bits8 - total_boost && boost < st->am->cap[i]) {
            int flag = ec_dec_bit_logp(&dec, (unsigned)loop_logp);
            tell = (int32_t)ec_tell_frac(&dec);
            if (!flag) break;
            boost += quanta;
            total_boost += quanta;
            loop_logp = 1;
        }
        offsets[i] = boost;
        if (boost > 0) dynalloc_logp = dynalloc_logp - 1 < 2 ? 2 : dynalloc_logp - 1;
    }
    /* Samsung: propagate a large boost upward into the next band, based on channel-0 coarse energies */
    {
        const opus_val16 *E = st->oldBandE;
        if (offsets[18] > 1000 && offsets[19] == 0) {
            if (E[19] > 7000 || (E[18] >> 1) < E[19]) offsets[19] = offsets[18];
        } else if (offsets[19] > 1000 && offsets[20] == 0) {
            if (E[20] > 7000 || (E[19] >> 1) < E[20]) offsets[20] = offsets[19];
        } else if (offsets[20] > 1000 && offsets[21] == 0) {
            if (E[21] > 7000 || (E[20] >> 1) < E[21]) offsets[21] = offsets[20];
        }
    }

    /* allocation trim */
    alloc_trim = (tell + (6 << 3) <= total_bits8 - total_boost) ? ec_dec_icdf(&dec, trim_icdf, 7) : 5;

    /* bit allocation */
    bits = total_bits8 - (int32_t)ec_tell_frac(&dec) - 1;
    anti_collapse_rsv = (isTransient && bits >= ((LM + 2) << 3)) ? (1 << 3) : 0;
    bits -= anti_collapse_rsv;
    codedBands = ssc_compute_allocation_dec(st->am, end, offsets, alloc_trim, h.bitrate > 192000, bits,
                                            &balance, pulses, fine_quant, fine_priority, &intensity,
                                            &dual_stereo, &dec);

    PROF(2); /* tf, spread, dynalloc, trim, allocation */
    ssc_unquant_fine_energy(NB, 0, end, st->oldBandE, fine_quant, &dec, C);

    /* The frame part of the postfilter buffers is free here: the previous frame is also in the history. */
    memset(X, 0, sizeof(float) * N);
    memset(Y, 0, sizeof(float) * N);
    ssc_quant_all_bands_dec(&st->bm, end, X, Y, st->collapse_masks, pulses, shortBlocks, spread, dual_stereo,
                            intensity, tf_res, total_bits8 - anti_collapse_rsv, balance, &dec, codedBands,
                            &st->rng, &st->scratch.qab);

    PROF(3); /* fine energy + band shapes (PVQ) */
    if (anti_collapse_rsv > 0) anti_collapse_on = (int)ec_dec_bits(&dec, 1);

    ssc_unquant_energy_finalise(NB, 0, end, st->oldBandE, fine_quant, fine_priority, total_bits - ec_tell(&dec),
                                &dec, C);

    for (i = 0; i < C * NB; i++) logE[i] = st->oldBandE[i] * (1.f / (1 << DB_SHIFT));

    if (anti_collapse_on)
        ssc_anti_collapse(&st->bm, X, Y, st->collapse_masks, 0, end, logE, st->oldLogE, st->oldLogE2, pulses,
                          st->rng);

    PROF(4); /* finalise + anti-collapse */
    for (c = 0; c < C; c++) {
        float *freq = c ? Y : X; /* denormalised in place, then replaced by the time signal */
        if (silence)
            memset(freq, 0, sizeof(float) * N);
        else
            ssc_denormalise_bands(eBands, freq, freq, logE + c * NB, 0, end);
        PROF(5); /* denormalise */
        ssc_synthesis_frame(st->scratch.synth, freq, shortBlocks, st->ola[c]);
        PROF(6); /* IMDCT + overlap-add */
        ssc_postfilter_run(st->pf[c], pf_on, pf_T, pf_qg);
        PROF(7); /* postfilter */
    }

    /* Samsung: bands above the coded end get -28 (CELT uses 0) */
    if (silence)
        for (i = 0; i < C * NB; i++) st->oldBandE[i] = NEG28;
    for (c = 0; c < C; c++)
        for (i = end; i < NB; i++) st->oldBandE[c * NB + i] = NEG28;

    /* anti-collapse energy history (as opus 1.1) */
    for (i = 0; i < C * NB; i++) logE[i] = st->oldBandE[i] * (1.f / (1 << DB_SHIFT));
    if (!isTransient) {
        memcpy(st->oldLogE2, st->oldLogE, sizeof st->oldLogE);
        memcpy(st->oldLogE, logE, sizeof st->oldLogE);
    } else {
        for (i = 0; i < C * NB; i++)
            if (logE[i] < st->oldLogE[i]) st->oldLogE[i] = logE[i];
    }
#ifdef SSC_DEBUG_TELL
    ssc_debug_tell = ec_tell(&dec);
    ssc_debug_total_bits = total_bits;
    ssc_debug_storage_bits = (int)dec.storage * 8;
#endif
    st->rng = dec.rng;
    if (dec.error) return -2;
    st->out_valid = 1;
    return 0;
}

int ssc_frame_bytes(const uint8_t *frame, int avail, int sample_rate) {
    ssc_frame_info h;
    int fs, base;
    if (avail < 6 || ssc_parse_frame_info(frame, avail, &h)) return 0;
    fs = sample_rate == 96000 ? 96000 : 44100; /* 48 kHz streams use the 44.1 kHz mode */
    /* Same formula as the encoder: base = bitrate*864/(Fs*8), +3/128 for non-hq rates, forced odd;
     * the range-coded payload is base-1 bytes after the 4-byte header. */
    base = (int)(((int64_t)h.bitrate * N) / ((int64_t)fs * 8));
    if (!h.hq) base += base * 3 >> 7;
    base |= 1;
    return base - 1 + 4;
}

/* lrintf() in the default rounding mode (nearest, ties to even) for |v| < 2^22, without a libm call (lrintf
 * is a library call on the ESP32). Adding 1.5 * 2^23 rounds v to an integer in the low mantissa bits. */
static inline int32_t rint_small(float v) {
    union { float f; uint32_t u; } t;
    t.f = v + 12582912.f;
    return (int32_t)(t.u - 0x4B400000u);
}

/* Same as lrintf() for |v| < 2^31. At or above 2^22 a float is a multiple of 0.5, so truncation plus a
 * ties-to-even fix-up is exact. */
static inline int32_t rint_s32(float v) {
    int32_t i;
    float f;
    if (v < 4194304.f && v > -4194304.f) return rint_small(v);
    i = (int32_t)v;
    f = v - (float)i; /* exact: 0 or +-0.5 */
    if (f == 0.5f) i += i & 1;
    else if (f == -0.5f) i -= i & 1;
    return i;
}

static int16_t to_s16(float v) {
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)rint_small(v);
}

static int32_t to_s32(float x) {
    float v = x * 65536.f;
    return v >= 2147483520.f ? INT32_MAX : v <= -2147483648.f ? INT32_MIN : rint_s32(v);
}

int ssc_decode_frame(ssc_decoder *st, const uint8_t *frame, int len) {
    int ret = decode_frame(st, frame, len);
    return ret < 0 ? ret : N;
}

void *ssc_output_buffer(ssc_decoder *st) { return &st->scratch; }

/* Shared range check: 0 <= offset, 0 <= count, offset + count <= 864. */
static int read_range(int offset, int count) { return offset >= 0 && count >= 0 && count <= N - offset; }

int ssc_read_s16(ssc_decoder *st, int offset, int count, int16_t *pcm) {
    const float *l, *r;
    int i;
    if (!read_range(offset, count)) return -1;
    l = ssc_postfilter_buffer(st->pf[0]) + offset;
    r = ssc_postfilter_buffer(st->pf[1]) + offset;
    if (!st->out_valid) {
        memset(pcm, 0, sizeof(int16_t) * 2 * (size_t)count);
        return count;
    }
    for (i = 0; i < count; i++) {
        pcm[2 * i] = to_s16(l[i]);
        pcm[2 * i + 1] = to_s16(r[i]);
    }
    return count;
}

int ssc_read_s32(ssc_decoder *st, int offset, int count, int32_t *pcm) {
    const float *l, *r;
    int i;
    if (!read_range(offset, count)) return -1;
    l = ssc_postfilter_buffer(st->pf[0]) + offset;
    r = ssc_postfilter_buffer(st->pf[1]) + offset;
    if (!st->out_valid) {
        memset(pcm, 0, sizeof(int32_t) * 2 * (size_t)count);
        return count;
    }
    for (i = 0; i < count; i++) {
        pcm[2 * i] = to_s32(l[i]);
        pcm[2 * i + 1] = to_s32(r[i]);
    }
    return count;
}

int ssc_read_float(ssc_decoder *st, int offset, int count, float *pcm) {
    const float *l, *r;
    int i;
    if (!read_range(offset, count)) return -1;
    l = ssc_postfilter_buffer(st->pf[0]) + offset;
    r = ssc_postfilter_buffer(st->pf[1]) + offset;
    if (!st->out_valid) {
        memset(pcm, 0, sizeof(float) * 2 * (size_t)count);
        return count;
    }
    for (i = 0; i < count; i++) {
        pcm[2 * i] = l[i] * (1.f / 32768.f);
        pcm[2 * i + 1] = r[i] * (1.f / 32768.f);
    }
    return count;
}

int ssc_decode(ssc_decoder *st, const uint8_t *frame, int len, int16_t *pcm) {
    int ret = ssc_decode_frame(st, frame, len);
    ssc_read_s16(st, 0, N, pcm); /* silence on error */
    return ret;
}

int ssc_decode_s32(ssc_decoder *st, const uint8_t *frame, int len, int32_t *pcm) {
    int ret = ssc_decode_frame(st, frame, len);
    ssc_read_s32(st, 0, N, pcm);
    return ret;
}

int ssc_decode_float(ssc_decoder *st, const uint8_t *frame, int len, float *pcm) {
    int ret = ssc_decode_frame(st, frame, len);
    ssc_read_float(st, 0, N, pcm);
    return ret;
}
#endif
