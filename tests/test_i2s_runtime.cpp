#include "../src/platform/GigaI2s.h"
#include <cassert>
#include <cstdio>
using namespace giga_bredr;
int main() {
    // Host fallback does not claim DMA operation. Legacy/new ABI entry points link,
    // and runtime range validation happens before touching platform hardware.
    assert(detail::i2sBegin(48000)==Status::Unsupported);
    assert(detail::i2sBegin(48000,20)==Status::Unsupported);
    assert(detail::i2sBegin(48000,150)==Status::Unsupported);
    assert(detail::i2sBegin(48000,320)==Status::Unsupported);
    assert(detail::i2sBegin(48000,320,I2sPins{},150)==Status::Unsupported);
    assert(detail::i2sBegin(48000,320,I2sPins{},321)==Status::InvalidArgument);
    assert(detail::i2sBegin(48000,1)==Status::InvalidArgument);
    assert(detail::i2sBegin(48000,321)==Status::InvalidArgument);
    puts("PASS I2S legacy/runtime entry points and duration bounds");
}
