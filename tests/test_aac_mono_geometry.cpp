extern "C" {
#include "../src/vendor/codecs/aac/sbr.h"
}
static_assert(SBRStorageBytes(0)==0&&SBRStorageBytes(3)==0,"Reject invalid geometry before allocation");
static_assert(SBRStorageBytes(2)-SBRStorageBytes(1)==960,"Mono must not reserve the unused envelope-dequant channel");
static_assert(SBRStorageBytes(1)>=sizeof(PSInfoSBR),"Owned allocation contains complete fixed state");
