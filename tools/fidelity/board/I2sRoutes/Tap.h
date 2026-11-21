#pragma once
#include <stdint.h>
// Separate header so the Arduino prototype generator sees the type first.
struct Tap { volatile uint32_t* idr; uint32_t shift[4]; uint8_t count; uint8_t bit[4]; };
