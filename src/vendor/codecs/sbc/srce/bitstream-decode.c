/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "../../CodecPlatform.h"
#if GBR_ENABLE_SBC
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
/******************************************************************************
 *
 *  Copyright (C) 2014 The Android Open Source Project
 *  Copyright 2003 - 2004 Open Interface North America, Inc. All rights reserved.
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

/**********************************************************************************
  $Revision: #1 $
***********************************************************************************/

/**
@file
Functions for manipulating input bitstreams.

@ingroup codec_internal
*/

/**
@addtogroup codec_internal
@{
*/

#include "../Compat.h"
#include "../include/oi_stddefs.h"
#include "../include/oi_bitstream.h"
#include "../include/oi_assert.h"

#if (defined(SBC_DEC_INCLUDED) && SBC_DEC_INCLUDED == TRUE)

PRIVATE void OI_BITSTREAM_ReadInit(OI_BITSTREAM *bs,
                                   const OI_BYTE *buffer, const OI_BYTE *end)
{
    bs->end = end;
    if (end - buffer >= 3) {
        bs->value = ((OI_INT32)buffer[0] << 16) | ((OI_INT32)buffer[1] << 8) | buffer[2];
        bs->ptr.r = buffer + 3;
    } else {
        OI_UINT i;
        bs->value = 0;
        bs->ptr.r = buffer;
        for (i = 0; i < 3; ++i) {
            bs->value = (bs->value << 8) | (bs->ptr.r < end ? *bs->ptr.r++ : 0);
        }
    }
    bs->bitPtr = 8;
}

PRIVATE OI_UINT32 OI_BITSTREAM_ReadUINT(OI_BITSTREAM *bs, OI_UINT bits)
{
    OI_UINT32 result;

    OI_BITSTREAM_READUINT(result, bits, bs->ptr.r, bs->value, bs->bitPtr, bs->end);

    return result;
}

PRIVATE OI_UINT8 OI_BITSTREAM_ReadUINT4Aligned(OI_BITSTREAM *bs)
{
    OI_UINT32 result;

    OI_ASSERT(bs->bitPtr < 16);
    OI_ASSERT(bs->bitPtr % 4 == 0);

    if (bs->bitPtr == 8) {
        result = bs->value << 8;
        bs->bitPtr = 12;
    } else {
        result = bs->value << 12;
        bs->value = (bs->value << 8) | (bs->ptr.r < bs->end ? *bs->ptr.r++ : 0);
        bs->bitPtr = 8;
    }
    result >>= 28;
    OI_ASSERT(result < (1u << 4));
    return (OI_UINT8)result;
}

PRIVATE OI_UINT8 OI_BITSTREAM_ReadUINT8Aligned(OI_BITSTREAM *bs)
{
    OI_UINT32 result;
    OI_ASSERT(bs->bitPtr == 8);

    result = bs->value >> 16;
    bs->value = (bs->value << 8) | (bs->ptr.r < bs->end ? *bs->ptr.r++ : 0);

    return (OI_UINT8)result;
}

/**
@}
*/

#endif /* #if (defined(SBC_DEC_INCLUDED) && SBC_DEC_INCLUDED == TRUE) */

#endif
