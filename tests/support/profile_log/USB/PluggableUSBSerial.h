#pragma once
#include <stdint.h>
// Test-only stand-in: this test never opens or writes a physical USB device.
struct TestSerial {
    bool connected() const { return false; }
    int send_nb(uint8_t*, uint32_t, uint32_t* sent, bool) { *sent = 0; return 0; }
};
extern TestSerial _SerialUSB;
