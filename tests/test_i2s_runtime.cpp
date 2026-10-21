#include "../src/platform/GigaI2s.h"
#include <cassert>
#include <cstdio>
using namespace giga_bredr;
int main() {
    // Host fallback does not claim DMA operation. Both ABI entry points link,
    // and runtime range validation happens before touching platform hardware.
    assert(detail::i2sBegin(48000)==Status::Unsupported);
    assert(detail::i2sBegin(48000,20)==Status::Unsupported);
    assert(detail::i2sBegin(48000,1)==Status::InvalidArgument);
    assert(detail::i2sBegin(48000,101)==Status::InvalidArgument);
    puts("PASS I2S legacy/runtime entry points and duration bounds");
}
