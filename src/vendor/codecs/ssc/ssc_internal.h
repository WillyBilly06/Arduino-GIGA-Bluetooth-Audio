/* SSC decoder internal types shared between modules. */
#ifndef SSC_INTERNAL_H
#define SSC_INTERNAL_H

#include <stdint.h>
#include "ssc_ec.h"

#define SSC_FRAME 864        /* samples per channel per frame */
#define SSC_NB_BANDS 22

/* Fast float math for the reconstruction path (libm versions are slow on MCUs). Only audio values depend on
 * these, never the bitstream parsing. */
/* 2^x, from libopus celt_exp2 (float build): relative error < 1e-4. */
static inline float ssc_exp2(float x) {
    int integer;
    float frac;
    union { uint32_t i; float f; } res;
    integer = (int)x;
    if (x < (float)integer) integer--; /* floor */
    if (integer < -50) return 0.f;
    frac = x - (float)integer;
    res.f = 0.99992522f + frac * (0.69583354f + frac * (0.22606716f + 0.078024523f * frac));
    res.i = (res.i + ((uint32_t)integer << 23)) & 0x7fffffffu;
    return res.f;
}

/* cos(pi/2 * x) for x in [0, 1]: even polynomial, max error 2e-5. */
static inline float ssc_cos_half_pi(float x) {
    float x2 = x * x;
    return 1.f + x2 * (-1.2336979f + x2 * (0.25366235f + x2 * (-0.020863481f + x2 * 0.00091892557f)));
}

#include "ssc_decoder.h"

/* Allocation view of a mode (ssc_alloc.c). */
typedef struct {
    const short *eBands;                  /* 23 entries */
    const unsigned char *allocVectors[2]; /* [0]: bitrate <= 192000, [1]: bitrate > 192000 */
    const short *logN;                    /* 22 */
    const short *cache_index;             /* 5*22, row = LM+1 */
    const unsigned char *cache_bits;
    const int *cap;                       /* 22, final cap[] in 1/8 bits */
} ssc_alloc_mode;

extern const ssc_alloc_mode ssc_alloc_mode_44k;
extern const ssc_alloc_mode ssc_alloc_mode_96k;

int ssc_compute_allocation_dec(const ssc_alloc_mode *m, int end, const int *offsets,
                               int alloc_trim, int hi_rate, int32_t total, int32_t *balance,
                               int *pulses, int *ebits, int *fine_priority,
                               int *intensity, int *dual_stereo, ec_dec *ec);

/* Band-shape view of a mode (ssc_bands.c). */
typedef struct {
    const short *eBands;      /* 23 */
    const short *logN;        /* 22 */
    const short *cache_index; /* 5*22 */
    const unsigned char *cache_bits;
} ssc_band_mode;

typedef struct {
    float norm[2 * 720];          /* folding memory, both channels (M*eBands[21] max = 720) */
    float lowband_scratch[144];   /* largest band: 18*8 */
} ssc_qab_work;

/* X_, Y_: the two channels' shapes (864 floats each, need not be adjacent). */
void ssc_quant_all_bands_dec(const ssc_band_mode *m, int end, float *X_, float *Y_,
                             unsigned char *collapse_masks, const int *pulses, int shortBlocks, int spread,
                             int dual_stereo, int intensity, const int *tf_res, int32_t total_bits,
                             int32_t balance, ec_dec *ec, int codedBands, uint32_t *seed, ssc_qab_work *ws);
void ssc_anti_collapse(const ssc_band_mode *m, float *X_, float *Y_, const unsigned char *collapse_masks,
                       int start, int end, const float *logE, const float *prev1logE,
                       const float *prev2logE, const int *pulses, uint32_t seed);

/* Synthesis (ssc_synth.c). */
typedef struct { float r, i; } ssc_cpx;

extern const ssc_cpx ssc_fft_tw[432];
extern const ssc_cpx ssc_pre_long[432], ssc_post_long[432];
extern const ssc_cpx ssc_pre_short[54], ssc_post_short[54];

/* Per-frame scratch, shared by the band decoder and the synthesis (nothing in it survives a frame):
 *   band decode: ssc_qab_work (folding memory)                                       1584 floats
 *   long IMDCT : FFT input, then the DCT-IV output (the FFT output goes to the frame) 864 floats
 *   short IMDCT: 972 overlap-add accumulator + 3 x 108 per-block buffers            1296 floats
 * Between frames it is unused; ssc_output_buffer() lends it to the caller (SSC_OUTPUT_BYTES). */
#define SSC_SYNTH_SCRATCH_FLOATS (864 + 108 + 3 * 108)
typedef union {
    ssc_qab_work qab;
    float synth[SSC_SYNTH_SCRATCH_FLOATS];
    double align_;
} ssc_scratch;

/* hist[0 .. 1026): past output (the comb filter looks back up to 1024+2 samples).
 * hist[1026 .. 1890): the current frame. During decode it first holds this channel's band shapes / MDCT
 * spectrum (the previous frame there is already duplicated in the history), then the synthesised and
 * postfiltered output, which stays readable until the next decode. */
typedef struct {
    float hist[1026 + 864];
    int T_old;
    float g_old;
} ssc_postfilter_state;

extern const float ssc_window[108];
extern const float ssc_eMeans[25];

void ssc_denormalise_bands(const short *eBands, const float *X, float *freq, const float *logE,
                           int start, int end);
/* io: in = 864 MDCT coefficients, out = 864 time samples (before postfilter). scratch: SSC_SYNTH_SCRATCH_FLOATS. */
void ssc_synthesis_frame(float *scratch, float *io, int shortBlocks, float *ola_mem);
void ssc_postfilter_init(ssc_postfilter_state *pf);
float *ssc_postfilter_buffer(ssc_postfilter_state *pf);
void ssc_postfilter_run(ssc_postfilter_state *pf, int pf_on, int T, int qg);

#endif
