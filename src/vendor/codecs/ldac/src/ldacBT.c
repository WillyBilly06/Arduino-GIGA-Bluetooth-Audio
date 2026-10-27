/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "../../CodecPlatform.h"
#if GBR_ENABLE_LDAC
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
#include "../inc/ldacBT.h"

#include "ldacBT_internal.inc"
#include "ldacBT_api.inc"


#endif
