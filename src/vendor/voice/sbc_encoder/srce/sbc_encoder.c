/* GigaBredr voice port, 2026-10-27: portable exact-width arithmetic,
 * selected mono mSBC geometry and instance-owned analysis state.
 * Original Apache-2.0 terms and Broadcom notices below are retained. */
/******************************************************************************
 *
 *  Copyright (C) 1999-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  contains code for encoder flow and initialization of encoder
 *
 ******************************************************************************/

#include <string.h>
#include "../VoiceSbcCompat.h"
#include "../include/sbc_encoder.h"
#include "../include/sbc_enc_func_declare.h"

#if (defined(SBC_ENC_INCLUDED) && SBC_ENC_INCLUDED == TRUE)

#define EncMaxShiftCounter (pstrEncParams->voiceMaxShift)

#if (SBC_JOINT_STE_INCLUDED == TRUE)
SINT32   s32LRDiff[SBC_MAX_NUM_OF_BLOCKS]    = {0};
SINT32   s32LRSum[SBC_MAX_NUM_OF_BLOCKS]     = {0};
#endif

void SBC_Encoder(SBC_ENC_PARAMS *pstrEncParams)
{
    if (!pstrEncParams || pstrEncParams->sbc_mode != SBC_MODE_MSBC ||
        pstrEncParams->s16NumOfChannels != 1 || pstrEncParams->s16NumOfSubBands != 8 ||
        pstrEncParams->s16NumOfBlocks != 15 || pstrEncParams->s16BitPool != 26) return;
    SINT32 s32Ch;                               /* counter for ch*/
    SINT32 s32Sb;                               /* counter for sub-band*/
    UINT32 u32Count, maxBit = 0;                          /* loop count*/
    SINT32 s32MaxValue;                         /* temp variable to store max value */

    SINT16 *ps16ScfL;
    SINT32 *SbBuffer;
    SINT32 s32Blk;                              /* counter for block*/
    SINT32  s32NumOfBlocks   = pstrEncParams->s16NumOfBlocks;
#if (SBC_JOINT_STE_INCLUDED == TRUE)
    SINT32 s32MaxValue2;
    UINT32 u32CountSum, u32CountDiff;
    SINT32 *pSum, *pDiff;
#endif
    register SINT32  s32NumOfSubBands = pstrEncParams->s16NumOfSubBands;

    pstrEncParams->pu8NextPacket = pstrEncParams->pu8Packet;

#if (SBC_NO_PCM_CPY_OPTION == TRUE)
    pstrEncParams->ps16NextPcmBuffer = pstrEncParams->ps16PcmBuffer;
#else
    pstrEncParams->ps16NextPcmBuffer  = pstrEncParams->as16PcmBuffer;
#endif
    do {
        /* SBC analysis filter*/
        if (s32NumOfSubBands == 4) {
            SbcAnalysisFilter4(pstrEncParams);
        } else {
            SbcAnalysisFilter8(pstrEncParams);
        }

        /* compute the scale factor, and save the max */
        ps16ScfL = pstrEncParams->as16ScaleFactor;
        s32Ch = pstrEncParams->s16NumOfChannels * s32NumOfSubBands;

        pstrEncParams->ps16NextPcmBuffer += s32Ch * s32NumOfBlocks; /* in case of multiple sbc frame to encode update the pcm pointer */

        for (s32Sb = 0; s32Sb < s32Ch; s32Sb++) {
            SbBuffer = pstrEncParams->s32SbBuffer + s32Sb;
            s32MaxValue = 0;
            for (s32Blk = s32NumOfBlocks; s32Blk > 0; s32Blk--) {
                if (s32MaxValue < abs32(*SbBuffer)) {
                    s32MaxValue = abs32(*SbBuffer);
                }
                SbBuffer += s32Ch;
            }

            u32Count = (s32MaxValue > 0x800000) ? 9 : 0;

            for ( ; u32Count < 15; u32Count++) {
                if (s32MaxValue <= (SINT32)(0x8000 << u32Count)) {
                    break;
                }
            }
            *ps16ScfL++ = (SINT16)u32Count;

            if (u32Count > maxBit) {
                maxBit = u32Count;
            }
        }
        /* In case of JS processing,check whether to use JS */
#if (SBC_JOINT_STE_INCLUDED == TRUE)
        if (pstrEncParams->s16ChannelMode == SBC_JOINT_STEREO) {
            /* Calculate sum and difference  scale factors for making JS decision   */
            ps16ScfL = pstrEncParams->as16ScaleFactor ;
            /* calculate the scale factor of Joint stereo max sum and diff */
            for (s32Sb = 0; s32Sb < s32NumOfSubBands - 1; s32Sb++) {
                SbBuffer = pstrEncParams->s32SbBuffer + s32Sb;
                s32MaxValue2 = 0;
                s32MaxValue = 0;
                pSum       = s32LRSum;
                pDiff      = s32LRDiff;
                for (s32Blk = 0; s32Blk < s32NumOfBlocks; s32Blk++) {
                    *pSum = (*SbBuffer + * (SbBuffer + s32NumOfSubBands)) >> 1;
                    if (abs32(*pSum) > s32MaxValue) {
                        s32MaxValue = abs32(*pSum);
                    }
                    pSum++;
                    *pDiff = (*SbBuffer - * (SbBuffer + s32NumOfSubBands)) >> 1;
                    if (abs32(*pDiff) > s32MaxValue2) {
                        s32MaxValue2 = abs32(*pDiff);
                    }
                    pDiff++;
                    SbBuffer += s32Ch;
                }
                u32Count = (s32MaxValue > 0x800000) ? 9 : 0;
                for ( ; u32Count < 15; u32Count++) {
                    if (s32MaxValue <= (SINT32)(0x8000 << u32Count)) {
                        break;
                    }
                }
                u32CountSum = u32Count;
                u32Count = (s32MaxValue2 > 0x800000) ? 9 : 0;
                for ( ; u32Count < 15; u32Count++) {
                    if (s32MaxValue2 <= (SINT32)(0x8000 << u32Count)) {
                        break;
                    }
                }
                u32CountDiff = u32Count;
                if ( (*ps16ScfL + * (ps16ScfL + s32NumOfSubBands)) > (SINT16)(u32CountSum + u32CountDiff) ) {

                    if (u32CountSum > maxBit) {
                        maxBit = u32CountSum;
                    }

                    if (u32CountDiff > maxBit) {
                        maxBit = u32CountDiff;
                    }

                    *ps16ScfL = (SINT16)u32CountSum;
                    *(ps16ScfL + s32NumOfSubBands) = (SINT16)u32CountDiff;

                    SbBuffer = pstrEncParams->s32SbBuffer + s32Sb;
                    pSum       = s32LRSum;
                    pDiff      = s32LRDiff;

                    for (s32Blk = 0; s32Blk < s32NumOfBlocks; s32Blk++) {
                        *SbBuffer = *pSum;
                        *(SbBuffer + s32NumOfSubBands) = *pDiff;

                        SbBuffer += s32NumOfSubBands << 1;
                        pSum++;
                        pDiff++;
                    }

                    pstrEncParams->as16Join[s32Sb] = 1;
                } else {
                    pstrEncParams->as16Join[s32Sb] = 0;
                }
                ps16ScfL++;
            }
            pstrEncParams->as16Join[s32Sb] = 0;
        }
#endif

        pstrEncParams->s16MaxBitNeed = (SINT16)maxBit;

        /* bit allocation */
        if ((pstrEncParams->s16ChannelMode == SBC_STEREO) || (pstrEncParams->s16ChannelMode == SBC_JOINT_STEREO)) {
            sbc_enc_bit_alloc_ste(pstrEncParams);
        } else {
            sbc_enc_bit_alloc_mono(pstrEncParams);
        }

        /* Quantize the encoded audio */
        EncPacking(pstrEncParams);
    } while (--(pstrEncParams->u8NumPacketToEncode));

    pstrEncParams->u8NumPacketToEncode = 1; /* default is one for retrocompatibility purpose */

}

/****************************************************************************
* InitSbcAnalysisFilt - Initializes the input data to 0
*
* RETURNS : N/A
*/
void SBC_Encoder_Init(SBC_ENC_PARAMS *pstrEncParams)
{
    /* Dedicated mSBC profile: no stereo/standard-SBC state is admitted. */
    if (!pstrEncParams || pstrEncParams->sbc_mode != SBC_MODE_MSBC) return;
    pstrEncParams->u8NumPacketToEncode = 1;
    pstrEncParams->s16ChannelMode = SBC_MONO;
    pstrEncParams->s16NumOfChannels = 1;
    pstrEncParams->s16SamplingFreq = SBC_sf16000;
    pstrEncParams->s16BitPool = 26;
    pstrEncParams->s16NumOfSubBands = 8;
    pstrEncParams->s16NumOfBlocks = 15;
    pstrEncParams->s16AllocationMethod = SBC_LOUDNESS;
    pstrEncParams->FrameHeader = 0;
    pstrEncParams->u16PacketLength = 0;
    pstrEncParams->u16BitRate = 60800;
    EncMaxShiftCounter = ((ENC_VX_BUFFER_SIZE - 8 * 10) >> 3) << 3;
    SbcAnalysisInit(pstrEncParams);
}

#endif /* #if (defined(SBC_ENC_INCLUDED) && SBC_ENC_INCLUDED == TRUE) */
