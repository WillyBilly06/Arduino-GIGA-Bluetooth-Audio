/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* Part of the SSC decoder. Derived from libopus 1.1 (BSD license, see COPYING.opus) and from
 * analysis of Samsung's SSC encoder; see docs/notes for the format notes. */
#include "ssc_ec.h"
#include "ssc_tables.h"
#include "ssc_internal.h"


/* ---------------------------------------------------------------------- */
/* Constants (all verified in the disassembly)                             */
/* ---------------------------------------------------------------------- */
#define SSC_NB_EBANDS        22
#define SSC_NB_ALLOC_VECTORS 11
#define SSC_LM               3   /* always: 864-sample frame, 8 x 108 short MDCTs */
#define SSC_C                2   /* allocation is hard-wired for C=2 */
#define SSC_BITRES           3
#define SSC_ALLOC_STEPS      6
#define SSC_FINE_OFFSET      21
#define SSC_MAX_FINE_BITS    8
#define SSC_LOG_MAX_PSEUDO   6   /* bits2pulses bisection steps (quant_band) */
#define SSC_QTHETA_OFFSET    4   /* compute_qn: (pulse_cap>>1)-4, pulse_cap=logN+(LM<<3) */
#define SSC_MAX_PSEUDO       40

static int ssc_imin(int a, int b) { return a < b ? a : b; }
static int ssc_imax(int a, int b) { return a > b ? a : b; }

/* ---------------------------------------------------------------------- */

#if 0
typedef struct {
   const short         *eBands;            /* 23 entries */
   const unsigned char *allocVectors[2];   /* [0]: bitrate <= 192000, [1]: bitrate > 192000 */
   const short         *logN;              /* 22 */
   const short         *cache_index;       /* 5*22, row = LM+1 */
   const unsigned char *cache_bits;
   const int           *cap;               /* 22, final cap[] in 1/8 bits */
} ssc_alloc_mode_unused;
#endif

/* Fs 44100 and 48000 -> mode @0x1b9c0 */
const ssc_alloc_mode ssc_alloc_mode_44k = {
   ssc_eBands_44k, { ssc_allocVectorsA_44k, ssc_allocVectorsB_44k },
   ssc_logN_44k, ssc_cache_index_44k, ssc_cache_bits_44k, ssc_cap_44k
};
/* Fs 96000 -> mode @0x1ba40 */
const ssc_alloc_mode ssc_alloc_mode_96k = {
   ssc_eBands_96k, { ssc_allocVectorsA_96k, ssc_allocVectorsB_96k },
   ssc_logN_96k, ssc_cache_index_96k, ssc_cache_bits_96k, ssc_cap_96k
};

/* ---------------------------------------------------------------------- */
/* interp_bits2pulses, decoder side (start = 0, C = 2, LM = 3)             */
/* ---------------------------------------------------------------------- */
static int ssc_interp_bits2pulses(const ssc_alloc_mode *m, int end, int skip_start,
      const int *bits1, const int *bits2, const int *thresh, const int *cap,
      int32_t total, int32_t *_balance, int skip_rsv,
      int *intensity, int intensity_rsv, int *dual_stereo, int dual_stereo_rsv,
      int *bits, int *ebits, int *fine_priority, ec_dec *ec)
{
   const int C = SSC_C;
   const int stereo = 1;                         /* C > 1 */
   const int alloc_floor = C << SSC_BITRES;      /* 16 */
   const int logM = SSC_LM << SSC_BITRES;        /* 24 */
   const short *eBands = m->eBands;
   int32_t psum, left, percoeff, balance;
   int lo, hi, i, j, done, codedBands;

   lo = 0;
   hi = 1 << SSC_ALLOC_STEPS;
   for (i = 0; i < SSC_ALLOC_STEPS; i++)
   {
      int mid = (lo + hi) >> 1;
      psum = 0;
      done = 0;
      for (j = end; j-- > 0;)
      {
         int tmp = bits1[j] + (int)(((int32_t)mid * bits2[j]) >> SSC_ALLOC_STEPS);
         if (tmp >= thresh[j] || done)
         {
            done = 1;
            psum += ssc_imin(tmp, cap[j]);
         } else {
            if (tmp >= alloc_floor)
               psum += alloc_floor;
         }
      }
      if (psum > total)
         hi = mid;
      else
         lo = mid;
   }
   psum = 0;
   done = 0;
   for (j = end; j-- > 0;)
   {
      int tmp = bits1[j] + (int)(((int32_t)lo * bits2[j]) >> SSC_ALLOC_STEPS);
      if (tmp < thresh[j] && !done)
      {
         if (tmp >= alloc_floor)
            tmp = alloc_floor;
         else
            tmp = 0;
      } else
         done = 1;
      tmp = ssc_imin(tmp, cap[j]);
      bits[j] = tmp;
      psum += tmp;
   }

   /* Decide which bands to skip, working backwards from the end. */
   for (codedBands = end;; codedBands--)
   {
      int band_width, band_bits, rem;
      j = codedBands - 1;
      if (j <= skip_start)
      {
         total += skip_rsv;     /* give the reserved skip bit back */
         break;
      }
      left = total - psum;
      percoeff = left / (eBands[codedBands] - eBands[0]);
      left -= (eBands[codedBands] - eBands[0]) * percoeff;
      rem = ssc_imax(left - (eBands[j] - eBands[0]), 0);
      band_width = eBands[codedBands] - eBands[j];
      band_bits = (int)(bits[j] + percoeff * band_width + rem);
      /* threshold for coding the skip flag: max(thresh[j], 24) */
      if (band_bits >= ssc_imax(thresh[j], alloc_floor + (1 << SSC_BITRES)))
      {
         if (ec_dec_bit_logp(ec, 1))
            break;              /* "1" = stop skipping, keep codedBands */
         psum += 1 << SSC_BITRES;
         band_bits -= 1 << SSC_BITRES;
      }
      /* Reclaim the bits originally allocated to this band. */
      psum -= bits[j] + intensity_rsv;
      if (intensity_rsv > 0)
         intensity_rsv = SSC_LOG2_FRAC_TABLE[j];
      psum += intensity_rsv;
      if (band_bits >= alloc_floor)
      {
         psum += alloc_floor;
         bits[j] = alloc_floor;
      } else {
         bits[j] = 0;
      }
   }

   /* Intensity and dual stereo parameters. */
   if (intensity_rsv > 0)
      *intensity = (int)ec_dec_uint(ec, (uint32_t)(codedBands + 1));
   else
      *intensity = 0;
   if (*intensity <= 0)
   {
      total += dual_stereo_rsv;
      dual_stereo_rsv = 0;
   }
   if (dual_stereo_rsv > 0)
      *dual_stereo = ec_dec_bit_logp(ec, 1);
   else
      *dual_stereo = 0;

   /* Allocate the remaining bits */
   left = total - psum;
   percoeff = left / (eBands[codedBands] - eBands[0]);
   left -= (eBands[codedBands] - eBands[0]) * percoeff;
   for (j = 0; j < codedBands; j++)
      bits[j] += (int)(percoeff * (eBands[j + 1] - eBands[j]));
   for (j = 0; j < codedBands; j++)
   {
      int tmp = (int)ssc_imin(left, eBands[j + 1] - eBands[j]);
      bits[j] += tmp;
      left -= tmp;
   }

   balance = 0;
   for (j = 0; j < codedBands; j++)
   {
      int N0, den, offset, NClogN;
      int32_t excess, bit;

      N0 = eBands[j + 1] - eBands[j];
      /* N = N0<<3 >= 8 always, so CELT's N==1 and N==2 branches never occur. */
      bit = (int32_t)bits[j] + balance;
      excess = bit - cap[j];
      if (excess < 0) excess = 0;
      bits[j] = bit - excess;

      /* den = C*N + (C==2 && N>2 && !dual_stereo && j<intensity) */
      den = C * (N0 << SSC_LM) + ((!*dual_stereo && j < *intensity) ? 1 : 0);
      NClogN = den * (m->logN[j] + logM);
      offset = (NClogN >> 1) - den * SSC_FINE_OFFSET;
      if (bits[j] + offset < den * 2 << SSC_BITRES)
         offset += NClogN >> 2;
      else if (bits[j] + offset < den * 3 << SSC_BITRES)
         offset += NClogN >> 3;

      ebits[j] = ssc_imax(0, (bits[j] + offset + (den << (SSC_BITRES - 1))) / (den << SSC_BITRES));
      if (C * ebits[j] > (bits[j] >> SSC_BITRES))
         ebits[j] = bits[j] >> stereo >> SSC_BITRES;
      ebits[j] = ssc_imin(ebits[j], SSC_MAX_FINE_BITS);
      fine_priority[j] = ebits[j] * (den << SSC_BITRES) >= bits[j] + offset;
      bits[j] -= C * ebits[j] << SSC_BITRES;

      if (excess > 0)
      {
         int extra_fine, extra_bits;
         extra_fine = ssc_imin((int)(excess >> (stereo + SSC_BITRES)), SSC_MAX_FINE_BITS - ebits[j]);
         ebits[j] += extra_fine;
         extra_bits = extra_fine * C << SSC_BITRES;
         fine_priority[j] = extra_bits >= excess - balance;
         excess -= extra_bits;
      }
      balance = excess;
   }
   *_balance = balance;

   /* The skipped bands use all their bits for fine energy. */
   for (; j < end; j++)
   {
      ebits[j] = bits[j] >> stereo >> SSC_BITRES;
      bits[j] = 0;
      fine_priority[j] = ebits[j] < 1;
   }
   return codedBands;
}

/* ---------------------------------------------------------------------- */
/* compute_allocation, decoder side                                         */
/*                                                                          */
/*  m          : &ssc_alloc_mode_44k (Fs 44100/48000) or &ssc_alloc_mode_96k */
/*  end        : 22 - lastband_reduction (see B_allocation.md section 6)     */
/*  offsets    : dynalloc boosts per band (1/8 bit), as decoded             */
/*  alloc_trim : decoded trim (0..10)                                       */
/*  hi_rate    : 1 if the frame bitrate > 192000 (selects 2nd allocVectors) */
/*  total      : (nbBytes*8<<3) - ec_tell_frac(ec) - 1 - anti_collapse_rsv  */
/*  pulses/ebits/fine_priority : out, 22 entries                            */
/*  intensity/dual_stereo/balance : out                                     */
/*  returns codedBands                                                      */
/* ---------------------------------------------------------------------- */
int ssc_compute_allocation_dec(const ssc_alloc_mode *m, int end, const int *offsets,
      int alloc_trim, int hi_rate, int32_t total, int32_t *balance,
      int *pulses, int *ebits, int *fine_priority,
      int *intensity, int *dual_stereo, ec_dec *ec)
{
   const int C = SSC_C;
   const int LM = SSC_LM;
   const int len = SSC_NB_EBANDS;
   const unsigned char *allocVectors = m->allocVectors[hi_rate ? 1 : 0];
   const int *cap = m->cap;
   int lo, hi, j, skip_start, skip_rsv, intensity_rsv, dual_stereo_rsv;
   int bits1[SSC_NB_EBANDS], bits2[SSC_NB_EBANDS];
   int thresh[SSC_NB_EBANDS], trim_offset[SSC_NB_EBANDS];

   if (total < 0) total = 0;
   skip_start = 0;
   skip_rsv = total >= 1 << SSC_BITRES ? 1 << SSC_BITRES : 0;
   total -= skip_rsv;
   /* C==2 always: reserve intensity / dual-stereo bits */
   intensity_rsv = dual_stereo_rsv = 0;
   intensity_rsv = SSC_LOG2_FRAC_TABLE[end];
   if (intensity_rsv > total)
      intensity_rsv = 0;
   else
   {
      total -= intensity_rsv;
      dual_stereo_rsv = total >= 1 << SSC_BITRES ? 1 << SSC_BITRES : 0;
      total -= dual_stereo_rsv;
   }

   for (j = 0; j < end; j++)
   {
      int N0 = m->eBands[j + 1] - m->eBands[j];
      /* = max(16, 12*N0) */
      thresh[j] = ssc_imax(C << SSC_BITRES, (3 * N0 << LM << SSC_BITRES) >> 4);
      /* = 2*N0*(alloc_trim-8)*(end-j-1)  (the *64>>6 is exact) */
      /* CELT: C*N0*(alloc_trim-5-LM)*(end-j-1)*(1<<(LM+BITRES))>>6; with LM=3 the
         *64>>6 is exact, so write it without shifting a negative value: */
      trim_offset[j] = C * N0 * (alloc_trim - 5 - LM) * (end - j - 1);
      /* (N0<<LM)==1 never happens: no single-coefficient adjustment */
   }

   lo = 1;
   hi = SSC_NB_ALLOC_VECTORS - 1;
   do
   {
      int done = 0;
      int32_t psum = 0;
      int mid = (lo + hi) >> 1;
      for (j = end; j-- > 0;)
      {
         int bitsj;
         int N0 = m->eBands[j + 1] - m->eBands[j];
         bitsj = C * N0 * allocVectors[mid * len + j] << LM >> 2;   /* = 4*N0*a */
         if (bitsj > 0)
            bitsj = ssc_imax(0, bitsj + trim_offset[j]);
         bitsj += offsets[j];
         if (bitsj >= thresh[j] || done)
         {
            done = 1;
            psum += ssc_imin(bitsj, cap[j]);   /* (encoder saturates; cannot overflow here) */
         } else {
            if (bitsj >= C << SSC_BITRES)
               psum += C << SSC_BITRES;
         }
      }
      if (psum > total)
         hi = mid - 1;
      else
         lo = mid + 1;
   }
   while (lo <= hi);
   hi = lo--;

   for (j = 0; j < end; j++)
   {
      int bits1j, bits2j;
      int N0 = m->eBands[j + 1] - m->eBands[j];
      bits1j = C * N0 * allocVectors[lo * len + j] << LM >> 2;
      bits2j = hi >= SSC_NB_ALLOC_VECTORS ?
            cap[j] : C * N0 * allocVectors[hi * len + j] << LM >> 2;
      if (bits1j > 0)
         bits1j = ssc_imax(0, bits1j + trim_offset[j]);
      if (bits2j > 0)
         bits2j = ssc_imax(0, bits2j + trim_offset[j]);
      if (lo > 0)
         bits1j += offsets[j];
      bits2j += offsets[j];
      if (offsets[j] > 0)
         skip_start = j;
      bits2j = ssc_imax(0, bits2j - bits1j);
      bits1[j] = bits1j;
      bits2[j] = bits2j;
   }
   return ssc_interp_bits2pulses(m, end, skip_start, bits1, bits2, thresh, cap,
         total, balance, skip_rsv, intensity, intensity_rsv, dual_stereo, dual_stereo_rsv,
         pulses, ebits, fine_priority, ec);
}

/* Typical call sequence in the frame decoder (mirrors ssc_encode):
 *
 *   alloc_trim = ec_dec_icdf(ec, trim_icdf, 7);              (or 5 if not coded)
 *   bits = ((int32_t)nbBytes*8 << 3) - (int32_t)ec_tell_frac(ec) - 1;
 *   anti_collapse_rsv = (isTransient && bits >= (3+2)<<3) ? 1<<3 : 0;
 *   bits -= anti_collapse_rsv;
 *   codedBands = ssc_compute_allocation_dec(m, end, offsets, alloc_trim,
 *                    bitrate > 192000, bits, &balance, pulses, fine_quant,
 *                    fine_priority, &intensity, &dual_stereo, ec);
 *   ... unquant_fine_energy(fine_quant) -> quant_all_bands(pulses, balance,
 *       codedBands, intensity, dual_stereo) -> anti-collapse bit -> finalise
 */
#endif
