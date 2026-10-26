#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace giga_bredr { namespace detail {
// One serialized producer and one IRQ consumer. PCM words are native H7 TXDR
// words: left channel first, MSB first on the wire; never swap halfwords.
class I2sRing {
 public:
  I2sRing(int32_t* memory, uint32_t capacity): data_(memory), capacity_(capacity) {}
  void reset() { readPos_=writePos_=0; __atomic_store_n(&read_,0u,__ATOMIC_RELAXED); __atomic_store_n(&write_,0u,__ATOMIC_RELAXED); }
  size_t queued() const {
    const uint32_t r=__atomic_load_n(&read_,__ATOMIC_ACQUIRE);
    return __atomic_load_n(&write_,__ATOMIC_ACQUIRE)-r;
  }
  size_t push(const int32_t* pcm, size_t frames) {
    if (!pcm || !capacity_) return 0;
    const uint32_t w=__atomic_load_n(&write_,__ATOMIC_RELAXED);
    const uint32_t r=__atomic_load_n(&read_,__ATOMIC_ACQUIRE);
    const uint32_t room=capacity_-(w-r);
    const uint32_t n=frames>room?room:static_cast<uint32_t>(frames);
    for(uint32_t i=0;i<n;++i) { const uint32_t pos=(writePos_+i)%capacity_; data_[2*pos]=pcm[2*i]; data_[2*pos+1]=pcm[2*i+1]; }
    writePos_=(writePos_+n)%capacity_;
    __atomic_store_n(&write_,w+n,__ATOMIC_RELEASE); return n;
  }
  // Only the producer calls prime, before any DMA IRQ consumer starts. Leave
  // both output and queue untouched until real PCM can fill both DMA halves
  // and retain half-ring jitter reserve (clamped for deliberate tiny rings).
  bool prime(int32_t* dma, uint32_t halfFrames) {
    const size_t dmaFrames=size_t(halfFrames)*2;
    if(!dma || !halfFrames || dmaFrames>capacity_) return false;
    size_t threshold=capacity_/2+dmaFrames;
    if(threshold>capacity_) threshold=capacity_;
    if(queued()<threshold) return false;
    return fill(dma,static_cast<uint32_t>(dmaFrames))==dmaFrames;
  }
  // Returns real frames; always writes exactly frames including zero silence.
  size_t fill(int32_t* dma, uint32_t frames) {
    const uint32_t r=__atomic_load_n(&read_,__ATOMIC_RELAXED);
    const uint32_t w=__atomic_load_n(&write_,__ATOMIC_ACQUIRE);
    const uint32_t n=frames>w-r?w-r:frames;
    // The backend keeps ring and DMA storage disjoint. Copy each contiguous
    // span before publishing the consumed frames; zero input needs no copy.
    if(n) {
      const uint32_t first=n<capacity_-readPos_?n:capacity_-readPos_;
      memcpy(dma,data_+2*readPos_,size_t(first)*8);
      if(n>first) memcpy(dma+2*first,data_,size_t(n-first)*8);
    }
    memset(dma+2*n,0,size_t(frames-n)*8);
    readPos_=(readPos_+n)%capacity_;
    __atomic_store_n(&read_,r+n,__ATOMIC_RELEASE); return n;
  }
 private:
  int32_t* data_; uint32_t capacity_; uint32_t read_=0,write_=0,readPos_=0,writePos_=0;
};
inline bool i2sDmaAddress(uintptr_t a,size_t size) {
  return size && !(a%32) && ((a>=0x24000000u && a<0x24080000u && size<=0x24080000u-a) ||
         (a>=0x30000000u && a<0x30048000u && size<=0x30048000u-a));
}
struct I2sClock { uint32_t kernel_hz=0,divider=0,pll_n=0,pll_frac=0; };
inline bool i2sClock(uint32_t rate, I2sClock& c) {
  switch(rate) {
    case 11025: case 22050: case 44100: case 88200: case 176400: c.kernel_hz=45158400; break;
    case 8000: case 16000: case 24000: case 32000: case 48000: case 96000: case 192000: c.kernel_hz=49152000; break;
    default:return false;
  }
  c.divider=c.kernel_hz/(64*rate);
  const uint64_t ratio=(uint64_t(c.kernel_hz)*8*8192+2000000u)/4000000u;
  c.pll_n=ratio/8192; c.pll_frac=ratio%8192; return true;
}
struct I2sBufferGeometry {
  uint32_t half_frames = 0, ring_frames = 0;
  size_t dma_bytes = 0, ring_offset = 0, total_bytes = 0;
};
// H747 CLKGEN/HAL: ratio1 bypass is DIV0/ODD1; ratio3 is forbidden.
inline bool i2sPrescaler(uint32_t rate,bool mclk,uint32_t& div,uint32_t& odd) {
 I2sClock c;if(!i2sClock(rate,c))return false;
 const uint32_t unit=(mclk?256u:64u)*rate;
 if(!unit || c.kernel_hz%unit)return false;
 const uint32_t ratio=c.kernel_hz/unit;
 if(!ratio || ratio==3 || ratio>511)return false;
 div=ratio/2;odd=ratio&1u;return true;
}
inline bool i2sBufferGeometry(uint32_t rate, uint16_t buffer_ms, I2sBufferGeometry& out) {
  out = {};
  I2sClock clock;
  if (buffer_ms < 2 || buffer_ms > 100 || !i2sClock(rate, clock)) return false;
  out.half_frames = ((rate + 999) / 1000 + 3) & ~3u;
  out.ring_frames = ((rate * buffer_ms + 999) / 1000 + 3) & ~3u;
  if (out.ring_frames < out.half_frames * 2) out.ring_frames = out.half_frames * 2;
  out.dma_bytes = size_t(out.half_frames) * 16;
  out.ring_offset = (out.dma_bytes + sizeof(I2sRing) + 31) & ~size_t(31);
  out.total_bytes = out.ring_offset + size_t(out.ring_frames) * 8;
  return true;
}
}}



