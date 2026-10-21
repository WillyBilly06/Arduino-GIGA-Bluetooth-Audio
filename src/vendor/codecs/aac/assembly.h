/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
/* ***** BEGIN LICENSE BLOCK *****  
 * Source last modified: $Id: assembly.h,v 1.7 2005/11/10 00:04:40 margotm Exp $ 
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
 * assembly.h - inline assembly language functions and prototypes
 *
 * MULSHIFT32(x, y) 		signed multiply of two 32-bit integers (x and y), 
 *                            returns top 32-bits of 64-bit result
 * CLIPTOSHORT(x)			convert 32-bit integer to 16-bit short, 
 *                            clipping to [-32768, 32767]
 * FASTABS(x)               branchless absolute value of signed integer x
 * CLZ(x)                   count leading zeros on signed integer x
 * MADD64(sum64, x, y)		64-bit multiply accumulate: sum64 += (x*y)
 **************************************************************************************/

#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H

#include <stdint.h>
typedef int64_t Word64;
typedef union { Word64 w64; struct { unsigned int lo32; signed int hi32; } r; } U64;
#define CLIP_2N(y,n) do { int sg=(y)>>31; if(sg!=((y)>>(n))) (y)=sg^(int)((1u<<(n))-1u); } while(0)
#define CLIP_2N_SHIFT(y,n) do { int sg=(y)>>31; if(sg!=((y)>>(30-(n)))) (y)=sg^0x3fffffff; else (y)=(int)((unsigned)(y)<<(n)); } while(0)
static inline int MULSHIFT32(int x,int y) { return (int)(((int64_t)x*y)>>32); }
static inline short CLIPTOSHORT(int x) { return x>32767?32767:x< -32768?-32768:(short)x; }
static inline int FASTABS(int x) { unsigned s=(unsigned)x>>31; return (int)(((unsigned)x^(0u-s))+s); }
static inline int CLZ(int x) { return x?__builtin_clz((unsigned)x):32; }
static inline Word64 MADD64(Word64 s,int x,int y) { return (Word64)((uint64_t)s+(uint64_t)((int64_t)x*y)); }
static inline Word64 SHL64(Word64 x,int n) { return (Word64)((uint64_t)x<<n); }
static inline Word64 SAR64(Word64 x,int n) { return x>>n; }
#endif /* _ASSEMBLY_H */
