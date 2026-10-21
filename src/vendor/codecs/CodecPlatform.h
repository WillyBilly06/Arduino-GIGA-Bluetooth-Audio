#pragma once
#include "../../CodecBuildConfig.h"
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* gbr_malloc(size_t);
void* gbr_calloc(size_t,size_t);
void gbr_free(void*);
uint32_t gbr_ldac_read24(const uint8_t*,int,int);
int64_t gbr_codec_now_us(void);
#ifdef __cplusplus
}
#endif
#define USE_DEFAULT_STDLIB 1
#define LHDC_HOST_BUILD 1
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define VAR_ARRAYS 1
#define HAVE_LRINTF 1
#define HAVE_LRINT 1
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "giga-port"
#endif
