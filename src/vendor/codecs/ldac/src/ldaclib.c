/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "../../CodecPlatform.h"
#if GBR_ENABLE_LDAC
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
#include "ldac.h"

#if CONFIG_USED_CSI_DSP
    #ifdef CONFIG_SCALAR_F64
        #undef CONFIG_SCALAR_F64
    #endif
    #define CONFIG_SCALAR_F64 0
#endif

#include "tables_ldac.inc"
#include "tables_sigproc_ldac.inc"
#include "setpcm_ldac.inc"
#include "bitalloc_sub_ldac.inc"
#include "memory_ldac.inc"
#include "ldaclib_api.inc"
#include "imdct_ldac.inc"
#include "dequant_ldac.inc"
#include "unpack_ldac.checked.inc"
#include "decode_ldac.inc"



#endif
