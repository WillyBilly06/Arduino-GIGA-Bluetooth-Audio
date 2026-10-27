#include "../src/platform/PcmRecovery.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace giga_bredr::detail;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
  fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
// Division is deliberately confined to the test oracle, not the IRQ code.
static int32_t reference(int32_t anchor,int32_t target,unsigned gain,unsigned n) {
  return int32_t((int64_t(anchor)*(n-gain)+int64_t(target)*gain)/n);
}
struct Fixture {
  std::vector<int32_t> mem,out;
  I2sRing ring; PcmRecovery p;
  unsigned half,capacity; uint64_t real=0,missing=0;
  Fixture(unsigned h):mem(h*8),out(h*4+2,123),ring(mem.data(),h*4),half(h),capacity(h*4) {
    CHECK(p.begin(1,capacity,half));
  }
  void push(unsigned frames,int32_t l,int32_t r) {
    std::vector<int32_t> pcm(frames*2);
    for(unsigned i=0;i<frames;++i) {pcm[2*i]=l;pcm[2*i+1]=r;}
    CHECK(ring.push(pcm.data(),frames)==frames);
  }
  void start(int32_t l,int32_t r) {
    push(p.snapshot().threshold,l,r);
    CHECK(ring.prime(out.data(),half));p.seedPrimed(out.data(),half*2);
    for(unsigned i=0;i<half*2;++i) CHECK(out[2*i]==l && out[2*i+1]==r);
  }
  unsigned tick(unsigned expected) {
    out.assign(out.size(),123);
    auto q=ring.queued();unsigned n=unsigned(p.refill(ring,out.data(),0));
    CHECK(n==expected && ring.queued()==q-n);
    CHECK(out[half*2]==123);real+=n;missing+=half-n;
    auto s=p.snapshot();
    CHECK(s.starved_frames+s.hold_frames==missing);
    CHECK(s.synthesized_frames+s.zero_frames==missing);
    return n;
  }
  void drain(unsigned frames) {while(frames) {tick(half);frames-=half;}}
};
static void carry_and_reversal() {
  // Six-frame halves, eight-frame ramp: missing begins in the final slot.
  Fixture f(6);f.start(800000,-1600000);f.drain(12);
  f.push(5,800000,-1600000);f.tick(5);
  CHECK(f.out[10]==700000 && f.out[11]==-1400000);
  auto q=f.ring.queued();f.tick(0);CHECK(f.ring.queued()==q);
  for(unsigned i=0;i<6;++i) {
    CHECK(f.out[2*i]==reference(800000,0,i+2,8));
    CHECK(f.out[2*i+1]==reference(-1600000,0,i+2,8));
  }
  // Resume before decay reaches zero: restart from the actual scheduled tail.
  CHECK(f.out[10]==100000 && f.out[11]==-200000);
  f.push(f.p.snapshot().threshold,400000,-800000);f.tick(6);
  for(unsigned i=0;i<6;++i) {
    CHECK(f.out[2*i]==reference(100000,400000,i+1,8));
    CHECK(f.out[2*i+1]==reference(-200000,-800000,i+1,8));
  }
  f.tick(6);
  CHECK(f.out[0]==reference(100000,400000,7,8));
  for(unsigned i=1;i<6;++i) CHECK(f.out[2*i]==400000 && f.out[2*i+1]==-800000);
  CHECK(f.p.snapshot().mixed_frames==8);
  f.tick(6);for(unsigned i=0;i<6;++i) CHECK(f.out[2*i]==400000 && f.out[2*i+1]==-800000);
}
static void rates_extremes_and_reset() {
  for(unsigned half:{1u,2u,4u,8u,12u,16u,24u,32u,48u,96u,180u,192u}) {
    for(int sign:{-1,1}) {
      const int32_t l=sign<0?INT32_MIN:INT32_MAX,r=sign<0?INT32_MAX:INT32_MIN;
      Fixture f(half);f.start(l,r);f.drain(half*2);
      unsigned n=f.p.snapshot().ramp_frames;
      CHECK(n>=2 && n<=256 && !(n&(n-1)));
      unsigned done=0;
      while(done<n) {
        f.tick(0);
        for(unsigned i=0;i<half;++i) {
          unsigned gain=done+i+1;if(gain>n)gain=n;
          CHECK(f.out[2*i]==reference(l,0,gain,n));
          CHECK(f.out[2*i+1]==reference(r,0,gain,n));
        }
        done+=half;
      }
      f.tick(0);for(unsigned i=0;i<half*2;++i)CHECK(f.out[i]==0);
      f.push(f.p.snapshot().threshold,r,l);f.tick(half);
      unsigned gain=0;
      do {
        for(unsigned i=0;i<half;++i) {
          unsigned g=gain+i+1;if(g>n)g=n;
          CHECK(f.out[2*i]==reference(0,r,g,n));
          CHECK(f.out[2*i+1]==reference(0,l,g,n));
        }
        gain+=half;
        if(gain<n) f.tick(half);
      } while(gain<n);
      CHECK(f.p.snapshot().mixed_frames==n);
      auto before=f.p.snapshot();f.p.end(0,unsigned(f.ring.queued()));
      f.out.assign(f.out.size(),99);auto q=f.ring.queued();
      CHECK(f.p.refill(f.ring,f.out.data(),0)==0 && f.ring.queued()==q);
      for(auto x:f.out)CHECK(x==99);
      CHECK(f.p.snapshot().mixed_frames==before.mixed_frames);
      CHECK(f.p.begin(2,f.capacity,half));
      auto s=f.p.snapshot();CHECK(!s.synthesized_frames && !s.zero_frames && !s.mixed_frames);
      // Before priming, no stale tail can be synthesized.
      f.ring.reset();f.missing=f.real=0;f.tick(0);
      for(unsigned i=0;i<half*2;++i)CHECK(f.out[i]==0);
    }
  }
}
static void steady_and_real_zero() {
  Fixture f(8);f.start(0,0);f.drain(16);f.tick(0);f.tick(0);
  CHECK(!f.p.snapshot().synthesized_frames && f.p.snapshot().zero_frames==16);
  f.push(f.p.snapshot().threshold,0,0);f.tick(8);
  CHECK(f.p.snapshot().mixed_frames==8); // Count processed frames, even zero PCM.
  f.drain(24); // Consume the preserved three-half reserve after resume.
  for(unsigned j=0;j<40;++j) {
    const int32_t l=int32_t(j*393939)-8000000,r=~l;
    f.push(8,l,r);f.tick(8);
    for(unsigned i=0;i<8;++i) CHECK(f.out[2*i]==l && f.out[2*i+1]==r);
  }
  CHECK(f.p.snapshot().mixed_frames==8);
  CHECK(f.p.snapshot().entries==1 && f.p.snapshot().resumes==1);
}
int main(){carry_and_reversal();rates_extremes_and_reset();steady_and_real_zero();
  std::printf("Production PCM transition boundary/extreme/queue/accounting checks=%u PASS\n",checks);
}
