extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/sbr.h"
int gbr_test_sbr_full(AACDecInfo* a){const PSInfoSBR* p=(PSInfoSBR*)a->psInfoSBR;return p && p->sbrHdr && p->sbrGrid && p->sbrFreq && p->sbrChan && p->delayIdxQMFA && p->delayQMFA && p->delayIdxQMFS && p->delayQMFS && p->XBufDelay;}
}
