/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* Part of the SSC decoder. Derived from libopus 1.1 (BSD license, see COPYING.opus) and from
 * analysis of Samsung's SSC encoder; see docs/notes for the format notes. */
#include "ssc_ec.h"
#include "ssc_internal.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------- */
/* Constants                                                               */
/* ---------------------------------------------------------------------- */
#define SSC_NBANDS        22
#define SSC_LM            3
#define SSC_M             8          /* 1<<LM */
#define SSC_NFREQ         864        /* coefficients per channel */
#define SSC_BITRES        3
#define SSC_QTHETA_OFFSET 4
#define SSC_QTHETA_OFFSET_TWOPHASE 16 /* unreachable (N>=8) */
#define SSC_LOG_MAX_PSEUDO 6
#define SSC_MAX_BAND_N    144        /* 18*8 */
#define SSC_MAX_K         128        /* get_pulses(40) */
#define SSC_NORM_MAX      720        /* M*eBands[nbEBands-1]: 672 (44.1k), 720 (96k) */

#define SSC_SPREAD_NONE       0
#define SSC_SPREAD_LIGHT      1
#define SSC_SPREAD_NORMAL     2
#define SSC_SPREAD_AGGRESSIVE 3

#define SSC_EPSILON 1e-15f
#define SSC_PI      3.14159265358979323846f

/* spread_icdf @0x3d14 (decode: spread = (tell+4 <= total_bits) ? ec_dec_icdf(ec, ssc_spread_icdf, 5) : SPREAD_NORMAL) */


/* Mode view needed here. Fill it from the tables in B_decoder_snippets.c
 * (ssc_eBands_44k / ssc_logN_44k / ssc_cache_index_44k / ssc_cache_bits_44k, and the 96k ones).
 * The tables must be the dumped ones, NOT recomputed with compute_pulse_cache. */
#if 0
typedef struct {
   const short         *eBands;       /* 23 */
   const short         *logN;         /* 22 */
   const short         *cache_index;  /* 5*22, row = LM+1 (LM = -1..3) */
   const unsigned char *cache_bits;
} ssc_band_mode_unused;
#endif

static int ssc_imin(int a, int b) { return a < b ? a : b; }
static int ssc_imax(int a, int b) { return a > b ? a : b; }
#define ssc_ilog(x) ssc_ilog32(x) /* EC_ILOG */

/* ---------------------------------------------------------------------- */
/* Integer helpers (bit-exact, they drive the bit allocation)              */
/* ---------------------------------------------------------------------- */
static uint32_t ssc_lcg_rand(uint32_t seed)
{
   return 1664525u * seed + 1013904223u;
}

/* FRAC_MUL16 with opus_int16 casts */
static int ssc_frac_mul16(int a, int b)
{
   return (16384 + (int32_t)(int16_t)a * (int16_t)b) >> 15;
}

/* = ssc_bitexact_cos_asm (one lane) */
static int ssc_bitexact_cos(int x)
{
   int32_t tmp;
   int x2;
   tmp = (4096 + (int32_t)x * x) >> 13;
   x2 = (int)tmp;
   x2 = (32767 - x2) + ssc_frac_mul16(x2, (-7651 + ssc_frac_mul16(x2, (8277 + ssc_frac_mul16(-626, x2)))));
   return 1 + x2;
}

static int ssc_bitexact_log2tan(int isin, int icos)
{
   int lc, ls;
   lc = ssc_ilog((uint32_t)icos);
   ls = ssc_ilog((uint32_t)isin);
   icos <<= 15 - lc;
   isin <<= 15 - ls;
   return (ls - lc) * (1 << 11)
        + ssc_frac_mul16(isin, ssc_frac_mul16(isin, -2597) + 7932)
        - ssc_frac_mul16(icos, ssc_frac_mul16(icos, -2597) + 7932);
}

/* opus mathops.c isqrt32 */
static unsigned ssc_isqrt32(uint32_t val)
{
   unsigned b, g;
   int bshift;
   g = 0;
   bshift = (ssc_ilog(val) - 1) >> 1;
   b = 1U << bshift;
   do {
      uint32_t t = (((uint32_t)g << 1) + b) << bshift;
      if (t <= val) { g += b; val -= t; }
      b >>= 1;
      bshift--;
   } while (bshift >= 0);
   return g;
}

static int ssc_compute_qn(int N, int b, int offset, int pulse_cap, int stereo)
{
   static const int16_t exp2_table8[8] = /* @0x2760 */
      {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
   int qn, qb;
   int N2 = 2 * N - 1;
   if (stereo && N == 2)
      N2--;
   qb = ssc_imin(b - pulse_cap - (4 << SSC_BITRES), (b + N2 * offset) / N2);
   qb = ssc_imin(8 << SSC_BITRES, qb);
   if (qb < (1 << SSC_BITRES >> 1)) {
      qn = 1;
   } else {
      qn = exp2_table8[qb & 0x7] >> (14 - (qb >> SSC_BITRES));
      qn = (qn + 1) >> 1 << 1;
   }
   return qn;
}

/* rate.h helpers on Samsung's cache (identical code to opus; tables differ, see header) */
static int ssc_get_pulses(int i)
{
   return i < 8 ? i : (8 + (i & 7)) << ((i >> 3) - 1);
}

static int ssc_qb_bits2pulses(const ssc_band_mode *m, int band, int LM, int bits)
{
   int i, lo, hi;
   const unsigned char *cache = m->cache_bits + m->cache_index[(LM + 1) * SSC_NBANDS + band];
   lo = 0;
   hi = cache[0];
   bits--;
   for (i = 0; i < SSC_LOG_MAX_PSEUDO; i++) {
      int mid = (lo + hi + 1) >> 1;
      if ((int)cache[mid] >= bits) hi = mid; else lo = mid;
   }
   if (bits - (lo == 0 ? -1 : (int)cache[lo]) <= (int)cache[hi] - bits)
      return lo;
   return hi;
}

static int ssc_qb_pulses2bits(const ssc_band_mode *m, int band, int LM, int pulses)
{
   const unsigned char *cache = m->cache_bits + m->cache_index[(LM + 1) * SSC_NBANDS + band];
   return pulses == 0 ? 0 : cache[pulses] + 1;
}

/* ---------------------------------------------------------------------- */
/* CWRS decode (opus SMALL_FOOTPRINT form: no 5 KB U table needed).        */
/* Index mapping is identical to Samsung's table-based icwrs (verified on   */
/* all reachable (N,K): 2340 random vectors, 0 mismatches).                 */
/* ---------------------------------------------------------------------- */


/* returns V(n,k); u[0..k+1] = U(n,0..k+1). n>=2, k>=1 */


/* = decode side of ssc_encode_pulses: ec_dec_uint(V(N,K)) then CWRS */
void ssc_cwrs_decode_pulses(int *y, int n, int k, ec_dec *ec); /* ssc_cwrs.c (table-based) */
static void ssc_decode_pulses(int *y, int n, int k, ec_dec *ec)
{
   ssc_cwrs_decode_pulses(y, n, k, ec);
}

/* ---------------------------------------------------------------------- */
/* vq.c (float)                                                            */
/* ---------------------------------------------------------------------- */
static void ssc_exp_rotation1(float *X, int len, int stride, float c, float s)
{
   int i;
   for (i = 0; i < len - stride; i++) {
      float x1 = X[i], x2 = X[i + stride];
      X[i + stride] = c * x2 + s * x1;
      X[i]          = c * x1 - s * x2;
   }
   for (i = len - 2 * stride - 1; i >= 0; i--) {
      float x1 = X[i], x2 = X[i + stride];
      X[i + stride] = c * x2 + s * x1;
      X[i]          = c * x1 - s * x2;
   }
}

/* SPREAD_FACTOR @0x5ebc = {15,10,5} */
static void ssc_exp_rotation(float *X, int len, int dir, int stride, int K, int spread)
{
   static const int SPREAD_FACTOR[3] = {15, 10, 5};
   int i, stride2 = 0, factor;
   float gain, theta, c, s;

   if (2 * K >= len || spread == SSC_SPREAD_NONE)
      return;
   factor = SPREAD_FACTOR[spread - 1];
   gain = (float)len / (float)(len + factor * K);
   theta = 0.5f * gain * gain;
   c = ssc_cos_half_pi(theta);               /* celt_cos_norm(theta) */
   s = ssc_cos_half_pi(1.0f - theta);        /* celt_cos_norm(1-theta) */
   if (len >= 8 * stride) {
      stride2 = 1;
      while ((stride2 * stride2 + stride2) * stride + (stride >> 2) < len)
         stride2++;
   }
   len /= stride;
   for (i = 0; i < stride; i++) {
      if (dir < 0) {
         if (stride2)
            ssc_exp_rotation1(X + i * len, len, stride2, s, c);
         ssc_exp_rotation1(X + i * len, len, 1, c, s);
      } else {
         ssc_exp_rotation1(X + i * len, len, 1, c, -s);
         if (stride2)
            ssc_exp_rotation1(X + i * len, len, stride2, s, -c);
      }
   }
}

static unsigned ssc_extract_collapse_mask(const int *iy, int N, int B)
{
   unsigned mask = 0;
   int i, j, N0;
   if (B <= 1)
      return 1;
   N0 = N / B;
   for (i = 0; i < B; i++)
      for (j = 0; j < N0; j++)
         mask |= (unsigned)(iy[i * N0 + j] != 0) << i;
   return mask;
}

static unsigned ssc_alg_unquant(float *X, int N, int K, int spread, int B, ec_dec *ec, float gain)
{
   int iy[SSC_MAX_BAND_N];
   int i;
   float Ryy = 0, g;
   ssc_decode_pulses(iy, N, K, ec);
   for (i = 0; i < N; i++)
      Ryy += (float)iy[i] * (float)iy[i];
   g = gain / sqrtf(Ryy);                    /* normalise_residual */
   for (i = 0; i < N; i++)
      X[i] = g * (float)iy[i];
   ssc_exp_rotation(X, N, -1, B, K, spread);
   return ssc_extract_collapse_mask(iy, N, B);
}

static void ssc_renormalise_vector(float *X, int N, float gain)
{
   int i;
   float E = SSC_EPSILON, g;
   for (i = 0; i < N; i++)
      E += X[i] * X[i];
   g = gain / sqrtf(E);
   for (i = 0; i < N; i++)
      X[i] *= g;
}

/* ---------------------------------------------------------------------- */
/* bands.c helpers (float)                                                  */
/* ---------------------------------------------------------------------- */
static void ssc_haar1(float *X, int N0, int stride)
{
   int i, j;
   N0 >>= 1;
   for (i = 0; i < stride; i++)
      for (j = 0; j < N0; j++) {
         float tmp1 = 0.70710678f * X[stride * 2 * j + i];
         float tmp2 = 0.70710678f * X[stride * (2 * j + 1) + i];
         X[stride * 2 * j + i] = tmp1 + tmp2;
         X[stride * (2 * j + 1) + i] = tmp1 - tmp2;
      }
}

/* ordery_table @0x2880 */
static const int ssc_ordery_table[] = {
    1,  0,
    3,  0,  2,  1,
    7,  0,  4,  3,  6,  1,  5,  2,
   15,  0,  8,  7, 12,  3, 11,  4, 14,  1,  9,  6, 13,  2, 10,  5,
};

static void ssc_deinterleave_hadamard(float *X, int N0, int stride, int hadamard)
{
   float tmp[SSC_MAX_BAND_N];
   int i, j, N = N0 * stride;
   if (hadamard) {
      const int *ordery = ssc_ordery_table + stride - 2;
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++)
            tmp[ordery[i] * N0 + j] = X[j * stride + i];
   } else {
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++)
            tmp[i * N0 + j] = X[j * stride + i];
   }
   memcpy(X, tmp, (size_t)N * sizeof(float));
}

static void ssc_interleave_hadamard(float *X, int N0, int stride, int hadamard)
{
   float tmp[SSC_MAX_BAND_N];
   int i, j, N = N0 * stride;
   if (hadamard) {
      const int *ordery = ssc_ordery_table + stride - 2;
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++)
            tmp[j * stride + i] = X[ordery[i] * N0 + j];
   } else {
      for (i = 0; i < stride; i++)
         for (j = 0; j < N0; j++)
            tmp[j * stride + i] = X[i * N0 + j];
   }
   memcpy(X, tmp, (size_t)N * sizeof(float));
}

static void ssc_stereo_merge(float *X, float *Y, float mid, int N)
{
   int j;
   float xp = 0, side = 0, El, Er, lgain, rgain;
   for (j = 0; j < N; j++) {
      xp += Y[j] * X[j];
      side += Y[j] * Y[j];
   }
   xp *= mid;
   El = mid * mid + side - 2 * xp;
   Er = mid * mid + side + 2 * xp;
   if (Er < 6e-4f || El < 6e-4f) {
      for (j = 0; j < N; j++)
         Y[j] = X[j];
      return;
   }
   lgain = 1.0f / sqrtf(El);
   rgain = 1.0f / sqrtf(Er);
   for (j = 0; j < N; j++) {
      float l = mid * X[j], r = Y[j];
      X[j] = lgain * (l - r);
      Y[j] = rgain * (l + r);
   }
}

/* ---------------------------------------------------------------------- */
/* band context                                                             */
/* ---------------------------------------------------------------------- */
typedef struct {
   const ssc_band_mode *m;
   int      i;
   int      intensity;
   int      spread;
   int      tf_change;
   ec_dec  *ec;
   int32_t  remaining_bits;
   uint32_t seed;
} ssc_band_ctx;

typedef struct {
   int inv, imid, iside, delta, itheta, qalloc;
} ssc_split_ctx;

/* compute_theta, decode side. Identical to the encoder's inlined versions
 * (stereo: ssc_quant_all_bands; mono: FUN_00109460). */
static void ssc_compute_theta(ssc_band_ctx *ctx, ssc_split_ctx *sctx, int N, int *b,
      int B, int B0, int LM, int stereo, int *fill)
{
   int qn, itheta = 0, delta, imid, iside, qalloc, pulse_cap, offset, inv = 0;
   int32_t tell;
   const ssc_band_mode *m = ctx->m;
   int i = ctx->i;
   ec_dec *ec = ctx->ec;

   pulse_cap = m->logN[i] + LM * (1 << SSC_BITRES);
   offset = (pulse_cap >> 1) - (stereo && N == 2 ? SSC_QTHETA_OFFSET_TWOPHASE : SSC_QTHETA_OFFSET);
   qn = ssc_compute_qn(N, *b, offset, pulse_cap, stereo);
   if (stereo && i >= ctx->intensity)
      qn = 1;
   tell = (int32_t)ec_tell_frac(ec);
   if (qn != 1) {
      if (stereo && N > 2) {
         /* step pdf: p0=3 up to qn/2 */
         int p0 = 3, x, x0 = qn / 2, ft = p0 * (x0 + 1) + x0, fs;
         fs = (int)ec_decode(ec, (unsigned)ft);
         if (fs < (x0 + 1) * p0)
            x = fs / p0;
         else
            x = x0 + 1 + (fs - (x0 + 1) * p0);
         ec_dec_update(ec, x <= x0 ? p0 * x : (x - 1 - x0) + (x0 + 1) * p0,
                           x <= x0 ? p0 * (x + 1) : (x - x0) + (x0 + 1) * p0, (unsigned)ft);
         itheta = x;
      } else if (B0 > 1 || stereo) {
         itheta = (int)ec_dec_uint(ec, (uint32_t)qn + 1);   /* uniform */
      } else {
         /* triangular */
         int fs, fl, fm, ft;
         ft = ((qn >> 1) + 1) * ((qn >> 1) + 1);
         fm = (int)ec_decode(ec, (unsigned)ft);
         if (fm < ((qn >> 1) * ((qn >> 1) + 1) >> 1)) {
            itheta = ((int)ssc_isqrt32(8 * (uint32_t)fm + 1) - 1) >> 1;
            fs = itheta + 1;
            fl = itheta * (itheta + 1) >> 1;
         } else {
            itheta = (2 * (qn + 1) - (int)ssc_isqrt32(8 * (uint32_t)(ft - fm - 1) + 1)) >> 1;
            fs = qn + 1 - itheta;
            fl = ft - ((qn + 1 - itheta) * (qn + 2 - itheta) >> 1);
         }
         ec_dec_update(ec, (unsigned)fl, (unsigned)(fl + fs), (unsigned)ft);
      }
      itheta = (int32_t)itheta * 16384 / qn;
   } else if (stereo) {
      if (*b > 2 << SSC_BITRES && ctx->remaining_bits > 2 << SSC_BITRES)
         inv = ec_dec_bit_logp(ec, 2);
      else
         inv = 0;
      itheta = 0;
   }
   qalloc = (int)((int32_t)ec_tell_frac(ec) - tell);
   *b -= qalloc;

   if (itheta == 0) {
      imid = 32767;
      iside = 0;
      *fill &= (1 << B) - 1;
      delta = -16384;
   } else if (itheta == 16384) {
      imid = 0;
      iside = 32767;
      *fill &= ((1 << B) - 1) << B;
      delta = 16384;
   } else {
      imid = ssc_bitexact_cos(itheta);
      iside = ssc_bitexact_cos(16384 - itheta);
      delta = ssc_frac_mul16((N - 1) << 7, ssc_bitexact_log2tan(iside, imid));
   }
   sctx->inv = inv;
   sctx->imid = imid;
   sctx->iside = iside;
   sctx->delta = delta;
   sctx->itheta = itheta;
   sctx->qalloc = qalloc;
}

/* quant_partition, decode side (FUN_00109460) */
static unsigned ssc_quant_partition(ssc_band_ctx *ctx, float *X, int N, int b, int B,
      float *lowband, int LM, float gain, int fill)
{
   const ssc_band_mode *m = ctx->m;
   int i = ctx->i;
   const unsigned char *cache;
   int B0 = B;
   unsigned cm = 0;

   cache = m->cache_bits + m->cache_index[(LM + 1) * SSC_NBANDS + i];
   if (LM != -1 && b > cache[cache[0]] + 12 && N > 2) {
      int mbits, sbits, delta, itheta, qalloc;
      ssc_split_ctx sctx;
      float *Y, *next_lowband2 = NULL;
      float mid, side;
      int32_t rebalance;

      N >>= 1;
      Y = X + N;
      LM -= 1;
      if (B == 1)
         fill = (fill & 1) | (fill << 1);
      B = (B + 1) >> 1;

      ssc_compute_theta(ctx, &sctx, N, &b, B, B0, LM, 0, &fill);
      delta = sctx.delta;
      itheta = sctx.itheta;
      qalloc = sctx.qalloc;
      mid = (1.f / 32768) * sctx.imid;
      side = (1.f / 32768) * sctx.iside;

      if (B0 > 1 && (itheta & 0x3fff)) {
         if (itheta > 8192)
            delta -= delta >> (4 - LM);
         else
            delta = ssc_imin(0, delta + (N << SSC_BITRES >> (5 - LM)));
      }
      mbits = ssc_imax(0, ssc_imin(b, (b - delta) / 2));
      sbits = b - mbits;
      ctx->remaining_bits -= qalloc;

      if (lowband)
         next_lowband2 = lowband + N;

      rebalance = ctx->remaining_bits;
      if (mbits >= sbits) {
         cm = ssc_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain * mid, fill);
         rebalance = mbits - (rebalance - ctx->remaining_bits);
         if (rebalance > 3 << SSC_BITRES && itheta != 0)
            sbits += rebalance - (3 << SSC_BITRES);
         cm |= ssc_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain * side, fill >> B) << (B0 >> 1);
      } else {
         cm = ssc_quant_partition(ctx, Y, N, sbits, B, next_lowband2, LM, gain * side, fill >> B) << (B0 >> 1);
         rebalance = sbits - (rebalance - ctx->remaining_bits);
         if (rebalance > 3 << SSC_BITRES && itheta != 16384)
            mbits += rebalance - (3 << SSC_BITRES);
         cm |= ssc_quant_partition(ctx, X, N, mbits, B, lowband, LM, gain * mid, fill);
      }
   } else {
      int q, curr_bits;
      q = ssc_qb_bits2pulses(m, i, LM, b);
      curr_bits = ssc_qb_pulses2bits(m, i, LM, q);
      ctx->remaining_bits -= curr_bits;
      while (ctx->remaining_bits < 0 && q > 0) {
         ctx->remaining_bits += curr_bits;
         q--;
         curr_bits = ssc_qb_pulses2bits(m, i, LM, q);
         ctx->remaining_bits -= curr_bits;
      }
      if (q != 0) {
         int K = ssc_get_pulses(q);
         cm = ssc_alg_unquant(X, N, K, ctx->spread, B, ctx->ec, gain);
      } else {
         /* no pulse: fold or noise (resynth-only; opus 1.1 behaviour) */
         int j;
         unsigned cm_mask = (unsigned)(1UL << B) - 1;
         fill &= (int)cm_mask;
         if (!fill) {
            for (j = 0; j < N; j++)
               X[j] = 0;
         } else {
            if (lowband == NULL) {
               for (j = 0; j < N; j++) {
                  ctx->seed = ssc_lcg_rand(ctx->seed);
                  X[j] = (float)((int32_t)ctx->seed >> 20);
               }
               cm = cm_mask;
            } else {
               for (j = 0; j < N; j++) {
                  float tmp;
                  ctx->seed = ssc_lcg_rand(ctx->seed);
                  tmp = (ctx->seed & 0x8000) ? (1.0f / 256) : -(1.0f / 256);
                  X[j] = lowband[j] + tmp;
               }
               cm = (unsigned)fill;
            }
            ssc_renormalise_vector(X, N, gain);
         }
      }
   }
   return cm;
}

/* quant_band, decode side (FUN_00109038). N >= 8 always, so there is no N==1 case. */
static unsigned ssc_quant_band(ssc_band_ctx *ctx, float *X, int N, int b, int B,
      float *lowband, int LM, float *lowband_out, float gain, float *lowband_scratch, int fill)
{
   static const unsigned char bit_interleave_table[16] = /* @0x27c0 */
      {0, 1, 1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 2, 3, 3, 3};
   static const unsigned char bit_deinterleave_table[16] = {
      0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
      0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF };
   int N0 = N, N_B = N, N_B0, B0 = B, time_divide = 0, recombine = 0, longBlocks, k;
   int tf_change = ctx->tf_change;
   unsigned cm;

   longBlocks = B0 == 1;
   N_B /= B;
   if (tf_change > 0)
      recombine = tf_change;

   if (lowband_scratch && lowband && (recombine || ((N_B & 1) == 0 && tf_change < 0) || B0 > 1)) {
      memcpy(lowband_scratch, lowband, (size_t)N * sizeof(float));
      lowband = lowband_scratch;
   }
   for (k = 0; k < recombine; k++) {
      if (lowband)
         ssc_haar1(lowband, N >> k, 1 << k);
      fill = bit_interleave_table[fill & 0xF] | bit_interleave_table[fill >> 4] << 2;
   }
   B >>= recombine;
   N_B <<= recombine;

   while ((N_B & 1) == 0 && tf_change < 0) {
      if (lowband)
         ssc_haar1(lowband, N_B, B);
      fill |= fill << B;
      B <<= 1;
      N_B >>= 1;
      time_divide++;
      tf_change++;
   }
   B0 = B;
   N_B0 = N_B;

   if (B0 > 1 && lowband)
      ssc_deinterleave_hadamard(lowband, N_B >> recombine, B0 << recombine, longBlocks);

   cm = ssc_quant_partition(ctx, X, N, b, B, lowband, LM, gain, fill);

   if (B0 > 1)
      ssc_interleave_hadamard(X, N_B >> recombine, B0 << recombine, longBlocks);
   N_B = N_B0;
   B = B0;
   for (k = 0; k < time_divide; k++) {
      B >>= 1;
      N_B <<= 1;
      cm |= cm >> B;
      ssc_haar1(X, N_B, B);
   }
   for (k = 0; k < recombine; k++) {
      cm = bit_deinterleave_table[cm];
      ssc_haar1(X, N0 >> k, 1 << k);
   }
   B <<= recombine;

   if (lowband_out) {
      int j;
      float n = sqrtf((float)N0);
      for (j = 0; j < N0; j++)
         lowband_out[j] = n * X[j];
   }
   cm &= (1u << B) - 1;
   return cm;
}

/* quant_band_stereo, decode side (inlined in ssc_quant_all_bands). N >= 8, so N==1/N==2 cases are absent. */
static unsigned ssc_quant_band_stereo(ssc_band_ctx *ctx, float *X, float *Y, int N, int b, int B,
      float *lowband, int LM, float *lowband_out, float *lowband_scratch, int fill)
{
   ssc_split_ctx sctx;
   int mbits, sbits, delta, itheta, qalloc, j;
   float mid, side;
   unsigned cm;
   int32_t rebalance;

   ssc_compute_theta(ctx, &sctx, N, &b, B, B, LM, 1, &fill);
   delta = sctx.delta;
   itheta = sctx.itheta;
   qalloc = sctx.qalloc;
   mid = (1.f / 32768) * sctx.imid;
   side = (1.f / 32768) * sctx.iside;

   mbits = ssc_imax(0, ssc_imin(b, (b - delta) / 2));
   sbits = b - mbits;
   ctx->remaining_bits -= qalloc;

   rebalance = ctx->remaining_bits;
   if (mbits >= sbits) {
      cm = ssc_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.0f, lowband_scratch, fill);
      rebalance = mbits - (rebalance - ctx->remaining_bits);
      if (rebalance > 3 << SSC_BITRES && itheta != 0)
         sbits += rebalance - (3 << SSC_BITRES);
      cm |= ssc_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill >> B);
   } else {
      cm = ssc_quant_band(ctx, Y, N, sbits, B, NULL, LM, NULL, side, NULL, fill >> B);
      rebalance = sbits - (rebalance - ctx->remaining_bits);
      if (rebalance > 3 << SSC_BITRES && itheta != 16384)
         mbits += rebalance - (3 << SSC_BITRES);
      cm |= ssc_quant_band(ctx, X, N, mbits, B, lowband, LM, lowband_out, 1.0f, lowband_scratch, fill);
   }

   ssc_stereo_merge(X, Y, mid, N);
   if (sctx.inv)
      for (j = 0; j < N; j++)
         Y[j] = -Y[j];
   return cm;
}

/* ---------------------------------------------------------------------- */
/* quant_all_bands, decode side                                             */
/* ---------------------------------------------------------------------- */
#if 0
typedef struct {
   float norm[2 * SSC_NORM_MAX];          /* folding memory, both channels */
   float lowband_scratch[SSC_MAX_BAND_N];
} ssc_qab_work_unused;
#endif

/* X_, Y_: 864 floats per channel, zeroed by the caller beforehand.
 * collapse_masks: 2*22 bytes.
 * total_bits = (nbBytes*8 << BITRES) - anti_collapse_rsv (nbBytes = range-coded payload bytes).
 * end = 22 - k (see C_bands_pvq.md / header analysis). seed: decoder st->rng (in/out). */
void ssc_quant_all_bands_dec(const ssc_band_mode *m, int end, float *X_, float *Y_,
      unsigned char *collapse_masks, const int *pulses, int shortBlocks, int spread,
      int dual_stereo, int intensity, const int *tf_res, int32_t total_bits, int32_t balance,
      ec_dec *ec, int codedBands, uint32_t *seed, ssc_qab_work *ws)
{
   const short *eBands = m->eBands;
   const int M = SSC_M, LM = SSC_LM, C = 2;
   float *norm = ws->norm;
   float *norm2 = ws->norm + M * eBands[SSC_NBANDS - 1];
   int B = shortBlocks ? M : 1;
   int lowband_offset = 0, update_lowband = 1;
   const int norm_offset = 0;    /* start = 0 */
   int i;
   ssc_band_ctx ctx;

   ctx.m = m;
   ctx.ec = ec;
   ctx.intensity = intensity;
   ctx.spread = spread;
   ctx.seed = *seed;

   for (i = 0; i < end; i++) {
      int32_t tell, remaining_bits, curr_balance;
      int b, N, tf_change, last, effective_lowband = -1;
      unsigned x_cm, y_cm;
      float *X, *Y, *lowband_scratch;

      ctx.i = i;
      last = (i == end - 1);
      X = X_ + M * eBands[i];
      Y = Y_ + M * eBands[i];
      N = M * eBands[i + 1] - M * eBands[i];
      tell = (int32_t)ec_tell_frac(ec);

      if (i != 0)
         balance -= tell;
      remaining_bits = total_bits - tell - 1;
      ctx.remaining_bits = remaining_bits;
      if (i <= codedBands - 1) {
         curr_balance = balance / ssc_imin(3, codedBands - i);
         b = ssc_imax(0, ssc_imin(16383, ssc_imin(remaining_bits + 1, pulses[i] + curr_balance)));
      } else {
         b = 0;
      }

      if (M * eBands[i] - N >= M * eBands[0] && (update_lowband || lowband_offset == 0))
         lowband_offset = i;

      tf_change = tf_res[i];
      ctx.tf_change = tf_change;

      /* SSC: no "if (i >= m->effEBands) { X=Y=norm; lowband_scratch=NULL; }" -- band 21 is coded into X. */
      lowband_scratch = last ? NULL : ws->lowband_scratch;

      if (lowband_offset != 0 && (spread != SSC_SPREAD_AGGRESSIVE || B > 1 || tf_change < 0)) {
         int fold_start, fold_end, fold_i;
         effective_lowband = ssc_imax(0, M * eBands[lowband_offset] - norm_offset - N);
         fold_start = lowband_offset;
         while (M * eBands[--fold_start] > effective_lowband + norm_offset);
         fold_end = lowband_offset - 1;
         while (M * eBands[++fold_end] < effective_lowband + norm_offset + N);
         x_cm = y_cm = 0;
         fold_i = fold_start;
         do {
            x_cm |= collapse_masks[fold_i * C + 0];
            y_cm |= collapse_masks[fold_i * C + C - 1];
         } while (++fold_i < fold_end);
      } else {
         x_cm = y_cm = (1u << B) - 1;
      }

      if (dual_stereo && i == intensity) {
         int j;
         dual_stereo = 0;
         for (j = 0; j < M * eBands[i] - norm_offset; j++)
            norm[j] = 0.5f * (norm[j] + norm2[j]);
      }
      if (dual_stereo) {
         x_cm = ssc_quant_band(&ctx, X, N, b / 2, B,
               effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
               last ? NULL : norm + M * eBands[i] - norm_offset, 1.0f, lowband_scratch, (int)x_cm);
         y_cm = ssc_quant_band(&ctx, Y, N, b / 2, B,
               effective_lowband != -1 ? norm2 + effective_lowband : NULL, LM,
               last ? NULL : norm2 + M * eBands[i] - norm_offset, 1.0f, lowband_scratch, (int)y_cm);
      } else {
         x_cm = ssc_quant_band_stereo(&ctx, X, Y, N, b, B,
               effective_lowband != -1 ? norm + effective_lowband : NULL, LM,
               last ? NULL : norm + M * eBands[i] - norm_offset, lowband_scratch, (int)(x_cm | y_cm));
         y_cm = x_cm;
      }
      collapse_masks[i * C + 0] = (unsigned char)x_cm;
      collapse_masks[i * C + C - 1] = (unsigned char)y_cm;
      balance += pulses[i] + tell;

      update_lowband = b > (N << SSC_BITRES);
   }
   *seed = ctx.seed;
}

/* ---------------------------------------------------------------------- */
/* anti_collapse (opus 1.1 float), C=2, LM=3, size=864.                     */
/* Call after the anti-collapse bit (raw ec_dec_bits(ec,1), present only if */
/* anti_collapse_rsv) and energy finalise:                                  */
/*   if (anti_collapse_on) ssc_anti_collapse(m, X, collapse_masks, 0, end,  */
/*        oldBandE, oldLogE, oldLogE2, pulses, seed_after_quant_all_bands); */
/* logE arrays: [2*22], log2 units (opus float convention: 1.0 = 6.02 dB).  */
/* ---------------------------------------------------------------------- */
void ssc_anti_collapse(const ssc_band_mode *m, float *X_, float *Y_, const unsigned char *collapse_masks,
      int start, int end, const float *logE, const float *prev1logE, const float *prev2logE,
      const int *pulses, uint32_t seed)
{
   const int LM = SSC_LM, C = 2;
   int c, i, j, k;
   for (i = start; i < end; i++) {
      int N0 = m->eBands[i + 1] - m->eBands[i];
      int depth = (1 + pulses[i]) / (N0 << LM);   /* depth in 1/8 bits */
      float thresh = 0.5f * ssc_exp2(-0.125f * (float)depth);
      float sqrt_1 = 1.0f / sqrtf((float)(N0 << LM));
      for (c = 0; c < C; c++) {
         float prev1 = prev1logE[c * SSC_NBANDS + i];
         float prev2 = prev2logE[c * SSC_NBANDS + i];
         float Ediff, r;
         float *X;
         int renormalize = 0;
         Ediff = logE[c * SSC_NBANDS + i] - (prev1 < prev2 ? prev1 : prev2);
         if (Ediff < 0) Ediff = 0;
         r = 2.f * ssc_exp2(-Ediff);
         if (LM == 3)
            r *= 1.41421356f;
         if (r > thresh) r = thresh;
         r = r * sqrt_1;
         X = (c ? Y_ : X_) + (m->eBands[i] << LM);
         for (k = 0; k < 1 << LM; k++) {
            if (!(collapse_masks[i * C + c] & 1 << k)) {
               for (j = 0; j < N0; j++) {
                  seed = ssc_lcg_rand(seed);
                  X[(j << LM) + k] = (seed & 0x8000) ? r : -r;
               }
               renormalize = 1;
            }
         }
         if (renormalize)
            ssc_renormalise_vector(X, N0 << LM, 1.0f);
      }
   }
}

/* Keep the static table referenced so -Wunused does not complain when the
 * spread decode lives elsewhere. */

#endif
