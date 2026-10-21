#include "Types.h"
namespace giga_bredr {
const char* statusName(Status s) {
    static const char* const names[] = {"ok", "invalid argument", "unsupported", "out of memory", "need more data", "corrupt frame", "output full", "busy", "hardware error", "firmware mismatch"};
    unsigned i = static_cast<unsigned>(s); return i < sizeof(names)/sizeof(*names) ? names[i] : "unknown";
}
const char* codecName(Codec c) {
    static const char* const names[] = {"SBC/SBC-XQ", "AAC-LC", "aptX", "aptX HD", "LDAC", "LHDC V5", "Opus", "HE-AAC"};
    unsigned i = static_cast<unsigned>(c); return i < sizeof(names)/sizeof(*names) ? names[i] : "unknown";
}
}
