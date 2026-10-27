#ifndef LHDC_ENTROPY_LIMITS_H
#define LHDC_ENTROPY_LIMITS_H

/* These are FAC symbol-history lengths, not packet/PCM capacities. The five
 * production band configurations are defined here once so a future window
 * change cannot silently exceed the corresponding exact history storage. */
#define LHDC_FAC_HISTORY1_CAPACITY 96
#define LHDC_FAC_HISTORY2_CAPACITY 64
#define LHDC_FAC_BAND_CONFIGS(X) \
    X(LHDC_BAND_CFG_480,    32, 480,  _band_off_480,    _band_scale_480,    57, 63) \
    X(LHDC_BAND_CFG_480_HR, 32, 480,  _band_off_480_hr, _band_scale_480_hr, 57, 63) \
    X(LHDC_BAND_CFG_480_LB, 24, 480,  _band_off_480_lb, _band_scale_480_lb, 57, 63) \
    X(LHDC_BAND_CFG_960,    32, 960,  _band_off_960,    _band_scale_960,    96, 64) \
    X(LHDC_BAND_CFG_1920,   32, 1920, _band_off_1920,   _band_scale_1920,   96, 64)

#if defined(__cplusplus)
#define LHDC_FAC_ASSERT static_assert
#else
#define LHDC_FAC_ASSERT _Static_assert
#endif
#define LHDC_FAC_CHECK_CONFIG(cfg,sfb,n,offset,scale,w1,w2) \
    LHDC_FAC_ASSERT((w1)>0 && (w1)<=LHDC_FAC_HISTORY1_CAPACITY, "FAC stream1 production window exceeds history"); \
    LHDC_FAC_ASSERT((w2)>0 && (w2)<=LHDC_FAC_HISTORY2_CAPACITY, "FAC stream2 production window exceeds history");
LHDC_FAC_BAND_CONFIGS(LHDC_FAC_CHECK_CONFIG)
#undef LHDC_FAC_CHECK_CONFIG
#undef LHDC_FAC_ASSERT
#endif
