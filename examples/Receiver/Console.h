#pragma once
#include <USB/PluggableUSBSerial.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Main-thread diagnostics only. A disconnected USB monitor cannot stall audio.
class ReceiverConsole {
    char ring_[4096] = {};
    size_t read_ = 0, write_ = 0, used_ = 0;
public:
    unsigned dropped = 0;
    bool print(const char* format, ...) {
        char line[1024]; va_list args; va_start(args, format);
        int count = vsnprintf(line, sizeof(line), format, args); va_end(args);
        if (count <= 0) { ++dropped; return false; }
        // Never enqueue a silently truncated or partial record.
        if (size_t(count) >= sizeof(line)) { ++dropped; return false; }
        size_t bytes = size_t(count);
        if (bytes > sizeof(ring_) - used_) { ++dropped; return false; }
        for (size_t i = 0; i < bytes; ++i) { ring_[write_] = line[i]; write_ = (write_ + 1) % sizeof(ring_); }
        used_ += bytes;
        return true; // Ring admission only: not USB or peer delivery.
    }
    void pump() {
        if (!::_SerialUSB.connected() || !used_) return;
        size_t bytes = sizeof(ring_) - read_;
        if (bytes > used_) bytes = used_;
        if (bytes > 64) bytes = 64;
        uint32_t sent = 0;
        ::_SerialUSB.send_nb(reinterpret_cast<uint8_t*>(ring_) + read_, uint32_t(bytes), &sent, true);
        read_ = (read_ + sent) % sizeof(ring_); used_ -= sent;
    }
};
