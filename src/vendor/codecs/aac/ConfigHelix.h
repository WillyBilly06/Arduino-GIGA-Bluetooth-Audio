/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#ifndef CONFIG_HELIX_H
#define CONFIG_HELIX_H
#ifndef USE_DEFAULT_STDLIB
#define USE_DEFAULT_STDLIB 1
#endif
#define HELIX_LOGGING_ACTIVE 0
#if GBR_ENABLE_AAC_HE
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR 1
#endif
#endif
