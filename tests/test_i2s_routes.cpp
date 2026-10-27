#include "../src/GigaBredr.h"
#include "../src/platform/I2sPins.h"
#include "../src/platform/I2sBuffer.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace giga_bredr;using namespace giga_bredr::detail;
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
int main(){
 ReceiverConfig c;CHECK(c.i2s.bclk==70 && c.i2s.data==72 && c.i2s.lrck==GIGA_BREDR_I2S_WS_PIN && c.i2s.mclk==-1);
 I2sRoutePlan p;CHECK(i2sRoutePlan(c.i2s,p) && p.peripheral==2 && p.count==3);
 unsigned combinations=0;
 for(const auto& b:I2sRoutes)if(b.signal==I2sBclk)for(const auto& d:I2sRoutes)if(d.signal==I2sData && d.peripheral==b.peripheral)
 for(const auto& w:I2sRoutes)if(w.signal==I2sLrck && w.peripheral==b.peripheral)for(int m:{-1,b.peripheral==1?76:b.peripheral==2?68:15}){
  I2sPins pins;pins.bclk=b.arduino;pins.data=d.arduino;pins.lrck=w.arduino;pins.mclk=m;CHECK(i2sRoutePlan(pins,p));CHECK((m==-1 || p.peripheral==b.peripheral) && p.count==(m==-1?3:4));CHECK(p.pins[0].arduino==pins.bclk && p.pins[1].arduino==pins.data && p.pins[2].arduino==pins.lrck);++combinations;
  for(unsigned i=0;i<p.count;++i){CHECK(p.pins[i].signal==i);CHECK(p.pins[i].peripheral==p.peripheral);uint32_t mode=0,type=0x1234,speed=0x55555555,pull=0xaaaaaaaa,low=0x87654321,high=0xfedcba98;I2sGpioRegisters r{mode,type,speed,pull,low,high};I2sPortPins mask(1u<<p.pins[i].pin);auto saved=i2sCapturePins(r,mask);CHECK(!i2sPinsBusy(r,mask));mode=2u<<(p.pins[i].pin*2);CHECK(i2sPinsBusy(r,mask));mode^=0xffffffffu;const uint32_t changed=mode;i2sRestorePins(r,mask,saved);CHECK(mode==((changed&~mask.fields2)|saved.mode));}
 }
 CHECK(combinations==138);
 for(unsigned signal=0;signal<4;++signal){I2sPins pins;int16_t* selected=signal==0?&pins.bclk:signal==1?&pins.data:signal==2?&pins.lrck:&pins.mclk;for(int invalid:{-2,103,999}){*selected=invalid;CHECK(!i2sRoutePlan(pins,p));CHECK(p.count==0 && p.peripheral==0);}}
 I2sPins bad;bad.data=90;CHECK(!i2sRoutePlan(bad,p));bad=I2sPins();bad.lrck=70;CHECK(!i2sRoutePlan(bad,p));bad=I2sPins();bad.mclk=76;CHECK(!i2sRoutePlan(bad,p));bad.bclk=96;CHECK(!i2sRoutePlan(bad,p));
 for(uint32_t rate:{8000u,11025u,16000u,22050u,24000u,32000u,44100u,48000u,88200u,96000u,176400u,192000u})for(bool m:{false,true}){
  I2sClock clk;uint32_t div=999,odd=999;CHECK(i2sClock(rate,clk));CHECK(i2sPrescaler(rate,m,div,odd));CHECK(clk.kernel_hz<=49152000);uint32_t real=div?2*div+odd:1;CHECK(clk.kernel_hz==rate*(m?256u:64u)*real);CHECK(!(div==1 && odd==1));if(m && rate>=176400)CHECK(div==0 && odd==1);
 }
 uint32_t div,odd;CHECK(!i2sPrescaler(12345,true,div,odd));printf("I2S routes combinations=%u checks=%u PASS\n",combinations,checks);
}
