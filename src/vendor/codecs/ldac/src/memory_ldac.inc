/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
#include "ldac.h"
#include <malloc.h>
/***************************************************************************************************
    Align Memory
***************************************************************************************************/
#define LDAC_ALLOC_LINE 8

DECLFUNC size_t align_ldac(size_t size) {
  if (LDAC_ALLOC_LINE != 0) {
    size = (((size - 1) / LDAC_ALLOC_LINE) + 1) * LDAC_ALLOC_LINE;
  }

  return size;
}

/***************************************************************************************************
    Clear Allocate Memory
***************************************************************************************************/
DECLFUNC void* calloc_ldac(SFINFO* p_sfinfo, size_t nmemb, size_t size) {
  char* p_mempos;

  p_mempos = p_sfinfo->p_mempos;
  if (p_mempos) {
    p_sfinfo->p_mempos = &p_mempos[nmemb * align_ldac(size)];
    return p_mempos;
  } else {
    return gbr_calloc(nmemb, size);
  }
}
