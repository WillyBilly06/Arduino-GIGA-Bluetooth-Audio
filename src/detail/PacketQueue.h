#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
// Single externally synchronized FIFO. No allocation in push/take.
class PacketQueue {
public:
    ~PacketQueue() { close(); }
    Status configure(size_t slots, size_t maximum_packet, MemoryKind kind);
    bool push(const uint8_t* packet, size_t bytes);
    size_t take(uint8_t* output, size_t capacity);
    void clear() { read_ = write_ = count_ = 0; }
    void close();
    size_t count() const { return count_; }
private:
    uint8_t* data_ = nullptr;
    size_t slots_ = 0, maximum_ = 0, stride_ = 0;
    size_t read_ = 0, write_ = 0, count_ = 0;
};
}}
