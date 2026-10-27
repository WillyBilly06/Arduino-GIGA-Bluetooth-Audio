#include "PacketQueue.h"
#include "Memory.h"
#include <string.h>
namespace giga_bredr { namespace detail {
Status PacketQueue::configure(size_t slots, size_t maximum, MemoryKind kind) {
    close();
    if (!slots || slots > 64 || !maximum || maximum > 65535) return Status::InvalidArgument;
    stride_ = maximum + sizeof(uint16_t);
    data_ = static_cast<uint8_t*>(allocate(stride_ * slots, kind));
    if (!data_) { close(); return Status::OutOfMemory; }
    slots_ = slots; maximum_ = maximum;
    return Status::Ok;
}
bool PacketQueue::push(const uint8_t* p, size_t n) {
    if (!data_ || !p || !n || n > maximum_ || count_ == slots_) return false;
    auto* slot = data_ + write_ * stride_; uint16_t size = uint16_t(n);
    memcpy(slot, &size, sizeof(size)); memcpy(slot + sizeof(size), p, n);
    write_ = (write_ + 1) % slots_; ++count_; return true;
}
size_t PacketQueue::take(uint8_t* out, size_t cap) {
    if (!count_ || !out) return 0;
    auto* slot = data_ + read_ * stride_; uint16_t size;
    memcpy(&size, slot, sizeof(size));
    if (size > cap) return 0;
    memcpy(out, slot + sizeof(size), size);
    read_ = (read_ + 1) % slots_; --count_; return size;
}
void PacketQueue::close() { release(data_); data_ = nullptr; slots_ = maximum_ = stride_ = 0; clear(); }
}}
