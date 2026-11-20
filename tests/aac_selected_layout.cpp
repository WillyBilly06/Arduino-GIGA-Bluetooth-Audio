#include <cstddef>
extern "C" {
#include "vendor/codecs/aac/aaccommon.h"
#include "vendor/codecs/aac/coder.h"
size_t gbr_test_plane(){const PSInfoBase *b=nullptr;return sizeof(b->icsInfo[0])+sizeof(b->scaleFactors[0])+sizeof(b->sfbCodeBook[0])+sizeof(b->pnsUsed[0])+sizeof(b->intensityUsed[0])+sizeof(b->pulseInfo[0])+sizeof(b->tnsInfo[0])+sizeof(b->gainControlInfo[0])+sizeof(b->gbCurrent[0]);}
void* gbr_test_work(AACDecInfo* a){return ((PSInfoBase*)a->psInfoBase)->sbrWorkBuf;}
}
