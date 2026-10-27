#pragma once
// Set with compiler build-property for the entire library, or edit this file.
#ifndef GBR_ENABLE_SBC
#define GBR_ENABLE_SBC 1
#endif
#ifndef GBR_ENABLE_AAC
#define GBR_ENABLE_AAC 1
#endif
#ifndef GBR_ENABLE_APTX
#define GBR_ENABLE_APTX 1
#endif
#ifndef GBR_ENABLE_LDAC
#define GBR_ENABLE_LDAC 1
#endif
#ifndef GBR_ENABLE_LHDC
#define GBR_ENABLE_LHDC 1
#endif
#ifndef GBR_ENABLE_OPUS
#define GBR_ENABLE_OPUS 1
#endif

// Experimental HE-AAC v1 core; requires independent vector/sender validation.
#ifndef GBR_ENABLE_AAC_HE
#define GBR_ENABLE_AAC_HE 1
#endif
