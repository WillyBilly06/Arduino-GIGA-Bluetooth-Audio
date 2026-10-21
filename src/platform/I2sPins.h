#pragma once
#include <stdint.h>
#ifndef GIGA_BREDR_I2S_WS_PIN
#define GIGA_BREDR_I2S_WS_PIN 74
#endif
static_assert(GIGA_BREDR_I2S_WS_PIN==74 || GIGA_BREDR_I2S_WS_PIN==69,
              "I2S WS must be D74/PB12 (single J5 row) or legacy D69/PI0");
namespace giga_bredr { namespace detail {
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
