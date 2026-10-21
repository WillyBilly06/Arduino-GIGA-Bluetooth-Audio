#include "../src/platform/I2sBuffer.h"
#include <thread>
#include <cassert>
#include <cstdio>
using namespace giga_bredr::detail;
int main() {
 int32_t memory[14]; I2sRing ring(memory,7);
 std::thread producer([&] { for(int32_t n=1;n<=500000;++n) { int32_t pcm[]={n,-n}; while(!ring.push(pcm,1)) std::this_thread::yield(); } });
 for(int32_t n=1;n<=500000;) { int32_t pcm[2]; if(ring.fill(pcm,1)) { assert(pcm[0]==n && pcm[1]==-n); ++n; } else assert(pcm[0]==0 && pcm[1]==0); }
 producer.join(); assert(ring.queued()==0);
 puts("I2S serialized-producer / IRQ-consumer ordering stress PASS");
}
