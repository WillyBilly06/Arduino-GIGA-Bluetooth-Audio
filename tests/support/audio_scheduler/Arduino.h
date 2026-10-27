#pragma once
#include <stdint.h>
uint32_t micros();
inline uint32_t millis(){return micros()/1000;}
