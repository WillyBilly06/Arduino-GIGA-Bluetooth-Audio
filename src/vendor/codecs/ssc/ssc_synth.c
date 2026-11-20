/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* Part of the SSC decoder. Derived from libopus 1.1 (BSD license, see COPYING.opus) and from
 * analysis of Samsung's SSC encoder; see docs/notes for the format notes. */
#include "ssc_tables.h"
#include "ssc_internal.h"
#include <math.h>
#include <string.h>

#define SSC_N            864   /* frame size = long-block coefficient count (N2 of the 1728 MDCT)   */
#define SSC_OVERLAP      108   /* window overlap (both long and short blocks)                       */
#define SSC_NB_SHORT     8     /* short blocks per transient frame                                  */
#define SSC_SHORT_N      108   /* coefficients per short block (N2 of the 216 MDCT)                 */
#define SSC_NBANDS       22    /* mode->nbEBands (all 22 are computed; see report for coded 'end')  */
#define SSC_LM           3     /* M = 1<<LM = 8                                                    */
#define SSC_PF_MINPERIOD 15    /* COMBFILTER_MINPERIOD                                              */
#define SSC_PF_MAXPERIOD 1024  /* COMBFILTER_MAXPERIOD; transmitted pitch <= 1022                  */
#define SSC_PF_HIST      (SSC_PF_MAXPERIOD + 2) /* postfilter needs y[n-T-2], T <= 1022 -> 1024 back */

#ifndef SSC_PI
#define SSC_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------------------------------
 * Window: the table the encoder actually uses (.rodata 0x3d80, 108 x int32 Q31, divided by 2^31).
 * It is the Vorbis power-complementary window
 *     w[i] = sin(pi/2 * sin^2(pi*(i+0.5)/(2*108))),   w[i]^2 + w[107-i]^2 = 1
 * (max |table - formula| = 2.3e-10). win_square (.data 0x1ff80) = w^2 is used by the comb filter.
 * ---------------------------------------------------------------------------------------------- */
const float ssc_window[SSC_OVERLAP] = {
   8.307024837e-05f, 7.475251332e-04f, 2.075871453e-03f, 4.066978116e-03f, 6.719139405e-03f, 1.003006287e-02f,
   1.399685349e-02f, 1.861599693e-02f, 2.388333622e-02f, 2.979404852e-02f, 3.634261712e-02f, 4.352280218e-02f,
   5.132761179e-02f, 5.974926753e-02f, 6.877917284e-02f, 7.840787945e-02f, 8.862505620e-02f, 9.941945365e-02f,
   1.107788784e-01f, 1.226901603e-01f, 1.351391296e-01f, 1.481105927e-01f, 1.615883140e-01f, 1.755550024e-01f,
   1.899922988e-01f, 2.048807754e-01f, 2.201999347e-01f, 2.359282179e-01f, 2.520430214e-01f, 2.685207147e-01f,
   2.853366728e-01f, 3.024653099e-01f, 3.198801246e-01f, 3.375537498e-01f, 3.554580142e-01f, 3.735640068e-01f,
   3.918421515e-01f, 4.102622881e-01f, 4.287937614e-01f, 4.474055134e-01f, 4.660661849e-01f, 4.847442196e-01f,
   5.034079752e-01f, 5.220258376e-01f, 5.405663364e-01f, 5.589982672e-01f, 5.772908097e-01f, 5.954136522e-01f,
   6.133371112e-01f, 6.310322522e-01f, 6.484710095e-01f, 6.656262991e-01f, 6.824721326e-01f, 6.989837214e-01f,
   7.151375790e-01f, 7.309116144e-01f, 7.462852201e-01f, 7.612393512e-01f, 7.757565966e-01f, 7.898212383e-01f,
   8.034193069e-01f, 8.165386217e-01f, 8.291688231e-01f, 8.413013942e-01f, 8.529296718e-01f, 8.640488475e-01f,
   8.746559564e-01f, 8.847498582e-01f, 8.943312061e-01f, 9.034024077e-01f, 9.119675732e-01f, 9.200324607e-01f,
   9.276044052e-01f, 9.346922487e-01f, 9.413062553e-01f, 9.474580232e-01f, 9.531603935e-01f, 9.584273491e-01f,
   9.632739099e-01f, 9.677160303e-01f, 9.717704854e-01f, 9.754547602e-01f, 9.787869370e-01f, 9.817855805e-01f,
   9.844696242e-01f, 9.868582557e-01f, 9.889708045e-01f, 9.908266324e-01f, 9.924450233e-01f, 9.938450786e-01f,
   9.950456130e-01f, 9.960650578e-01f, 9.969213633e-01f, 9.976319089e-01f, 9.982134164e-01f, 9.986818694e-01f,
   9.990524338e-01f, 9.993393887e-01f, 9.995560590e-01f, 9.997147527e-01f, 9.998267074e-01f, 9.999020393e-01f,
   9.999496979e-01f, 9.999774261e-01f, 9.999917299e-01f, 9.999978454e-01f, 9.999997206e-01f, 9.999999967e-01f,
};

/* Band layouts (x8 = MDCT bin, LM=3). 44.1k/48k share one mode; 96k has its own. */



/* eMeans (identical to CELT; encoder table ssc_eMeans_tmp is Q4 bytes, value/16). */
const float ssc_eMeans[25] = {
   6.437500f, 6.250000f, 5.750000f, 5.312500f, 5.062500f, 4.812500f, 4.500000f, 4.375000f,
   4.875000f, 4.687500f, 4.562500f, 4.437500f, 4.875000f, 4.625000f, 4.312500f, 4.500000f,
   4.375000f, 4.625000f, 4.750000f, 4.437500f, 3.750000f, 3.750000f, 3.750000f, 3.750000f, 3.750000f
};

/* ================================================================================================
 * 1. denormalise_bands  (CELT 1.1 float denormalise_bands, M = 8)
 *    X      : unit-norm shape per band (output of PVQ/quant_all_bands), interleaved if shortBlocks
 *             (index j*8+b = frequency j of short block b, exactly CELT's layout; bands stay contiguous).
 *    logE   : decoded band log-energy in log2 units (coarse+fine), *without* eMeans.
 *    freq   : MDCT coefficients for one channel, N = 864, in 16-bit PCM units.
 *    Bins below eBands[start]*8 and from eBands[end]*8 to 863 are zeroed.
 * ============================================================================================== */
void ssc_denormalise_bands(const short *eBands, const float *X, float *freq, const float *logE,
                           int start, int end)
{
   const int M = 1 << SSC_LM;
   int i, j;
   for (j = 0; j < M * eBands[start]; j++) freq[j] = 0.f;
   for (i = start; i < end; i++) {
      float g = ssc_exp2(logE[i] + ssc_eMeans[i]);   /* = bandE in 16-bit PCM units */
      for (j = M * eBands[i]; j < M * eBands[i + 1]; j++) freq[j] = X[j] * g;
   }
   for (j = M * eBands[end]; j < SSC_N; j++) freq[j] = 0.f;
}

/* ================================================================================================
 * 2. IMDCT (sizes 1728 and 216) + overlap-add.
 *
 * Encoder forward transform (bit-matched to opus 1.1 clt_mdct_forward, checked to +-3 LSB):
 *    X[k] = (4/N) * sum_{n=0}^{N-1} wf[n] x[n] cos(2*pi/N*(n + 1/2 + N/4)*(k + 1/2)),  k < N/2
 * N = 1728 (long) or 216 (short). The input block is the 972 (or 216) samples
 * [108 previous | 864 new] placed in the middle of the length-N window wf: pad = (N/2 - 108)/2 zeros
 * (378 long, 0 short), 108 rising w[], ones, 108 falling w[107-i], zeros.
 * Inverse used here (no extra scaling needed; PR verified):
 *    y[m] = wf[pad+m] * sum_k X[k] cos(2*pi/N*(pad+m + 1/2 + N/4)*(k + 1/2)),  m < N/2 + 108
 * then overlap-add with hop N/2 (long) or 108 (short block b at offset 108*b).
 * Output of a frame = 864 samples, delayed by 108 samples relative to the encoder's new input.
 * Fast path: N/2-point DCT-IV via an N/4-point complex FFT (432 = 4*4*3*3*3, 54 = 2*3*3*3).
 * opus 1.1 clt_mdct_backward (float) with this window and N=1728/maxshift=3 is an equivalent alternative.
 * ============================================================================================== */


static const int ssc_factors_432[] = {4, 108, 4, 27, 3, 9, 3, 3, 3, 1};  /* (radix, remaining) pairs */
static const int ssc_factors_54[]  = {2, 27, 3, 9, 3, 3, 3, 1};

/* Recursive mixed-radix decimation-in-time forward DFT with dedicated radix-2/3/4 butterflies
 * (same structure and conventions as kiss_fft in libopus). out[k] = sum_n in[n*in_stride] exp(-2 pi i n k / N).
 * tw is the 432-point table exp(-2 pi i k / 432); twstride = 432 / N_full. Twiddle indices never exceed 432,
 * so no modulo is needed. Recursion depth <= 5. */
#define CMUL(o, a, b) do { float ar_ = (a).r, ai_ = (a).i; (o).r = ar_ * (b).r - ai_ * (b).i; \
                           (o).i = ar_ * (b).i + ai_ * (b).r; } while (0)

static void ssc_fft_work(ssc_cpx *out, const ssc_cpx *in, int fstride, int in_stride, const int *factors,
                         const ssc_cpx *tw, int twstride)
{
   const int p = factors[0], m = factors[1];
   const int step = fstride * twstride;
   int k, q;
   if (m == 1) {
      for (q = 0; q < p; q++) out[q] = in[q * fstride * in_stride];
   } else {
      for (q = 0; q < p; q++)
         ssc_fft_work(out + q * m, in + q * fstride * in_stride, fstride * p, in_stride, factors + 2, tw, twstride);
   }
   if (p == 4) {
      ssc_cpx *F = out;
      for (k = 0; k < m; k++, F++) {
         ssc_cpx s0, s1, s2, s3, s4, s5;
         CMUL(s0, F[m], tw[k * step]);
         CMUL(s1, F[2 * m], tw[2 * k * step]);
         CMUL(s2, F[3 * m], tw[3 * k * step]);
         s5.r = F[0].r - s1.r; s5.i = F[0].i - s1.i;
         F[0].r += s1.r; F[0].i += s1.i;
         s3.r = s0.r + s2.r; s3.i = s0.i + s2.i;
         s4.r = s0.r - s2.r; s4.i = s0.i - s2.i;
         F[2 * m].r = F[0].r - s3.r; F[2 * m].i = F[0].i - s3.i;
         F[0].r += s3.r; F[0].i += s3.i;
         F[m].r = s5.r + s4.i; F[m].i = s5.i - s4.r;
         F[3 * m].r = s5.r - s4.i; F[3 * m].i = s5.i + s4.r;
      }
   } else if (p == 3) {
      const float epi3_i = tw[m * step].i; /* -sin(2 pi / 3) */
      ssc_cpx *F = out;
      for (k = 0; k < m; k++, F++) {
         ssc_cpx s0, s1, s2, s3;
         CMUL(s1, F[m], tw[k * step]);
         CMUL(s2, F[2 * m], tw[2 * k * step]);
         s3.r = s1.r + s2.r; s3.i = s1.i + s2.i;
         s0.r = (s1.r - s2.r) * epi3_i; s0.i = (s1.i - s2.i) * epi3_i;
         F[m].r = F[0].r - 0.5f * s3.r; F[m].i = F[0].i - 0.5f * s3.i;
         F[0].r += s3.r; F[0].i += s3.i;
         F[2 * m].r = F[m].r + s0.i; F[2 * m].i = F[m].i - s0.r;
         F[m].r -= s0.i; F[m].i += s0.r;
      }
   } else { /* p == 2 */
      ssc_cpx *F = out;
      for (k = 0; k < m; k++, F++) {
         ssc_cpx t;
         CMUL(t, F[m], tw[k * step]);
         F[m].r = F[0].r - t.r; F[m].i = F[0].i - t.i;
         F[0].r += t.r; F[0].i += t.i;
      }
   }
}

/* DCT-IV core of one inverse MDCT block: u[j] = sum_k X[k] cos(pi/N2 (j+1/2)(k+1/2)), j < N2.
 * X: coefficients with stride (1 long, 8 short). a, b: N2/2 complex each, distinct from each other and from X
 * (b may be X itself once it is no longer needed: the FFT runs after the pre-twiddle has consumed X).
 * u (N2 floats) may alias a, which is dead after the FFT. */
static void ssc_dct4(const float *X, int stride, int isShort, ssc_cpx *a, ssc_cpx *b, float *u)
{
   const int N2 = isShort ? SSC_SHORT_N : SSC_N;
   const int N4 = N2 >> 1;
   const ssc_cpx *pre = isShort ? ssc_pre_short : ssc_pre_long;
   const ssc_cpx *post = isShort ? ssc_post_short : ssc_post_long;
   int k;
   for (k = 0; k < N4; k++) {
      float re = X[(2 * k) * stride], im = X[(N2 - 1 - 2 * k) * stride];
      a[k].r = re * pre[k].r - im * pre[k].i;
      a[k].i = re * pre[k].i + im * pre[k].r;
   }
   if (isShort) ssc_fft_work(b, a, 1, 1, ssc_factors_54, ssc_fft_tw, 8);
   else         ssc_fft_work(b, a, 1, 1, ssc_factors_432, ssc_fft_tw, 1);
   for (k = 0; k < N4; k++) {
      float zr = b[k].r * post[k].r - b[k].i * post[k].i;
      float zi = b[k].r * post[k].i + b[k].i * post[k].r;
      u[2 * k] = zr;
      u[N2 - 1 - 2 * k] = -zi;
   }
}

/* Unfolding DCT-IV -> windowed IMDCT samples. With Q = N2/2 and n = pad + m (pad = (N2-108)/2):
 *    v[m] = u[n+Q] (n < Q),  -u[3Q-1-n] (n < 3Q),  -u[n-3Q] (otherwise);   y[m] = v[m] * w[m],  m < N2+108
 * w = rising window over m < 108, 1 up to N2, falling window over the last 108.
 *  long  (N2 = 864, pad = 378): m in [0,54) u[810+m]; [54,918) -u[917-m]; [918,972) -u[m-918]
 *  short (N2 = 108, pad = 0):   m in [0,54) u[54+m];  [54,162) -u[161-m]; [162,216) -u[m-162]
 * The loops below are split at these points; each sample gets exactly the product v * w it got before
 * (w = 1 in the flat part, where v * 1 = v). */

/* Short block: yo[0 .. 216). */
static void ssc_unfold_short(const float *u, float *yo)
{
   int m;
   for (m = 0; m < 54; m++)    yo[m] =  u[54 + m] * ssc_window[m];
   for (m = 54; m < 108; m++)  yo[m] = -u[161 - m] * ssc_window[m];
   for (m = 108; m < 162; m++) yo[m] = -u[161 - m] * ssc_window[215 - m];
   for (m = 162; m < 216; m++) yo[m] = -u[m - 162] * ssc_window[215 - m];
}

/* Synthesis of one frame for one channel.
 *   scratch   : SSC_SYNTH_SCRATCH_FLOATS of per-frame scratch.
 *   io        : in: 864 coefficients (long block) or 8 interleaved short blocks (io[j*8+b]);
 *               out: 864 output samples (16-bit PCM units, before postfilter).
 *   ola_mem   : 108-sample overlap memory of this channel (zero at start).
 * Long and short frames can be mixed freely (same 108 overlap). */
void ssc_synthesis_frame(float *scratch, float *io, int shortBlocks, float *ola_mem)
{
   int m, b;
   if (!shortBlocks) {
      /* pre-twiddle io -> scratch, FFT scratch -> io, post-twiddle io -> scratch (u), unfold u -> io + ola */
      const float *u = scratch;
      ssc_dct4(io, 1, 0, (ssc_cpx *)scratch, (ssc_cpx *)io, scratch);
      for (m = 0; m < 54; m++)    io[m] =  u[810 + m] * ssc_window[m];
      for (m = 54; m < 108; m++)  io[m] = -u[917 - m] * ssc_window[m];
      for (m = 108; m < 864; m++) io[m] = -u[917 - m];
      for (m = 0; m < SSC_OVERLAP; m++) io[m] += ola_mem[m];
      for (m = 864; m < 918; m++) ola_mem[m - 864] = -u[917 - m] * ssc_window[971 - m];
      for (m = 918; m < 972; m++) ola_mem[m - 864] = -u[m - 918] * ssc_window[971 - m];
   } else {
      /* y: overlap-add accumulator; per block: a, b (54 complex each), u (108); the block output blk
       * (216) reuses a and b, which are dead by then. io is only read until the last block. */
      float *y = scratch;
      ssc_cpx *a = (ssc_cpx *)(scratch + SSC_N + SSC_OVERLAP);
      ssc_cpx *bb = a + SSC_SHORT_N / 2;
      float *u = (float *)(bb + SSC_SHORT_N / 2);
      float *blk = (float *)a;
      memset(y, 0, (SSC_N + SSC_OVERLAP) * sizeof(float));
      for (b = 0; b < SSC_NB_SHORT; b++) {
         ssc_dct4(io + b, SSC_NB_SHORT, 1, a, bb, u);
         ssc_unfold_short(u, blk);
         for (m = 0; m < 2 * SSC_SHORT_N; m++) y[b * SSC_SHORT_N + m] += blk[m];
      }
      for (m = 0; m < SSC_OVERLAP; m++) y[m] += ola_mem[m];
      memcpy(io, y, SSC_N * sizeof(float));
      memcpy(ola_mem, y + SSC_N, SSC_OVERLAP * sizeof(float));
   }
}

/* ================================================================================================
 * 3. Postfilter (inverse of the encoder's prefilter).
 * Encoder: y = x + comb(x) with gains -g (FIR on unfiltered input), CELT tapset 0 ONLY
 * (no tapset symbol in the bitstream; the asm hard-codes 0.3066406250/0.2170410156/0.1296386719),
 * crossfade over the first 108 new samples with f = w[i]^2 (win_square): (1-f)*old + f*new.
 * Decoder: CELT comb_filter run IN PLACE with +g (IIR) on the synthesised output. Because the
 * output is delayed by 108 samples, the old->new crossfade happens at output offset 108
 * (= shortMdctSize), exactly as celt_decoder.c does for LM != 0.
 * Bitstream (unchanged from CELT minus tapset): pf_on bit; octave = uint(6);
 *   T = (16 << octave) + bits(4 + octave) - 1  (15..1022);  qg = bits(3);  g = 0.09375*(qg+1).
 * ============================================================================================== */
static const float ssc_pf_gains[3] = {0.3066406250f, 0.2170410156f, 0.1296386719f};

static void ssc_comb_filter_const(float *y, const float *x, int T, int N, float g10, float g11, float g12)
{
   float x0, x1, x2, x3, x4;
   int i;
   x4 = x[-T - 2]; x3 = x[-T - 1]; x2 = x[-T]; x1 = x[-T + 1];
   for (i = 0; i < N; i++) {
      x0 = x[i - T + 2];
      y[i] = x[i] + g10 * x2 + g11 * (x1 + x3) + g12 * (x0 + x4);
      x4 = x3; x3 = x2; x2 = x1; x1 = x0;
   }
}

/* CELT 1.1 comb_filter, float, tapset 0 for both halves. y may equal x (decoder: in place).
 * x must have SSC_PF_HIST valid samples before x[0]. */
void ssc_comb_filter(float *y, float *x, int T0, int T1, int N, float g0, float g1, int overlap)
{
   float g00, g01, g02, g10, g11, g12, x0, x1, x2, x3, x4;
   int i;
   if (g0 == 0.f && g1 == 0.f) {
      if (x != y) memmove(y, x, N * sizeof(float));
      return;
   }
   g00 = g0 * ssc_pf_gains[0]; g01 = g0 * ssc_pf_gains[1]; g02 = g0 * ssc_pf_gains[2];
   g10 = g1 * ssc_pf_gains[0]; g11 = g1 * ssc_pf_gains[1]; g12 = g1 * ssc_pf_gains[2];
   x1 = x[-T1 + 1]; x2 = x[-T1]; x3 = x[-T1 - 1]; x4 = x[-T1 - 2];
   for (i = 0; i < overlap; i++) {
      float f = ssc_window[i] * ssc_window[i];
      x0 = x[i - T1 + 2];
      y[i] = x[i]
           + (1.f - f) * g00 * x[i - T0]
           + (1.f - f) * g01 * (x[i - T0 + 1] + x[i - T0 - 1])
           + (1.f - f) * g02 * (x[i - T0 + 2] + x[i - T0 - 2])
           + f * g10 * x2 + f * g11 * (x1 + x3) + f * g12 * (x0 + x4);
      x4 = x3; x3 = x2; x2 = x1; x1 = x0;
   }
   if (g1 == 0.f) {
      if (x != y) memmove(y + overlap, x + overlap, (N - overlap) * sizeof(float));
      return;
   }
   ssc_comb_filter_const(y + i, x + i, T1, N - i, g10, g11, g12);
}


void ssc_postfilter_init(ssc_postfilter_state *pf)
{
   memset(pf, 0, sizeof(*pf));
   pf->T_old = SSC_PF_MINPERIOD;
}

/* The synthesis writes the frame straight into the postfilter history (ssc_postfilter_buffer), so the
 * decoder needs no separate output buffer. The filtered frame stays readable there until the next frame. */
float *ssc_postfilter_buffer(ssc_postfilter_state *pf)
{
   return pf->hist + SSC_PF_HIST;
}

void ssc_postfilter_run(ssc_postfilter_state *pf, int pf_on, int T, int qg)
{
   float *x = pf->hist + SSC_PF_HIST;
   int T_new = pf_on ? T : 0;
   float g_new = pf_on ? 0.09375f * (float)(qg + 1) : 0.f;
   int T_old = pf->T_old < SSC_PF_MINPERIOD ? SSC_PF_MINPERIOD : pf->T_old;
   /* first 108 output samples belong to the previous encoder frame: steady old filter */
   ssc_comb_filter(x, x, T_old, T_old, SSC_SHORT_N, pf->g_old, pf->g_old, SSC_OVERLAP);
   /* crossfade old -> new over output samples 108..215, then steady new filter */
   ssc_comb_filter(x + SSC_SHORT_N, x + SSC_SHORT_N, T_old, T_new < SSC_PF_MINPERIOD ? SSC_PF_MINPERIOD : T_new,
                   SSC_N - SSC_SHORT_N, pf->g_old, g_new, SSC_OVERLAP);
   /* keep the last 1026 samples as history; x itself is left intact (no overlap with the destination) */
   memmove(pf->hist, pf->hist + SSC_N, SSC_PF_HIST * sizeof(float));
   pf->T_old = T_new;
   pf->g_old = g_new;
}

/* ================================================================================================
 * 4. De-emphasis / output scaling.
 * The encoder has NO pre-emphasis (ssc_preemphasis and the inlined copy in ssc_encode are a pure shift:
 * <<12 for 16-bit input, <<4 for 24-bit input and for every "hq" bitrate). So the decoder applies no
 * de-emphasis filter, only scaling. With opus' deemphasis() set mode->preemph = {0,0,0,0}.
 * float PCM in [-1,1): pcm = sig / 32768.  int16: round(sig).  int24: round(sig * 256).
 * (hq bitrates with 16-bit input would make the encoder's signal 2^-8 of normal, i.e. -48 dB; the
 *  stack presumably feeds 24-bit for hq. Nothing the decoder can detect.)
 * ============================================================================================== */
void ssc_output_float(const float *sig, float *pcm, int n, int stride)
{
   int i;
   for (i = 0; i < n; i++) pcm[i * stride] = sig[i] * (1.f / 32768.f);
}

/* ================================================================================================
 * 5. OPTIONAL: encoder "headroom" HF attenuation (normal_headroom / uhq_headroom), applied to the MDCT
 * spectrum before the band energies are measured, so it is baked into the coded energies. It is a fixed
 * function of (mode, bitrate, hq) and could be undone by dividing freq[j] by this gain BEFORE the IMDCT
 * (same index j for long and interleaved short spectra: the encoder applies it per index in both cases).
 * It is <= 1.7 dB and only above ~6-16 kHz; Samsung's decoder most likely does NOT undo it, so the
 * default is to leave it. Returns the gain the encoder applied to bin j (1.0 where it did nothing or
 * where the encoder read garbage past its table: 96k mode, non-hq, bins 816..863).
 * ============================================================================================== */
static const short ssc_headroom_gain_tab[240] = {
   31308, 31235, 31162, 31089, 31016, 30943, 30870, 30797, 30744, 30691, 30638, 30585, 30532, 30479, 30426,
   30373, 30320, 30267, 30234, 30201, 30168, 30135, 30102, 30069, 30036, 30003, 29970, 29937, 29904, 29871,
   29838, 29805, 29786, 29767, 29748, 29729, 29710, 29691, 29672, 29653, 29634, 29615, 29596, 29577, 29558,
   29539, 29520, 29501, 29482, 29469, 29456, 29443, 29430, 29419, 29412, 29405, 29398, 29391, 29384, 29377,
   29370, 29363, 29356, 29349, 29342, 29335, 29328, 29321, 29314, 29307, 29300, 29293, 29286, 29279, 29272,
   29265, 29258, 29251, 29244, 29237, 29230, 29223, 29216, 29209, 29202, 29195, 29188, 29181, 29174, 29167,
   29160, 29153, 29146, 29139, 29132, 29125, 29110, 29095, 29081, 29066, 29051, 29036, 29021, 29007, 28992,
   28977, 28962, 28947, 28933, 28918, 28903, 28888, 28873, 28859, 28844, 28829, 28814, 28799, 28785, 28770,
   28755, 28740, 28725, 28711, 28696, 28681, 28666, 28651, 28637, 28622, 28607, 28592, 28577, 28563, 28548,
   28533, 28518, 28503, 28489, 28474, 28459, 28444, 28429, 28415, 28400, 28385, 28370, 28355, 28341, 28326,
   28311, 28296, 28281, 28267, 28252, 28237, 28222, 28207, 28193, 28178, 28163, 28148, 28133, 28119, 28104,
   28089, 28074, 28059, 28045, 28030, 28015, 28000, 27985, 27971, 27956, 27941, 27926, 27911, 27897, 27882,
   27867, 27852, 27837, 27823, 27808, 27793, 27778, 27763, 27749, 27734, 27719, 27704, 27689, 27675, 27660,
   27645, 27630, 27615, 27601, 27586, 27571, 27556, 27541, 27527, 27512, 27497, 27482, 27467, 27453, 27438,
   27423, 27408, 27393, 27379, 27364, 27349, 27334, 27319, 27305, 27290, 27275, 27260, 27245, 27231, 27216,
   27201, 27186, 27171, 27157, 27142, 27127, 27112, 27097, 27083, 27068, 27053, 27038, 27023, 27009, 26990,
};

/* is96k: the 96 kHz mode (eBands_96k). hq: the encoder's hq flag (E-family bitrates, 256k, 328k). */
float ssc_headroom_gain(int is96k, int bitrate, int hq, int j)
{
   const short *eb = is96k ? ssc_eBands_96k : ssc_eBands_44k;
   int q = 32768;
   if (!hq) {                                    /* normal_headroom */
      if (bitrate >= 96000) {
         int b0 = eb[20] * 8, lo = b0 + 68, hi = eb[22] * 8;
         if (j >= lo && j < hi && j - b0 < 240) q = ssc_headroom_gain_tab[j - b0];
      } else {
         int i0 = eb[17] * 8, i1 = eb[20] * 8 - 15, i2 = eb[22] * 8;
         if (j >= i0 && j < i1) q = 32767 - 3 * (j - i0);
         else if (j >= i1 && j < i2) q = 0x7767 - 3 * (i1 - i0) - 5 * (j - i1);
      }
   } else {                                      /* uhq_headroom (piecewise constant, measured) */
      int hiRate = bitrate >= 270000;
      if (!is96k) {
         if (!hiRate) { if (j >= 350 && j < 384) q = 30620; else if (j >= 384 && j < 409) q = 28893; else if (j >= 409) q = 30620; }
         else { if (j >= 354 && j < 384) q = 30720; else if (j >= 384 && j < 400) q = 28988;
                else if (j >= 400 && j < 409) q = 28893; else if (j >= 409) q = 30620; }
      } else {
         if (!hiRate) { if (j >= 288 && j < 313) q = 30920; else if (j >= 313 && j < 350) q = 30750; else if (j >= 350) q = 30620; }
         else { if (j >= 288 && j < 313) q = 30920; else if (j >= 313 && j < 354) q = 30820;
                else if (j >= 354 && j < 400) q = 30720; else if (j >= 400) q = 30620; }
      }
   }
   return (float)q * (1.f / 32768.f);
}

#endif
