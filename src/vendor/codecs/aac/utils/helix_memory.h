/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#pragma once
#include "../../CodecPlatform.h"
#define helix_malloc gbr_malloc
#define helix_free gbr_free
