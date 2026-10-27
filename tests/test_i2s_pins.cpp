#include "../src/platform/I2sPins.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace giga_bredr::detail;
struct Registers {
 uint32_t mode=0,type=0,speed=0,pull=0,low=0,high=0;
 I2sGpioRegisters view() { return {mode,type,speed,pull,low,high}; }
};
void checkRestore(const I2sPortPins& pins) {
 Registers r; r.mode=0xdeadbeefu; r.type=0x12345678u; r.speed=0x87654321u;
 r.pull=0x5a5a5a5au; r.low=0x13579bdfu; r.high=0x2468ace0u;
 Registers before=r; const auto saved=i2sCapturePins(r.view(),pins);
 // A later owner changes every unreserved bit while I2S changes its own bits.
 r.mode^=0xffffffffu; r.type^=0xffffffffu; r.speed^=0xffffffffu;
 r.pull^=0xffffffffu; r.low^=0xffffffffu; r.high^=0xffffffffu;
 Registers changed=r; i2sRestorePins(r.view(),pins,saved);
 assert(r.mode==((changed.mode&~pins.fields2)|(before.mode&pins.fields2)));
 assert(r.type==((changed.type&~pins.pins)|(before.type&pins.pins)));
 assert(r.speed==((changed.speed&~pins.fields2)|(before.speed&pins.fields2)));
 assert(r.pull==((changed.pull&~pins.fields2)|(before.pull&pins.fields2)));
 assert(r.low==((changed.low&~pins.afLow)|(before.low&pins.afLow)));
 assert(r.high==((changed.high&~pins.afHigh)|(before.high&pins.afHigh)));
}
int main() {
 const auto row=i2sPinPlan(74), legacy=i2sPinPlan(69);
 assert(row.portB.pins==0x1000 && row.portI.pins==0x000a);
 assert(row.portB.fields2==0x03000000 && row.portB.afLow==0 && row.portB.afHigh==0x000f0000);
 assert(row.portI.fields2==0xcc && row.portI.afLow==0xf0f0 && row.portI.afHigh==0);
 assert(legacy.portB.pins==0 && legacy.portI.pins==0x000b);
 assert(legacy.portI.fields2==0xcf && legacy.portI.afLow==0xf0ff);
 Registers b,i; b.mode=1u<<(13*2); i.mode=2u; // PB13 and PI0 may belong to unrelated users.
 assert(!i2sPinsBusy(b.view(),row.portB)); assert(!i2sPinsBusy(i.view(),row.portI));
 assert(i2sPinsBusy(i.view(),legacy.portI));
 for(unsigned mode=0;mode<4;++mode) {
  b.mode=mode<<(12*2); assert(i2sPinsBusy(b.view(),row.portB)==(mode==1||mode==2));
  for(unsigned pin : {1u,3u}) { i.mode=mode<<(pin*2); assert(i2sPinsBusy(i.view(),row.portI)==(mode==1||mode==2)); }
 }
 for(auto pins : {row.portB,row.portI,legacy.portB,legacy.portI}) checkRestore(pins);
 const auto selected=i2sPinPlan(GIGA_BREDR_I2S_WS_PIN);
 assert(selected.wsArduino==GIGA_BREDR_I2S_WS_PIN);
#if GIGA_BREDR_I2S_WS_PIN == 74
 assert(selected.portB.pins==0x1000 && selected.portI.pins==0xa);
#else
 assert(selected.portB.pins==0 && selected.portI.pins==0xb);
#endif
 puts("I2S selected WS profile, independent GPIO masks, conflicts and masked restore PASS");
}

