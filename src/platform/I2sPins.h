#pragma once
#include <stdint.h>
#include "../I2sConfig.h"
#ifndef GIGA_BREDR_I2S_WS_PIN
#define GIGA_BREDR_I2S_WS_PIN 74
#endif
static_assert(GIGA_BREDR_I2S_WS_PIN==74 || GIGA_BREDR_I2S_WS_PIN==69,
              "I2S WS must be D74/PB12 (single J5 row) or legacy D69/PI0");
namespace giga_bredr { namespace detail {
enum I2sSignal : uint8_t { I2sBclk, I2sData, I2sLrck, I2sMclk };
struct I2sRoutePin { uint8_t arduino,port,pin,peripheral,af,signal; };
// GIGA4.6 variant x DS12930 table9. Only board-accessible output pads;
// PA15/D92 (USB power) and PB10/D96 (WiFi power) are intentionally excluded.
constexpr I2sRoutePin I2sRoutes[] = {
 {85,0,5,1,5,I2sBclk},{91,1,3,1,5,I2sBclk},{62,6,11,1,5,I2sBclk},
 {5,0,7,1,5,I2sData},{93,1,5,1,5,I2sData},{90,3,7,1,5,I2sData},
 {84,0,4,1,5,I2sLrck},{44,6,10,1,5,I2sLrck},{76,2,4,1,5,I2sMclk},
 {1,0,9,2,5,I2sBclk},{94,1,13,2,5,I2sBclk},{75,3,3,2,5,I2sBclk},{70,8,1,2,5,I2sBclk},
 {73,2,1,2,5,I2sData},{80,2,3,2,5,I2sData},{72,8,3,2,5,I2sData},
 {7,1,4,2,7,I2sLrck},{9,1,9,2,5,I2sLrck},{74,1,12,2,5,I2sLrck},{69,8,0,2,5,I2sLrck},
 {68,2,6,2,5,I2sMclk},
 {91,1,3,3,6,I2sBclk},{47,1,2,3,7,I2sData},{93,1,5,3,7,I2sData},{19,3,6,3,5,I2sData},
 {84,0,4,3,6,I2sLrck},{15,2,7,3,6,I2sMclk}
};
struct I2sRoutePlan { uint8_t peripheral=0,count=0; I2sRoutePin pins[4] = {}; };
inline bool i2sRoutePlan(const I2sPins& config,I2sRoutePlan& output) {
 output={}; const int pins[]={config.bclk,config.data,config.lrck,config.mclk};
 if(pins[0]<0 || pins[1]<0 || pins[2]<0 || pins[3]<-1) return false;
 for(uint8_t peripheral=1;peripheral<=3;++peripheral) {
  I2sRoutePlan plan;plan.peripheral=peripheral;plan.count=config.mclk==-1?3:4;bool found=true;
  for(uint8_t signal=0;signal<plan.count;++signal) {
   bool match=false;
   for(const auto& route:I2sRoutes) if(route.arduino==pins[signal] && route.signal==signal && route.peripheral==peripheral) {plan.pins[signal]=route;match=true;break;}
   if(!match){found=false;break;}
   for(uint8_t i=0;i<signal;++i) if(plan.pins[i].port==plan.pins[signal].port && plan.pins[i].pin==plan.pins[signal].pin)found=false;
  }
  if(found){output=plan;return true;}
 }
 return false;
}
struct I2sPortPins {
 uint32_t pins=0,fields2=0,afLow=0,afHigh=0;
 constexpr explicit I2sPortPins(uint32_t mask):pins(mask) {
  for(unsigned pin=0;pin<16;++pin) if(mask&(1u<<pin)) {
   fields2|=3u<<(pin*2);
   if(pin<8) afLow|=15u<<(pin*4); else afHigh|=15u<<((pin-8)*4);
  }
 }
};
struct I2sPinPlan { unsigned wsArduino; I2sPortPins portB,portI; };
constexpr I2sPinPlan i2sPinPlan(unsigned ws) {
 return {ws,I2sPortPins(ws==74?1u<<12:0),I2sPortPins((1u<<1)|(1u<<3)|(ws==69?1u:0))};
}
// References bind to either real volatile GPIO registers or host-owned words.
// Zero pin masks perform no register accesses, notably for legacy GPIOB.
struct I2sGpioRegisters {
 volatile uint32_t &mode,&type,&speed,&pull,&afLow,&afHigh;
};
struct I2sGpioSnapshot { uint32_t mode=0,type=0,speed=0,pull=0,afLow=0,afHigh=0; };
inline bool i2sPinsBusy(I2sGpioRegisters r,const I2sPortPins& pins) {
 for(unsigned pin=0;pin<16;++pin) if(pins.pins&(1u<<pin)) {
  const uint32_t mode=(r.mode>>(pin*2))&3u;
  if(mode==1 || mode==2) return true;
 }
 return false;
}
inline I2sGpioSnapshot i2sCapturePins(I2sGpioRegisters r,const I2sPortPins& p) {
 if(!p.pins) return {};
 return {r.mode&p.fields2,r.type&p.pins,r.speed&p.fields2,r.pull&p.fields2,r.afLow&p.afLow,r.afHigh&p.afHigh};
}
inline void i2sRestorePins(I2sGpioRegisters r,const I2sPortPins& p,const I2sGpioSnapshot& s) {
 if(!p.pins) return;
 r.mode=(r.mode&~p.fields2)|(s.mode&p.fields2);
 r.type=(r.type&~p.pins)|(s.type&p.pins);
 r.speed=(r.speed&~p.fields2)|(s.speed&p.fields2);
 r.pull=(r.pull&~p.fields2)|(s.pull&p.fields2);
 if(p.afLow) r.afLow=(r.afLow&~p.afLow)|(s.afLow&p.afLow);
 if(p.afHigh) r.afHigh=(r.afHigh&~p.afHigh)|(s.afHigh&p.afHigh);
}
}}
