/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "../CodecPlatform.h"
#if GBR_ENABLE_AAC && GBR_ENABLE_AAC_HE
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
#include <string.h>
/* ***** BEGIN LICENSE BLOCK *****  
 * Source last modified: $Id: sbr.c,v 1.3 2005/05/24 16:01:55 albertofloyd Exp $ 
 *   
 * Portions Copyright (c) 1995-2005 RealNetworks, Inc. All Rights Reserved.  
 *       
 * The contents of this file, and the files included with this file, 
 * are subject to the current version of the RealNetworks Public 
 * Source License (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the current version of the RealNetworks Community 
 * Source License (the "RCSL") available at 
 * http://www.helixcommunity.org/content/rcsl, in which case the RCSL 
 * will apply. You may also obtain the license terms directly from 
 * RealNetworks.  You may not use this file except in compliance with 
 * the RPSL or, if you have a valid RCSL with RealNetworks applicable 
 * to this file, the RCSL.  Please see the applicable RPSL or RCSL for 
 * the rights, obligations and limitations governing use of the 
 * contents of the file. 
 *   
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the 
 * portions it created. 
 *   
 * This file, and the files included with this file, is distributed 
 * and made available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY 
 * KIND, EITHER EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS 
 * ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET 
 * ENJOYMENT OR NON-INFRINGEMENT. 
 *  
 * Technology Compatibility Kit Test Suite(s) Location:  
 *    http://www.helixcommunity.org/content/tck  
 *  
 * Contributor(s):  
 *   
 * ***** END LICENSE BLOCK ***** */  

/**************************************************************************************
 * Fixed-point HE-AAC decoder
 * Jon Recker (jrecker@real.com)
 * February 2005
 *
 * sbr.c - top level functions for SBR
 **************************************************************************************/

#if defined(USE_DEFAULT_STDLIB) || defined(ARDUINO)
#include <stdio.h>
#include <stdlib.h>
#else
#include "hlxclib/stdlib.h"
#endif

#include "sbr.h"

/**************************************************************************************
 * Function:    InitSBRState
 *
 * Description: initialize PSInfoSBR struct at start of stream or after flush
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     PSInfoSBR struct with proper initial state
 *
 * Return:      none
 **************************************************************************************/
static void InitSBRState(PSInfoSBR *psi)
{
	int i, ch;
	unsigned char *c;

	if (!psi)
		return;

    /* Save ownership only. Copying the complete state before a variable-size
     * clear can retain a large automatic copy on the Cortex-M7 compiler. */
    struct {
        int allocatedChannels;
        SBRHeader *sbrHdr;
        SBRGrid *sbrGrid;
        SBRFreq *sbrFreq;
        SBRChan *sbrChan;
        int *delayIdxQMFA;
        int (*delayQMFA)[DELAY_SAMPS_QMFA];
        int *delayIdxQMFS;
        int (*delayQMFS)[DELAY_SAMPS_QMFS];
        int (*XBufDelay)[HF_GEN][64][2];
    } saved = {psi->allocatedChannels, psi->sbrHdr, psi->sbrGrid,
        psi->sbrFreq, psi->sbrChan, psi->delayIdxQMFA, psi->delayQMFA,
        psi->delayIdxQMFS, psi->delayQMFS, psi->XBufDelay};
    memset(psi,0,SBRStorageBytes(saved.allocatedChannels)); psi->allocatedChannels=saved.allocatedChannels;
    psi->sbrHdr=saved.sbrHdr; memset(psi->sbrHdr,0,psi->allocatedChannels*sizeof(*psi->sbrHdr));
    psi->sbrGrid=saved.sbrGrid; memset(psi->sbrGrid,0,psi->allocatedChannels*sizeof(*psi->sbrGrid));
    psi->sbrFreq=saved.sbrFreq; memset(psi->sbrFreq,0,psi->allocatedChannels*sizeof(*psi->sbrFreq));
    psi->sbrChan=saved.sbrChan; memset(psi->sbrChan,0,psi->allocatedChannels*sizeof(*psi->sbrChan));
    psi->delayIdxQMFA=saved.delayIdxQMFA; memset(psi->delayIdxQMFA,0,psi->allocatedChannels*sizeof(*psi->delayIdxQMFA));
    psi->delayQMFA=saved.delayQMFA; memset(psi->delayQMFA,0,psi->allocatedChannels*sizeof(*psi->delayQMFA));
    psi->delayIdxQMFS=saved.delayIdxQMFS; memset(psi->delayIdxQMFS,0,psi->allocatedChannels*sizeof(*psi->delayIdxQMFS));
    psi->delayQMFS=saved.delayQMFS; memset(psi->delayQMFS,0,psi->allocatedChannels*sizeof(*psi->delayQMFS));
    psi->XBufDelay=saved.XBufDelay; memset(psi->XBufDelay,0,psi->allocatedChannels*sizeof(*psi->XBufDelay));
	/* initialize non-zero state variables */
	for (ch = 0; ch < psi->allocatedChannels; ch++) {
		psi->sbrChan[ch].reset = 1;
		psi->sbrChan[ch].laPrev = -1;
	}
}
 
/**************************************************************************************
 * Function:    InitSBR
 *
 * Description: initialize SBR decoder
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     PSInfoSBR struct to hold SBR state information
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Note:        memory allocation for SBR is only done here
 **************************************************************************************/
int InitSBRChannels(AACDecInfo *info,int channels) {
    const size_t storageBytes=SBRStorageBytes(channels);
    if(!storageBytes)return ERR_AAC_SBR_INIT;
    PSInfoSBR *psi=(PSInfoSBR*)gbr_calloc(1,storageBytes);
    if(!psi)return ERR_AAC_SBR_INIT;
    info->psInfoSBR=psi; psi->allocatedChannels=channels;
    psi->sbrHdr=(SBRHeader*)gbr_calloc(channels,sizeof(*psi->sbrHdr)); if(!psi->sbrHdr)return ERR_AAC_SBR_INIT;
    psi->sbrGrid=(SBRGrid*)gbr_calloc(channels,sizeof(*psi->sbrGrid)); if(!psi->sbrGrid)return ERR_AAC_SBR_INIT;
    psi->sbrFreq=(SBRFreq*)gbr_calloc(channels,sizeof(*psi->sbrFreq)); if(!psi->sbrFreq)return ERR_AAC_SBR_INIT;
    psi->sbrChan=(SBRChan*)gbr_calloc(channels,sizeof(*psi->sbrChan)); if(!psi->sbrChan)return ERR_AAC_SBR_INIT;
    psi->delayIdxQMFA=(int*)gbr_calloc(channels,sizeof(*psi->delayIdxQMFA)); if(!psi->delayIdxQMFA)return ERR_AAC_SBR_INIT;
    psi->delayQMFA=(int(*)[DELAY_SAMPS_QMFA])gbr_calloc(channels,sizeof(*psi->delayQMFA)); if(!psi->delayQMFA)return ERR_AAC_SBR_INIT;
    psi->delayIdxQMFS=(int*)gbr_calloc(channels,sizeof(*psi->delayIdxQMFS)); if(!psi->delayIdxQMFS)return ERR_AAC_SBR_INIT;
    psi->delayQMFS=(int(*)[DELAY_SAMPS_QMFS])gbr_calloc(channels,sizeof(*psi->delayQMFS)); if(!psi->delayQMFS)return ERR_AAC_SBR_INIT;
    psi->XBufDelay=(int(*)[HF_GEN][64][2])gbr_calloc(channels,sizeof(*psi->XBufDelay)); if(!psi->XBufDelay)return ERR_AAC_SBR_INIT;
    InitSBRState(psi); return 0;
}
int InitSBR(AACDecInfo *info) { return InitSBRChannels(info,2); }
int InitSBRPre(AACDecInfo *info,void **p,int *sz) { return ERR_AAC_SBR_INIT; }
/**************************************************************************************
 * Function:    FreeSBR
 *
 * Description: free SBR decoder
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     none
 *
 * Return:      none
 *
 * Note:        memory deallocation for SBR is only done here
 **************************************************************************************/
void FreeSBR(AACDecInfo *info) {
    if(!info || !info->psInfoSBR)return; PSInfoSBR *psi=(PSInfoSBR*)info->psInfoSBR;
    gbr_free(psi->sbrHdr);
    gbr_free(psi->sbrGrid);
    gbr_free(psi->sbrFreq);
    gbr_free(psi->sbrChan);
    gbr_free(psi->delayIdxQMFA);
    gbr_free(psi->delayQMFA);
    gbr_free(psi->delayIdxQMFS);
    gbr_free(psi->delayQMFS);
    gbr_free(psi->XBufDelay);
    gbr_free(psi); info->psInfoSBR=0;
}
/**************************************************************************************
 * Function:    DecodeSBRBitstream
 *
 * Description: decode sideband information for SBR
 *
 * Inputs:      valid AACDecInfo struct
 *              fill buffer with SBR extension block
 *              number of bytes in fill buffer
 *              base output channel (range = [0, nChans-1])
 *
 * Outputs:     initialized state structs (SBRHdr, SBRGrid, SBRFreq, SBRChan)
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Notes:       SBR payload should be in aacDecInfo->fillBuf
 *              returns with no error if fill buffer is not an SBR extension block, 
 *                or if current block is not a fill block (e.g. for LFE upsampling)
 **************************************************************************************/
int DecodeSBRBitstream(AACDecInfo *aacDecInfo, int chBase)
{
	int headerFlag;
	BitStreamInfo bsi;
	PSInfoSBR *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoSBR)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoSBR *)(aacDecInfo->psInfoSBR);
    if(chBase<0 || chBase>=psi->allocatedChannels) return ERR_AAC_SBR_NCHANS_TOO_HIGH;
    int blockChannels=aacDecInfo->currBlockID==AAC_ID_LFE?1:
        aacDecInfo->prevBlockID==AAC_ID_SCE?1:
        aacDecInfo->prevBlockID==AAC_ID_CPE?2:0;
    if(blockChannels<=0 || blockChannels>psi->allocatedChannels-chBase)
        return ERR_AAC_SBR_NCHANS_TOO_HIGH;


	if (aacDecInfo->currBlockID != AAC_ID_FIL || (aacDecInfo->fillExtType != EXT_SBR_DATA && aacDecInfo->fillExtType != EXT_SBR_DATA_CRC))
		return ERR_AAC_NONE;

	SetBitstreamPointer(&bsi, aacDecInfo->fillCount, aacDecInfo->fillBuf);
	if (GetBits(&bsi, 4) != (unsigned int)aacDecInfo->fillExtType)
		return ERR_AAC_SBR_BITSTREAM;
	
	if (aacDecInfo->fillExtType == EXT_SBR_DATA_CRC)
		psi->crcCheckWord = GetBits(&bsi, 10);

	headerFlag = GetBits(&bsi, 1);
	if (headerFlag) {
		/* get sample rate index for output sample rate (2x base rate) */
		psi->sampRateIdx = GetSampRateIdx(2 * aacDecInfo->sampRate);
		if (psi->sampRateIdx < 0 || psi->sampRateIdx >= NUM_SAMPLE_RATES)
			return ERR_AAC_SBR_BITSTREAM;
		else if (psi->sampRateIdx >= NUM_SAMPLE_RATES_SBR)
			return ERR_AAC_SBR_SINGLERATE_UNSUPPORTED;

		/* reset flag = 1 if header values changed */
		if (UnpackSBRHeader(&bsi, &(psi->sbrHdr[chBase])))
			psi->sbrChan[chBase].reset = 1;
	
		/* first valid SBR header should always trigger CalcFreqTables(), since psi->reset was set in InitSBR() */
		if (psi->sbrChan[chBase].reset)
			CalcFreqTables(&(psi->sbrHdr[chBase+0]), &(psi->sbrFreq[chBase]), psi->sampRateIdx);

		/* copy and reset state to right channel for CPE */
		if (aacDecInfo->prevBlockID == AAC_ID_CPE)
			psi->sbrChan[chBase+1].reset = psi->sbrChan[chBase+0].reset;
	}
	

	/* if no header has been received, upsample only */
	if (psi->sbrHdr[chBase].count == 0)
		return ERR_AAC_NONE;

	if (aacDecInfo->prevBlockID == AAC_ID_SCE) {
		UnpackSBRSingleChannel(&bsi, psi, chBase);
	} else if (aacDecInfo->prevBlockID == AAC_ID_CPE) {
		UnpackSBRChannelPair(&bsi, psi, chBase);
	} else {
		return ERR_AAC_SBR_BITSTREAM;
	}

	ByteAlignBitstream(&bsi);

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    DecodeSBRData
 *
 * Description: apply SBR to one frame of PCM data
 *
 * Inputs:      1024 samples of decoded 32-bit PCM, before SBR
 *              size of input PCM samples (must be 4 bytes)
 *              number of fraction bits in input PCM samples
 *              base output channel (range = [0, nChans-1])
 *              initialized state structs (SBRHdr, SBRGrid, SBRFreq, SBRChan)
 *
 * Outputs:     2048 samples of decoded 16-bit PCM, after SBR
 *
 * Return:      0 if successful, error code (< 0) if error
 **************************************************************************************/
int DecodeSBRData(AACDecInfo *aacDecInfo, int chBase, short *outbuf)
{
	int k, l, ch, chBlock, qmfaBands, qmfsBands;
	int upsampleOnly, gbIdx, gbMask;
	int *inbuf;
	short *outptr;
	PSInfoSBR *psi;
	SBRHeader *sbrHdr;
	SBRGrid *sbrGrid;
	SBRFreq *sbrFreq;
	SBRChan *sbrChan;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoSBR)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoSBR *)(aacDecInfo->psInfoSBR);
    if(chBase<0 || chBase>=psi->allocatedChannels) return ERR_AAC_SBR_NCHANS_TOO_HIGH;
    int blockChannels=aacDecInfo->currBlockID==AAC_ID_LFE?1:
        aacDecInfo->prevBlockID==AAC_ID_SCE?1:
        aacDecInfo->prevBlockID==AAC_ID_CPE?2:0;
    if(blockChannels<=0 || blockChannels>psi->allocatedChannels-chBase)
        return ERR_AAC_SBR_NCHANS_TOO_HIGH;

	
	/* same header and freq tables for both channels in CPE */
	sbrHdr =  &(psi->sbrHdr[chBase]);
	sbrFreq = &(psi->sbrFreq[chBase]);

	/* upsample only if we haven't received an SBR header yet or if we have an LFE block */
	if (aacDecInfo->currBlockID == AAC_ID_LFE) {
		chBlock = 1;
		upsampleOnly = 1;
	} else if (aacDecInfo->currBlockID == AAC_ID_FIL) {
		if (aacDecInfo->prevBlockID == AAC_ID_SCE) 
			chBlock = 1;
		else if (aacDecInfo->prevBlockID == AAC_ID_CPE)
			chBlock = 2;
		else
			return ERR_AAC_NONE;
		
		upsampleOnly = (sbrHdr->count == 0 ? 1 : 0);
		if (aacDecInfo->fillExtType != EXT_SBR_DATA && aacDecInfo->fillExtType != EXT_SBR_DATA_CRC)
			return ERR_AAC_NONE;
	} else {
		/* ignore non-SBR blocks */
		return ERR_AAC_NONE;
	}

	if (upsampleOnly) {
		sbrFreq->kStart = 32;
		sbrFreq->numQMFBands = 0;
	}

	for (ch = 0; ch < chBlock; ch++) {
		sbrGrid = &(psi->sbrGrid[chBase + ch]);	
		sbrChan = &(psi->sbrChan[chBase + ch]);

		if (aacDecInfo->rawSampleBuf[ch] == 0 || aacDecInfo->rawSampleBytes != 4)
			return ERR_AAC_SBR_PCM_FORMAT;
		inbuf = (int *)aacDecInfo->rawSampleBuf[ch];
		outptr = outbuf + chBase + ch;

		/* restore delay buffers (could use ring buffer or keep in temp buffer for nChans == 1) */
		for (l = 0; l < HF_GEN; l++) {
			for (k = 0; k < 64; k++) {
				psi->XBuf[l][k][0] = psi->XBufDelay[chBase + ch][l][k][0];
				psi->XBuf[l][k][1] = psi->XBufDelay[chBase + ch][l][k][1];
			}
		}

		/* step 1 - analysis QMF */
		qmfaBands = sbrFreq->kStart;
		for (l = 0; l < 32; l++) {
			gbMask = QMFAnalysis(inbuf + l*32, psi->delayQMFA[chBase + ch], psi->XBuf[l + HF_GEN][0], 
				aacDecInfo->rawSampleFBits, &(psi->delayIdxQMFA[chBase + ch]), qmfaBands);

			gbIdx = ((l + HF_GEN) >> 5) & 0x01;	
			sbrChan->gbMask[gbIdx] |= gbMask;	/* gbIdx = (0 if i < 32), (1 if i >= 32) */
		}

		if (upsampleOnly) {
			/* no SBR - just run synthesis QMF to upsample by 2x */
			qmfsBands = 32;
			for (l = 0; l < 32; l++) {
				/* step 4 - synthesis QMF */
				QMFSynthesis(psi->XBuf[l + HF_ADJ][0], psi->delayQMFS[chBase + ch], &(psi->delayIdxQMFS[chBase + ch]), qmfsBands, outptr, aacDecInfo->nChans);
				outptr += 64*aacDecInfo->nChans;
			}
		} else {
			/* if previous frame had lower SBR starting freq than current, zero out the synthesized QMF
			 *   bands so they aren't used as sources for patching
			 * after patch generation, restore from delay buffer
			 * can only happen after header reset
			 */
			for (k = sbrFreq->kStartPrev; k < sbrFreq->kStart; k++) {
				for (l = 0; l < sbrGrid->envTimeBorder[0] + HF_ADJ; l++) {
					psi->XBuf[l][k][0] = 0;
					psi->XBuf[l][k][1] = 0;
				}
			}

			/* step 2 - HF generation */
			GenerateHighFreq(psi, sbrGrid, sbrFreq, sbrChan, ch);

			/* restore SBR bands that were cleared before patch generation (time slots 0, 1 no longer needed) */
			for (k = sbrFreq->kStartPrev; k < sbrFreq->kStart; k++) {
				for (l = HF_ADJ; l < sbrGrid->envTimeBorder[0] + HF_ADJ; l++) {
					psi->XBuf[l][k][0] = psi->XBufDelay[chBase + ch][l][k][0];
					psi->XBuf[l][k][1] = psi->XBufDelay[chBase + ch][l][k][1];
				}
			}

			/* step 3 - HF adjustment */
			AdjustHighFreq(psi, sbrHdr, sbrGrid, sbrFreq, sbrChan, ch);

			/* step 4 - synthesis QMF */
			qmfsBands = sbrFreq->kStartPrev + sbrFreq->numQMFBandsPrev;
			for (l = 0; l < sbrGrid->envTimeBorder[0]; l++) {
				/* if new envelope starts mid-frame, use old settings until start of first envelope in this frame */
				QMFSynthesis(psi->XBuf[l + HF_ADJ][0], psi->delayQMFS[chBase + ch], &(psi->delayIdxQMFS[chBase + ch]), qmfsBands, outptr, aacDecInfo->nChans);
				outptr += 64*aacDecInfo->nChans;
			}

			qmfsBands = sbrFreq->kStart + sbrFreq->numQMFBands;
			for (     ; l < 32; l++) {
				/* use new settings for rest of frame (usually the entire frame, unless the first envelope starts mid-frame) */
				QMFSynthesis(psi->XBuf[l + HF_ADJ][0], psi->delayQMFS[chBase + ch], &(psi->delayIdxQMFS[chBase + ch]), qmfsBands, outptr, aacDecInfo->nChans);
				outptr += 64*aacDecInfo->nChans;
			}
		}

		/* save delay */
		for (l = 0; l < HF_GEN; l++) {
			for (k = 0; k < 64; k++) {		
				psi->XBufDelay[chBase + ch][l][k][0] = psi->XBuf[l+32][k][0];
				psi->XBufDelay[chBase + ch][l][k][1] = psi->XBuf[l+32][k][1];
			}
		}
		sbrChan->gbMask[0] = sbrChan->gbMask[1];
		sbrChan->gbMask[1] = 0;

		if (sbrHdr->count > 0)
			sbrChan->reset = 0;
	}
	sbrFreq->kStartPrev = sbrFreq->kStart;
	sbrFreq->numQMFBandsPrev = sbrFreq->numQMFBands;

	if (aacDecInfo->nChans > 0 && (chBase + ch) == aacDecInfo->nChans)
		psi->frameCount++;

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    FlushCodecSBR
 *
 * Description: flush internal SBR codec state (after seeking, for example)
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     updated state variables for SBR
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Notes:       SBR is heavily dependent on state from previous frames
 *                (e.g. delta coded scalefactors, previous envelope boundaries, etc.)
 *              On flush, we reset everything as if SBR had just been initialized
 *                for the first time. This triggers "upsample-only" mode until
 *                the first valid SBR header is received. Then SBR starts as usual.
 **************************************************************************************/
int FlushCodecSBR(AACDecInfo *aacDecInfo)
{
	PSInfoSBR *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoSBR)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoSBR *)(aacDecInfo->psInfoSBR);

	InitSBRState(psi);

	return 0;
}

#endif
