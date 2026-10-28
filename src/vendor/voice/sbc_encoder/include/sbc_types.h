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
 *  Data type declarations.
 *
 ******************************************************************************/

#ifndef SBC_TYPES_H
#define SBC_TYPES_H

#include "../VoiceSbcCompat.h"
// Explicit arithmetic widths on both Cortex-M7 and LP64 CPU test hosts.
#if defined(__GNUC__) || defined(__clang__)
typedef int16_t SINT16 __attribute__((may_alias));
typedef int32_t SINT32 __attribute__((may_alias));
#else
typedef int16_t SINT16;
typedef int32_t SINT32;
#endif
typedef int64_t SINT64;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;

#define abs32(x) ( (x >= 0) ? x : (-x) )

#endif
