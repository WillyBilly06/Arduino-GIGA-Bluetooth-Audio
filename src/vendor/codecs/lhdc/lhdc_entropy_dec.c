/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "../CodecPlatform.h"
#if GBR_ENABLE_LHDC
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
#include "lhdc_entropy_dec.h"
#include "lhdc_bit_reader.h"
#include "lhdc_tables.h"
#include "lhdc_entropy_limits.h"
#include "lhdc_dec_slot.h"   /* LHDC_NSLOTS / LHDC_DEC_SLOT(): parallel channel decode */
#include <string.h>
#include <stdlib.h>   /* malloc/free for the lazily-heaped FAC models */
#if defined(LHDC_HOST_BUILD)
#include <stdio.h>
#include <stdlib.h>
#define LHDC_HOT          /* host: no IRAM */
#else
#include "esp_attr.h"
/* Place the per-symbol/per-coeff entropy hot path in IRAM. The BT controller is
 * interrupt-heavy and evicts the decoder's flash-cache lines mid-frame, which
 * both slows the steady decode and causes the per-packet latency spikes. IRAM
 * execution removes those cache stalls. ~18 KB IRAM headroom available. */
#define LHDC_HOT          IRAM_ATTR
#endif

/* Inline control for the range coder. fac_decode + fac_model_update run once per
 * decoded symbol (~1M/s at 192k/1000k), so they must fold into the caller's loop:
 * an out-of-line Xtensa windowed call (entry/retw + register window rotation)
 * costs more than the symbol decode itself. fac_model_rescale is the opposite --
 * it fires roughly once per 20 symbols, and inlining its three call sites is what
 * pushed fac_decode past GCC's size heuristic and out of line in the first place,
 * so it is pinned out-of-line (still IRAM). */
#define LHDC_ALWAYS_INLINE __attribute__((always_inline)) inline
#define LHDC_NOINLINE      __attribute__((noinline))

/*
 * LHDC V5 entropy decoder — reverse-engineered and validated bit-exact against
 * the real liblhdcv5.so encoder (range coder 50/50, Rice 20000/20000 round-trip,
 * full tone1k frame -> ~1kHz spectrum).
 *
 * The spectral data is two ternary (3-symbol) streams produced by
 * RiceQuotientEncode then FAC-MA range coded (BitsPredictCompressOutput):
 *   stream1 = first `count` symbols (lsb/marker plane), init freqs {54,12,1},
 *             sliding window MA_WIN1.
 *   stream2 = the rest (quotient codes),                init freqs {90,16,1},
 *             sliding window MA_WIN2.
 * Both share one byte range-coder stream (init code = first 4 bytes; total freq
 * = 2^15). After the FAC stream a sign bit-plane follows: one bit per nonzero
 * coefficient (1 = negative).
 */

#define FAC_TOTAL_BITS 15
#define FAC_TOTAL      (1 << FAC_TOTAL_BITS)
#define FAC_NSYM       3

#if 0 /* excluded host diagnostics */
int g_fdc = 0;     /* per-channel DSTATE symbol counter (reset each entropy call) */
int g_block = 0;   /* which channel-block since trace start (0=a-ch0,1=a-ch1,2=b-ch0,...) */
#endif
/* Per-slot (per-core): the two channels decode concurrently on the classic
 * ESP32, and each reads back its own final range in lhdc_dec.c. */
uint32_t g_fac_final_range[LHDC_NSLOTS] = {0};

static const uint32_t FAC_FQ1[FAC_NSYM] = {54, 12, 1};
static const uint32_t FAC_FQ2[FAC_NSYM] = {90, 16, 1};

/* --- FAC-MA adaptive model (sliding window) --- */
typedef struct {
    int      n;
    int      window;
    int      pos;
    int      count;
    uint8_t *hist;                /* MA-window history; points at s_fac_hist1/2 */
    int      hist_cap;            /* capacity of *hist (window is clamped to it) */
    uint32_t freq[FAC_NSYM];
    uint32_t thresh[FAC_NSYM];
    uint32_t cum[FAC_NSYM + 1];   /* 15-bit normalized cumulative freqs */
} fac_model_t;

/* Actual production windows are 57/63 (44.1/48k) and 96/64 (96/192k), not the
 * obsolete bitrate-scaled estimates. Shared table definitions enforce these
 * bounds at compile time. Both histories remain static, with no new heap or
 * lifecycle dependency; reducing 512+3072 to 96+64 releases 3424 bytes per slot.
 * Oversize/nonpositive external overrides are rejected before any mutation. */
/* One set PER SLOT: the range coder's adaptive state is mutated on every symbol,
 * so two channels decoding at once (one per core) must not share it. */
static struct { uint8_t hist1[LHDC_FAC_HISTORY1_CAPACITY];
    uint8_t hist2[LHDC_FAC_HISTORY2_CAPACITY]; fac_model_t models[2]; } *s_entropy;
#define s_fac_m (&s_entropy->models)
void lhdc_entropy_alloc_internal(void) {
    if(!s_entropy) return;
    s_entropy->models[0].hist=s_entropy->hist1;
    s_entropy->models[0].hist_cap=sizeof(s_entropy->hist1);
    s_entropy->models[1].hist=s_entropy->hist2;
    s_entropy->models[1].hist_cap=sizeof(s_entropy->hist2);
}
void lhdc_entropy_alloc(void) { if(!s_entropy) s_entropy=gbr_calloc(1,sizeof(*s_entropy)); }
int gbr_lhdc_entropy_ready(void) { return s_entropy!=0; }
void lhdc_entropy_free(void) { gbr_free(s_entropy); s_entropy=0; }
/* The FAC alphabet is always ternary (m->n is only ever assigned FAC_NSYM), so
 * the model loops are written out for n == 3: no trip counts, no m->n reload,
 * and the cumulative freqs stay in registers.
 *
 * The 64-bit product is also gone. cum[i] <= total and scale = 0x80000000/total
 * (floor), so cum[i]*scale <= total*floor(0x80000000/total) <= 0x80000000 --
 * it cannot overflow 32 bits. Verified by differential test over 20M decoded
 * symbols (max product observed: exactly 0x80000000). On Xtensa that turns a
 * mull/muluh/shift sequence into a single mull per entry. */
_Static_assert(FAC_NSYM == 3, "fac model fast paths assume a ternary alphabet");

/* Exact floor(2^31 / total), for totals 67..171. Initial frequencies sum
 * to 67/107; each warm-up symbol adds one and, once full, replacing a history
 * symbol preserves the total. Positive initial frequencies ensure the removed
 * symbol has frequency >= 2. Thus the current 96/64 histories stay in this
 * range. Keep the original division fallback for all other nonzero totals.
 *
 * This const table occupies 420 bytes of flash, with no new DRAM or heap. An
 * IRAM caller reads cached flash data here; target timing (including Bluetooth
 * cache contention) must still be measured. */
static const uint32_t s_fac_scale[105] = {
    0x01e9131au, 0x01e1e1e1u, 0x01dae607u, 0x01d41d41u, 0x01cd8568u, 0x01c71c71u,
    0x01c0e070u, 0x01bacf91u, 0x01b4e81bu, 0x01af286bu, 0x01a98ef6u, 0x01a41a41u,
    0x019ec8e9u, 0x01999999u, 0x01948b0fu, 0x018f9c18u, 0x018acb90u, 0x01861861u,
    0x01818181u, 0x017d05f4u, 0x0178a4c8u, 0x01745d17u, 0x01702e05u, 0x016c16c1u,
    0x01681681u, 0x01642c85u, 0x01605816u, 0x015c9882u, 0x0158ed23u, 0x01555555u,
    0x0151d07eu, 0x014e5e0au, 0x014afd6au, 0x0147ae14u, 0x01446f86u, 0x01414141u,
    0x013e22cbu, 0x013b13b1u, 0x01381381u, 0x013521cfu, 0x01323e34u, 0x012f684bu,
    0x012c9fb4u, 0x0129e412u, 0x0127350bu, 0x01249249u, 0x0121fb78u, 0x011f7047u,
    0x011cf06au, 0x011a7b96u, 0x01181181u, 0x0115b1e5u, 0x01135c81u, 0x01111111u,
    0x010ecf56u, 0x010c9714u, 0x010a6810u, 0x01084210u, 0x010624ddu, 0x01041041u,
    0x01020408u, 0x01000000u, 0x00fe03f8u, 0x00fc0fc0u, 0x00fa232cu, 0x00f83e0fu,
    0x00f6603du, 0x00f4898du, 0x00f2b9d6u, 0x00f0f0f0u, 0x00ef2eb7u, 0x00ed7303u,
    0x00ebbdb2u, 0x00ea0ea0u, 0x00e865acu, 0x00e6c2b4u, 0x00e52598u, 0x00e38e38u,
    0x00e1fc78u, 0x00e07038u, 0x00dee95cu, 0x00dd67c8u, 0x00dbeb61u, 0x00da740du,
    0x00d901b2u, 0x00d79435u, 0x00d62b80u, 0x00d4c77bu, 0x00d3680du, 0x00d20d20u,
    0x00d0b69fu, 0x00cf6474u, 0x00ce168au, 0x00ccccccu, 0x00cb8727u, 0x00ca4587u,
    0x00c907dau, 0x00c7ce0cu, 0x00c6980cu, 0x00c565c8u, 0x00c4372fu, 0x00c30c30u,
    0x00c1e4bbu, 0x00c0c0c0u, 0x00bfa02fu,
};

static LHDC_HOT LHDC_NOINLINE void fac_model_rescale(fac_model_t *m)
{
    const uint32_t f0 = m->freq[0], f1 = m->freq[1], f2 = m->freq[2];
    m->thresh[0] = f0 + (f0 >> 5);
    m->thresh[1] = f1 + (f1 >> 5);
    m->thresh[2] = f2 + (f2 >> 5);

    const uint32_t c1 = f0, c2 = f0 + f1, c3 = c2 + f2;   /* running cumulative */
    const uint32_t scale_index = c3 - 67u;
    const uint32_t scale = scale_index < 105u ? s_fac_scale[scale_index]
                                             : 0x80000000u / c3; /* total > 0 */
    m->cum[0] = 0;
    m->cum[1] = (c1 * scale) >> 16;
    m->cum[2] = (c2 * scale) >> 16;
    m->cum[3] = (c3 * scale) >> 16;
}

static void fac_model_init(fac_model_t *m, const uint32_t *init_freq, int window)
{
    m->n = FAC_NSYM;
    /* Public entry validates the exact window before model or reader changes. */
    m->window = window;
    m->pos = 0;
    m->count = 0;
    memset(m->hist, 0, (size_t)window);   /* only the used window (was full 3072) */
    for (int i = 0; i < FAC_NSYM; i++) m->freq[i] = init_freq[i];
    fac_model_rescale(m);
}

/* Same state transition as before, with the history write hoisted above the
 * freq update: fac_model_rescale touches neither hist nor pos/count, so the
 * reorder is observationally identical (checked symbol-by-symbol, including the
 * full history buffer, against the previous version over 20M symbols) and it
 * frees the compiler to keep `pos` in a register across the branch. */
static LHDC_HOT LHDC_ALWAYS_INLINE void fac_model_update(fac_model_t *m, int sym)
{
    uint32_t *const freq = m->freq;
    if (m->count < m->window) {
        m->hist[m->count] = (uint8_t)sym;
        m->count++;
        if (++freq[sym] >= m->thresh[sym]) fac_model_rescale(m);
    } else {
        int pos = m->pos;
        const int old = m->hist[pos];
        if (freq[old] >= 2) freq[old]--;
        m->hist[pos] = (uint8_t)sym;
        if (++pos >= m->window) pos = 0;
        m->pos = pos;
        if (++freq[sym] >= m->thresh[sym]) fac_model_rescale(m);
    }
}

/* --- FAC range decoder --- */
typedef struct {
    const uint8_t *data;
    int            len;
    int            p;
    uint32_t       range;
    uint32_t       code;
} fac_dec_t;

static LHDC_HOT LHDC_ALWAYS_INLINE uint8_t fac_byte(fac_dec_t *d)
{
    uint8_t b = (d->p < d->len) ? d->data[d->p] : 0;
    d->p++;
    return b;
}

static void fac_dec_init(fac_dec_t *d, const uint8_t *data, int len)
{
    d->data = data;
    d->len = len;
    d->p = 0;
    d->range = 0xFFFFFFFFu;
    d->code = 0;
    for (int i = 0; i < 4; i++) d->code = (d->code << 8) | fac_byte(d);
}

static LHDC_HOT LHDC_ALWAYS_INLINE int fac_decode(fac_dec_t *d, fac_model_t *m)
{
#if 0 /* excluded host diagnostics */
    extern volatile int g_lhdc_trace; extern int g_fdc;   /* reset per-channel by the entropy fn */
    uint32_t dbg_range=d->range, dbg_code=d->code;
    uint32_t dbg_cum1=m->cum[1], dbg_cum2=m->cum[2], dbg_f0=m->freq[0],dbg_f1=m->freq[1],dbg_f2=m->freq[2],dbg_cnt=m->count;
#endif
    /* Divide-free, branch-flat ternary symbol search. The ARM reference computed
     * `target = code / r` per symbol; Xtensa LX6's divide is a ~30-cycle
     * non-pipelined instruction whereas its 32-bit multiply is single-cycle, so
     * use the integer identity floor(code/r) >= c  <=>  code >= r*c (c>=0, r>0)
     * and compare r*cum[] against code directly. Bit-IDENTICAL to the old
     * target+clamp path: the `target >= FAC_TOTAL -> FAC_TOTAL-1` clamp is
     * subsumed by stopping at the last symbol. No overflow: r*cum[k] <=
     * r*FAC_TOTAL = (range>>15)<<15 <= range < 2^32.
     *
     * Written out for the ternary alphabet instead of looping on m->n: that
     * drops the trip-count reload and the loop branch, and cum[0] is always 0
     * so symbol 0 needs no subtract at all. The decoder state is pulled into
     * locals for the duration so the renormalize loop works on registers
     * instead of re-reading the struct through a pointer each iteration. */
    uint32_t range = d->range, code = d->code;
    const uint32_t r = range >> FAC_TOTAL_BITS;
    const uint32_t c1 = m->cum[1];
    uint32_t lo, hi;
    int sym;
    if (r * c1 > code) {
        sym = 0; lo = 0;  hi = c1;
    } else {
        const uint32_t c2 = m->cum[2];
        if (r * c2 > code) { sym = 1; lo = c1; hi = c2; }
        else               { sym = 2; lo = c2; hi = m->cum[3]; }
    }
    code  -= r * lo;
    range  = r * (hi - lo);
    if (range < (1u << 24)) {                 /* renormalize (was `(range>>24)==0`) */
        const uint8_t *const data = d->data;
        int p = d->p;
        const int len = d->len;
        do {
            code = (code << 8) | ((p < len) ? data[p] : 0);
            p++;
            range <<= 8;
        } while (range < (1u << 24));
        d->p = p;
    }
    d->range = range;
    d->code = code;
    fac_model_update(m, sym);
#if 0 /* excluded host diagnostics */
    if (g_lhdc_trace > 0) {
        uint32_t target = dbg_code / (dbg_range >> FAC_TOTAL_BITS);   /* trace only */
        if (g_fdc < 15 || (g_fdc >= 234 && g_fdc <= 244))
            printf("[DSTATE blk%d] [%d] sym=%d range=%08x cum1=%u cum2=%u f=[%u %u %u] cnt=%u target=%u\n",
                   g_block, g_fdc, sym, dbg_range, dbg_cum1, dbg_cum2, dbg_f0,dbg_f1,dbg_f2, dbg_cnt, target);
        g_fdc++;
    }
#endif
    return sym;
}

/* --- Rice quotient inverse (mirrors rice_dec.py, validated) --- */

/*
 * Lazily-decoded pass-2 symbol stream.
 *
 * The old flow decoded the pass-2 stream THREE times per channel:
 *   1. an eager fill of min(2*count, cap) symbols  (1920 at 192k),
 *   2. the Rice inverse, which consumes only `s2_used` of them (typically a
 *      small fraction),
 *   3. a full re-decode of s2_used symbols from a re-initialized model, purely
 *      to recover the byte position where the mantissa plane starts.
 * At 192k that is 960 (s1) + 1920 (eager) + s2_used symbols per channel, of
 * which the 1920 are mostly thrown away. The range coder is serial and is the
 * single largest CPU consumer in the decoder, so this dominated the 192k
 * budget on a chip without ARM's headroom.
 *
 * s2_at() decodes symbols on demand into the SAME array, one at a time, under
 * the SAME guard the eager loop used (`len < cap && d->p <= d->len`). That
 * makes the produced prefix identical to the old eager fill symbol-for-symbol,
 * and s2_at() returns -1 exactly where the old code would have read past
 * `s2len` -- so the Rice inverse below is a literal transcription of the array
 * version with `j < n && s2[j]` rewritten as `s2_at(j)`.
 *
 * save_p/save_range snapshot the decoder state BEFORE the most recently decoded
 * symbol, which is what removes pass 3: after the Rice inverse consumes
 * s2_used symbols, the state after exactly s2_used symbols is either the live
 * one (nothing was read ahead) or the snapshot (one lookahead symbol was
 * decoded but not consumed). See s2_state_after().
 */
typedef struct {
    fac_dec_t   *d;
    fac_model_t *m;
    int          cur_idx;    /* index of the symbol held in cur_val; -1 = none yet */
    int          cur_val;    /* the newest decoded symbol */
    int          produced;   /* symbols decoded from the range coder */
    int          hard_cap;   /* runaway guard, see lhdc_entropy_decode_spectrum_ex2 */
    int          eos;        /* stream exhausted; no more symbols will be produced */
    int          save_p;     /* d->p before cur_val was decoded */
    uint32_t     save_range; /* d->range before cur_val was decoded */
} s2_lazy_t;

#if 0 /* excluded host diagnostics */
/* Why a pass-2 stream stopped, for host harnesses. cap_hits > 0 means the
 * runaway guard fired, which should never happen on a well-formed frame; it is
 * how the truncation bug this stream used to have was found. */
int g_s2_cap_hits = 0;
int g_s2_eod_hits = 0;
#endif

/* s2[i], or -1 once the stream can produce no more.
 *
 * The Rice inverse reads with a strictly non-decreasing index (j only ever
 * advances, within a call and across calls), so at most ONE symbol is live at a
 * time: whatever was decoded last, which the caller may re-read before moving
 * on. Nothing already consumed is ever looked at again. So no backing array is
 * needed, and with it goes the `2 * count` storage bound that used to cap the
 * stream -- the bound that silently truncated dense 192 kHz / 24-bit frames and
 * fed the Rice inverse zeros, exploding the reconstructed spectrum. */
static LHDC_HOT LHDC_ALWAYS_INLINE int s2_at(s2_lazy_t *z, int i)
{
    while (i > z->cur_idx) {
        if (z->eos) return -1;
        if (z->d->p > z->d->len || z->produced >= z->hard_cap) {
#if 0 /* excluded host diagnostics */
            if (z->produced >= z->hard_cap) g_s2_cap_hits++; else g_s2_eod_hits++;
#endif
            z->eos = 1;
            return -1;
        }
        z->save_p     = z->d->p;
        z->save_range = z->d->range;
        z->cur_val    = fac_decode(z->d, z->m);
        z->cur_idx++;
        z->produced++;
    }
    return (i == z->cur_idx) ? z->cur_val : -1;
}

/* Decoder state (byte position + range) after exactly `used` pass-2 symbols --
 * what the old pass-3 re-decode computed. */
static void s2_state_after(s2_lazy_t *z, int used, int *p_out, uint32_t *range_out)
{
    if (used == z->produced - 1) {
        /* One lookahead symbol was decoded but not consumed: the state to report
         * is the one captured just before it. */
        *p_out = z->save_p; *range_out = z->save_range;
    } else if (used >= z->produced) {
        /* The Rice inverse walked past what the stream produced (end of input).
         * The pre-lazy code re-decoded `used` symbols regardless, so match it. */
        for (int i = z->produced; i < used; i++) (void)fac_decode(z->d, z->m);
        *p_out = z->d->p; *range_out = z->d->range;
    } else {
        /* Cannot happen with a non-decreasing reader, but stay defined. */
        *p_out = z->d->p; *range_out = z->d->range;
    }
}

static LHDC_HOT int rice_read_quotient(s2_lazy_t *z, int *jp)
{
    int j = *jp;
    if (s2_at(z, j) == 2) {
        uint32_t low = 0;
        int shift = 0;
        while (s2_at(z, j) == 2) {
            j++;
            int v1 = s2_at(z, j); int b1 = (v1 < 0) ? 0 : v1; j++;
            int v0 = s2_at(z, j); int b0 = (v0 < 0) ? 0 : v0; j++;
            low |= (uint32_t)(((b1 << 1) | b0)) << shift;
            shift += 2;
        }
        int ones = 0;
        while (s2_at(z, j) == 1) { ones++; j++; }
        if (s2_at(z, j) == 0) j++;
        *jp = j;
        return (int)(low | ((uint32_t)(ones + 1) << shift));
    }
    int ones = 0;
    while (ones < 3 && s2_at(z, j) == 1) { ones++; j++; }
    if (ones == 3) { *jp = j; return 3; }
    if (s2_at(z, j) == 0) j++;
    *jp = j;
    return ones;
}

/*
 * Decode `count` quantized |coeff| values from the two ternary streams.
 *   s1: pass-1 plane (length count), z: lazy pass-2 quotient stream.
 *   pivot = max(count - split, 0); run = last index whose s1 symbol == 2;
 *   pivot2 = (run < pivot) ? count : run.
 *   i <= pivot2: s1==2 -> coeff = q+2 ; else coeff = s1[i] (coeff<2).
 *   i  > pivot2: coeff = (q<<1) | s1[i]  (s1[i] is the LSB).
 */
static LHDC_HOT void rice_decode_coeffs_used(const uint8_t *s1, s2_lazy_t *z,
                                    int count, int split, int32_t *coeff,
                                    int *s2_used)
{
    int pivot = count - split;
    if (pivot < 0) pivot = 0;
    int run = 0;
    for (int i = 0; i < count; i++) if (s1[i] == 2) run = i;
    int pivot2 = (run < pivot) ? count : run;

    int j = 0;
    for (int i = 0; i < count; i++) {
        int s = s1[i];
        if (i <= pivot2) {
            if (s == 2) {
                int q = rice_read_quotient(z, &j);
                coeff[i] = q + 2;
            } else {
                coeff[i] = s;
            }
        } else {
            int q = rice_read_quotient(z, &j);
            coeff[i] = (q << 1) | s;
        }
    }
    if (s2_used) *s2_used = j;
}

/* Pack the remaining FAC bytes without a generic 64-bit bit-reader call for
 * each byte. Drain any whole cached bytes first. The stable residue is then
 * 0..7 bits, so each full output byte consumes exactly one unread input byte.
 * Both branches preserve the original scratch bytes and complete reader state;
 * use forward byte copies so source/destination overlap keeps its old meaning.
 * Unusual reader states retain the original loop. No allocation or new state. */
static LHDC_HOT LHDC_ALWAYS_INLINE int fac_read_bytes(lhdc_dec_bit_reader_t *br,
                                                    uint8_t *out, int capacity)
{
    int nb = 0;
    if (capacity <= 0) return 0;
    while (br->cache_bits >= 8 && nb < capacity && lhdc_bit_reader_remaining(br) >= 8)
        out[nb++] = (uint8_t)lhdc_bit_reader_read(br, 8);
    if (nb == capacity) return nb;
    if (br->cache_bits < 0 || br->cache_bits > 7 || br->byte_pos > br->data_bytes ||
        br->data_bytes - br->byte_pos > SIZE_MAX / 8u) {
        while (lhdc_bit_reader_remaining(br) >= 8 && nb < capacity)
            out[nb++] = (uint8_t)lhdc_bit_reader_read(br, 8);
        return nb;
    }
    size_t bytes = br->data_bytes - br->byte_pos;
    if (bytes > (size_t)(capacity - nb)) bytes = (size_t)(capacity - nb);
    if (!bytes) return nb;
    const uint8_t *src = br->data + br->byte_pos;
    uint8_t *dst = out + nb;
    const unsigned bits = (unsigned)br->cache_bits;
    if (!bits) {
        for (size_t i = 0; i < bytes; ++i) dst[i] = src[i];
        br->cache = 0;
    } else {
        const uint32_t mask = (1u << bits) - 1u;
        uint32_t residue = (uint32_t)br->cache & mask;
        for (size_t i = 0; i < bytes; ++i) {
            const uint32_t value = src[i];
            dst[i] = (uint8_t)((residue << (8u - bits)) | (value >> bits));
            residue = value & mask;
        }
        br->cache = residue;
    }
    br->byte_pos += bytes;
    return nb + (int)bytes;
}

/*
 * Public entry: decode the spectral coefficients for one channel. The bit
 * reader must be positioned at the start of the FAC byte stream (i.e. right
 * after the per-channel leading section). `count` = nzc (significant coeffs),
 * `split` and the MA windows come from the band config / ECB.
 *
 * NOTE: the current decoder front-end (lhdc_dec.c) does not yet thread count /
 * split / windows / sign-plane offset through to here; this function exposes the
 * validated core. lhdc_entropy_decode_spectrum below adapts the legacy call.
 */
LHDC_HOT lhdc_dec_ret_t lhdc_entropy_decode_spectrum_ex2(int32_t *quant_spectrum,
                                                 int num_coeffs,
                                                 lhdc_dec_bit_reader_t *br,
                                                 int count, int split,
                                                 int ma_win1, int ma_win2,
                                                 int *fac_bytes_out,
                                                 uint8_t *scratch_fac, int scratch_fac_cap,
                                                 uint8_t *scratch_s1,
                                                 uint8_t *scratch_s2, int scratch_s2_cap)
{
    if (ma_win1 < 1 || ma_win1 > LHDC_FAC_HISTORY1_CAPACITY ||
        ma_win2 < 1 || ma_win2 > LHDC_FAC_HISTORY2_CAPACITY)
        return LHDC_DEC_INVALID_PARAM;
    /*
     * Scratch buffers (facbuf / s1 / s2) come from the heap-allocated decoder
     * workspace to avoid large .bss; passed in via the caller. The leading
     * section consumed up to br's current bit position; the FAC stream is the
     * MSB-first bit packing from here on.
     */
    uint8_t *facbuf = scratch_fac;
    uint8_t *s1 = scratch_s1;
#if 0 /* excluded host diagnostics */
    { extern volatile int g_lhdc_trace; if (g_lhdc_trace > 0) { g_fdc = 0; g_block++; } }
#endif
    const int nb = fac_read_bytes(br, facbuf, scratch_fac_cap);

    if (count < 1 || count > num_coeffs) count = num_coeffs;

    fac_dec_t d;
    fac_dec_init(&d, facbuf, nb);

    const int slot = LHDC_DEC_SLOT();       /* this core's private model set */
    fac_model_t *const pm1 = &s_fac_m[slot][0];   /* stream-1 model (.bss) */
    fac_model_t *const pm2 = &s_fac_m[slot][1];   /* stream-2 model (.bss) */
    lhdc_entropy_alloc_internal();          /* point pm1/pm2 at their right-sized hist buffers (idempotent) */

    fac_model_init(pm1, FAC_FQ1, ma_win1);
    for (int i = 0; i < count; i++) s1[i] = (uint8_t)fac_decode(&d, pm1);

    fac_model_init(pm2, FAC_FQ2, ma_win2);
    /* Pass-2 symbols are pulled on demand and only one is ever live, so the only
     * real bound is the encoded byte stream running out. `hard_cap` exists purely
     * so a corrupt frame cannot spin: the adaptive model can, in principle, code
     * a symbol in far less than a bit, so "bytes remaining" alone does not bound
     * the symbol count. 16 per coefficient is an order of magnitude above the
     * worst case observed across every rate/depth/bitrate.
     *
     * The previous bound was 2 per coefficient AND it truncated the stream
     * silently -- dense 192 kHz / 24-bit frames at 900k-1000k need more than
     * that, so the Rice inverse read zeros and the frame's spectrum exploded
     * (peaks ~26x the clip limit -> concealment -> audible statics). scratch_s2
     * is no longer used for storage and is kept only for API compatibility. */
    (void)scratch_s2; (void)scratch_s2_cap;
    s2_lazy_t z;
    z.d = &d; z.m = pm2;
    z.cur_idx = -1; z.cur_val = 0; z.produced = 0; z.eos = 0;
    z.hard_cap = count * 16;
    z.save_p = d.p; z.save_range = d.range;

#if 0 /* excluded host diagnostics */
    { extern volatile int g_lhdc_trace;
      if (g_lhdc_trace > 0) {
        printf("[ENT] count=%d split=%d ma_win1=%d ma_win2=%d nb=%d (s2 lazy)\n",
               count, split, ma_win1, ma_win2, nb);
        printf("[ENT] s1full:");
        for (int i = 0; i < count; i++) printf(" %d", s1[i]);
        printf("\n");
      } }
#endif

    memset(quant_spectrum, 0, num_coeffs * sizeof(int32_t));
    int s2_used = 0;
    rice_decode_coeffs_used(s1, &z, count, split, quant_spectrum, &s2_used);

    if (fac_bytes_out) {
        /* Byte position after exactly s2_used pass-2 symbols. The old code got
         * this by re-initializing the model and re-decoding s2_used symbols from
         * a snapshot taken after s1; the lazy stream already holds that state (or
         * the snapshot taken just before its single lookahead symbol), so the
         * whole third pass over the range coder is gone. */
        int consumed = 0;
        uint32_t final_range = 0;
        s2_state_after(&z, s2_used, &consumed, &final_range);
        if (consumed < 0) consumed = 0;
        *fac_bytes_out = consumed;            /* raw bytes pulled */
        g_fac_final_range[slot] = final_range;      /* leftover rule applied in lhdc_dec.c */
#if 0 /* excluded host diagnostics */
        if (getenv("LHDC_LEFTOVER_LOG"))
            printf("[LEFTOVER] consumed=%d range=%08x s2_used=%d rule=%d\n",
                   consumed, final_range, s2_used, (final_range <= 0x2000000u) ? 2 : 3);
#endif
    }
    return LHDC_DEC_OK;
}

lhdc_dec_ret_t lhdc_entropy_decode_bpc(int32_t *quant_spectrum,
                                         int num_coeffs,
                                         lhdc_dec_bit_reader_t *br)
{
    /* BPC/residual is folded into the Rice streams above; nothing extra here. */
    (void)quant_spectrum; (void)num_coeffs; (void)br;
    return LHDC_DEC_OK;
}

#endif
