#include "../src/platform/I2sBuffer.h"
#include <cassert>
#include <cstdio>
using namespace giga_bredr::detail;
int main() {
 int32_t storage[32]={}, dma[12]; I2sRing ring(storage,16);
 int32_t pcm[32]; for(int i=0;i<16;++i) { pcm[2*i]=(i+1)*256; pcm[2*i+1]=-(i+1)*256; }
 for(auto& word:dma) word=0x42424242;
 assert(!ring.prime(dma,2)); // negotiation silence must not start a transfer
 assert(ring.push(pcm,11)==11); assert(!ring.prime(dma,2)); // 4 DMA + 8 reserve required
 assert(ring.queued()==11); for(auto word:dma) assert(word==0x42424242);
 assert(ring.push(pcm+22,1)==1); assert(ring.prime(dma,2));
 assert(ring.queued()==8); // initial DMA contains PCM, half-ring jitter reserve remains
 for(unsigned i=0;i<8;++i) assert(dma[i]==pcm[i]);
 for(unsigned i=8;i<12;++i) assert(dma[i]==0x42424242);
 assert(ring.fill(dma,4)==4); for(unsigned i=0;i<8;++i) assert(dma[i]==pcm[8+i]);
 ring.reset(); assert(!ring.prime(dma,2)); // rate/stream restart must prime afresh
 int32_t tinyMemory[8]; I2sRing tiny(tinyMemory,4);
 assert(tiny.push(pcm,4)==4); assert(tiny.prime(dma,2)); assert(tiny.queued()==0);
 int32_t tooSmallMemory[4]; I2sRing tooSmall(tooSmallMemory,2);
 assert(tooSmall.push(pcm,2)==2); assert(!tooSmall.prime(dma,2)); assert(tooSmall.queued()==2);
 assert(!ring.prime(dma,0)); assert(!ring.prime(nullptr,2));
 puts("I2S priming waits for PCM/reserve, preserves initial stereo DMA and resets PASS");
}
