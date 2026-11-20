/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* Part of the SSC decoder. Derived from libopus 1.1 (BSD license, see COPYING.opus) and from
 * analysis of Samsung's SSC encoder; see docs/notes for the format notes. */
#include "ssc_ec.h"

/* ---- quant_bands.c tables (SSC .rodata) ---- */
#define SSC_DB_SHIFT      10
#define SSC_MAX_FINE_BITS 8

/* e_prob_model intra @0x5d91, inter @0x5dbf (46 bytes each). The first 42 bytes equal opus e_prob_model[3][*].
   The index is 2*min(i,20), so the last 4 bytes (bands 21/22) are never used. */
static const unsigned char ssc_e_prob_model[2][46] = {
    /* [0] Inter @0x5dbf */
    { 42,121,  96, 66, 108, 43, 111, 40, 117, 44, 123, 32, 120, 36, 119, 33, 127, 33, 134, 34, 139, 21,
     147, 23, 152, 20, 158, 25, 154, 26, 166, 21, 173, 16, 184, 13, 184, 10, 150, 13, 139, 15,
     130, 17, 125, 19 },
    /* [1] Intra @0x5d91 */
    { 22,178,  63,114,  74, 82,  84, 83,  92, 82, 103, 62,  96, 72,  96, 67, 101, 73, 107, 72, 113, 55,
     118, 52, 125, 52, 118, 52, 117, 55, 135, 49, 137, 39, 157, 32, 145, 29,  97, 33,  77, 40,
      68, 45,  62, 49 }
};
static const unsigned char ssc_small_energy_icdf[3] = { 2, 1, 0 };   /* @0x5ded */


/* LM = 3 is the only frame size: pred_coef[3], beta_coef[3], beta_intra. */
#define SSC_PRED_COEF   16384   /* 0.5  Q15 */
#define SSC_BETA_COEF   6554    /* 0.2  Q15 */
#define SSC_BETA_INTRA  4915    /* 0.15 Q15 */

/* Bit budget used by the encoder for coarse energy, finalise and allocation:
   nbCompressedBytes = min(nbBytes,1275), while the range-coder storage stays nbBytes. */

/* Intra flag: decoded right before unquant_coarse_energy (opus celt_decode_with_ec does this too). */

/* unquant_coarse_energy: opus 1.1 fixed point with LM=3 and explicit budget.
   oldEBands: int16 Q10, layout [c*nbEBands + i], nbEBands = 22. SSC encoder: start=0, C=2. */
void ssc_unquant_coarse_energy(int nbEBands, int start, int end, opus_val16 *oldEBands,
                               int intra, ec_dec *dec, int C, opus_int32 budget)
{
    const unsigned char *prob_model = ssc_e_prob_model[intra ? 1 : 0];
    opus_val32 prev[2] = { 0, 0 };
    opus_val16 coef = intra ? 0 : SSC_PRED_COEF;
    opus_val16 beta = intra ? SSC_BETA_INTRA : SSC_BETA_COEF;
    int i, c;

    for (i = start; i < end; i++) {
        c = 0;
        do {
            int qi;
            opus_val32 q, tmp;
            opus_int32 tell = ec_tell(dec);
            opus_val16 *e = &oldEBands[i + c * nbEBands];

            if (budget - tell >= 15) {
                int pi = 2 * (i < 20 ? i : 20);
                qi = ec_laplace_decode(dec, (unsigned)prob_model[pi] << 7, prob_model[pi + 1] << 6);
            } else if (budget - tell >= 2) {
                qi = ec_dec_icdf(dec, ssc_small_energy_icdf, 2);
                qi = (qi >> 1) ^ -(qi & 1);
            } else if (budget - tell >= 1) {
                qi = -ec_dec_bit_logp(dec, 1);
            } else {
                qi = -1;
            }
            q = (opus_val32)((opus_uint32)qi << SSC_DB_SHIFT);                 /* Q10 */

            if (*e < -(9 << SSC_DB_SHIFT)) *e = -(9 << SSC_DB_SHIFT);           /* MAX16(-9.0, oldE) */
            /* PSHR32(coef*oldE, 8): coef is 0 or 16384, so rounding has no effect (the encoder uses a plain >>8). */
            tmp = ((coef * (opus_val32)*e + 128) >> 8) + prev[c] + (opus_val32)((opus_uint32)q << 7);
            if (tmp < -(28 << (SSC_DB_SHIFT + 7))) tmp = -(28 << (SSC_DB_SHIFT + 7));  /* -0x380000 */
            *e = (opus_val16)((tmp + 64) >> 7);                                  /* PSHR32(tmp,7) */
            /* prev += SHL32(q,7) - MULT16_16(beta, PSHR32(q,8));  PSHR32(q,8) == qi*4 */
            prev[c] = prev[c] + (opus_val32)((opus_uint32)q << 7)
                    - (opus_val32)beta * (opus_val16)((q + 128) >> 8);
        } while (++c < C);
    }
}

/* unquant_fine_energy: unchanged from opus 1.1 (Q10). */
void ssc_unquant_fine_energy(int nbEBands, int start, int end, opus_val16 *oldEBands,
                             const int *fine_quant, ec_dec *dec, int C)
{
    int i, c;
    for (i = start; i < end; i++) {
        int fq = fine_quant[i];
        if (fq <= 0) continue;
        c = 0;
        do {
            int q2 = (int)ec_dec_bits(dec, (unsigned)fq);
            /* SUB16(SHR32(SHL32(q2,10) + 512, fq), 512) */
            opus_val16 offset = (opus_val16)((((opus_val32)q2 << SSC_DB_SHIFT) + (1 << (SSC_DB_SHIFT - 1))) >> fq)
                              - (1 << (SSC_DB_SHIFT - 1));
            oldEBands[i + c * nbEBands] = (opus_val16)(oldEBands[i + c * nbEBands] + offset);
        } while (++c < C);
    }
}

/* unquant_energy_finalise: unchanged from opus 1.1. bits_left = total_bits - ec_tell(dec), taken after
   the anti-collapse bit (same position as in opus). total_bits = ssc_total_bits(nbBytes). */
void ssc_unquant_energy_finalise(int nbEBands, int start, int end, opus_val16 *oldEBands,
                                 const int *fine_quant, const int *fine_priority,
                                 int bits_left, ec_dec *dec, int C)
{
    int i, prio, c;
    for (prio = 0; prio < 2; prio++) {
        for (i = start; i < end && bits_left >= C; i++) {
            if (fine_quant[i] >= SSC_MAX_FINE_BITS || fine_priority[i] != prio)
                continue;
            c = 0;
            do {
                int q2 = (int)ec_dec_bits(dec, 1);
                /* SHR16(SHL16(q2,10) - 512, fine_quant+1) */
                opus_val16 offset = (opus_val16)((((opus_val32)q2 << SSC_DB_SHIFT) - (1 << (SSC_DB_SHIFT - 1)))
                                                 >> (fine_quant[i] + 1));
                oldEBands[i + c * nbEBands] = (opus_val16)(oldEBands[i + c * nbEBands] + offset);
                bits_left--;
            } while (++c < C);
        }
    }
}
#endif
