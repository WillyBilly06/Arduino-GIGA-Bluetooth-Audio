#pragma once
#include <stdint.h>
#ifndef GIGA_BREDR_I2S_WS_PIN
#define GIGA_BREDR_I2S_WS_PIN 74
#endif
namespace giga_bredr {
// Arduino digital pin numbers; MCLK=-1 leaves its hardware pad unused.
// A route must resolve to one hardware I2S peripheral. No GPIO emulation.
struct I2sPins {
    int16_t bclk = 70, data = 72, lrck = GIGA_BREDR_I2S_WS_PIN, mclk = -1;
};
}
