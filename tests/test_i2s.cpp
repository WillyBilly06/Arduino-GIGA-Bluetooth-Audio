#include "../src/platform/I2sBuffer.h"
#include <cassert>
#include <cstdio>
#include <climits>
#include <initializer_list>
using namespace giga_bredr::detail;
int main() {
 int32_t storage[8] = {}; I2sRing ring(storage, 4);
 const int32_t pcm[] = {0x12345600, int32_t(0xabcdef00u), INT32_MIN, INT32_MAX, 0x10000, -65536, 7, -8, 9, -10};
 assert(ring.push(pcm,5)==4); assert(ring.queued()==4); assert(ring.push(pcm,1)==0);
 int32_t dma[12]; for (auto &x:dma) x=42;
 assert(ring.fill(dma,3)==3);
 for (unsigned i=0;i<6;++i) assert(dma[i]==pcm[i]);
 assert(ring.push(pcm+8,1)==1); assert(ring.fill(dma,4)==2);
 assert(dma[0]==7 && dma[1]==-8 && dma[2]==9 && dma[3]==-10);
 for(unsigned i=4;i<8;++i) assert(dma[i]==0);
 assert(dma[8]==42); assert(ring.queued()==0); assert(ring.push(nullptr,4)==0);
 assert(ring.fill(dma,4)==0); for(unsigned i=0;i<8;++i) assert(dma[i]==0);
 ring.push(pcm,2); ring.reset(); assert(ring.queued()==0);
 for(unsigned n=0;n<10000;++n) { assert(ring.push(pcm,3)==3); assert(ring.fill(dma,3)==3); }
 I2sClock c; assert(!i2sClock(0,c)); assert(!i2sClock(12345,c));
 for(uint32_t rate : {8000u,11025u,16000u,22050u,24000u,32000u,44100u,48000u,88200u,96000u,176400u,192000u}) {
   assert(i2sClock(rate,c)); assert(c.divider>=4 && c.divider<=511);
   assert(c.kernel_hz == rate*64u*c.divider);
   const uint64_t scaled=(uint64_t(c.pll_n)*8192+c.pll_frac)*4000000u;
   const uint64_t ideal=uint64_t(c.kernel_hz)*8*8192;
   assert((scaled>ideal?scaled-ideal:ideal-scaled)*1000000u<ideal);
 }
 assert(i2sDmaAddress(0x24000000u,32)); assert(i2sDmaAddress(0x30047fe0u,32));
 assert(!i2sDmaAddress(0x2407ffe0u,64)); assert(!i2sDmaAddress(0x24000001u,32));
 assert(!i2sDmaAddress(0x20000000u,32)); assert(!i2sDmaAddress(0xc0000000u,32));
 assert(!i2sDmaAddress(0xffffffe0u,64));
 I2sBufferGeometry geometry;
 assert(!i2sBufferGeometry(48000,1,geometry)); assert(!i2sBufferGeometry(48000,101,geometry));
 assert(!i2sBufferGeometry(12345,20,geometry));
 for(uint32_t rate : {16000u,44100u,48000u,96000u,192000u}) {
   for(uint16_t ms : {uint16_t(2),uint16_t(20),uint16_t(100)}) {
     assert(i2sBufferGeometry(rate,ms,geometry));
     assert(geometry.half_frames==(((rate+999)/1000+3)&~3u));
     uint32_t wanted=((rate*ms+999)/1000+3)&~3u;
     if(wanted<geometry.half_frames*2) wanted=geometry.half_frames*2;
     assert(geometry.ring_frames==wanted && geometry.dma_bytes==size_t(geometry.half_frames)*16);
     assert(geometry.ring_offset%32==0 && geometry.ring_offset>=geometry.dma_bytes+sizeof(I2sRing));
     assert(geometry.total_bytes==geometry.ring_offset+size_t(wanted)*8);
   }
 }
 puts("I2S sample preservation, ring bounds/wrap/silence and clock-family tests PASS");
}

