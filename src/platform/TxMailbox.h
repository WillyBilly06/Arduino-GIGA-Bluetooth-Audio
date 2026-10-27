#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace giga_bredr { namespace detail {
class TxMailbox {
public:
    static constexpr uint32_t Capacity = 1030;
    constexpr TxMailbox() noexcept : published_(0), cursor_(0), bytes_{} {}
    TxMailbox(const TxMailbox&) = delete;
    TxMailbox& operator=(const TxMailbox&) = delete;

    // Sole producer only. The caller supplies one complete packet and keeps
    // its source valid through this copy. H4 syntax is validated by the host.
    // No reader can see the new length until every byte and cursor are ready.
    bool submit(const uint8_t* packet, size_t length) {
        if (!packet || !length || length > Capacity ||
            __atomic_load_n(&published_, __ATOMIC_ACQUIRE)) return false;
        memcpy(bytes_, packet, length);
        cursor_ = 0;
        __atomic_store_n(&published_, uint32_t(length), __ATOMIC_RELEASE);
        return true;
    }
    // Producer may reuse the storage only after observing this acquire.
    bool empty() const { return __atomic_load_n(&published_, __ATOMIC_ACQUIRE) == 0; }

    // Sole consumer only. A rejected/stalled hardware write must leave the
    // cursor unchanged; call advance exactly once after one accepted byte.
    bool peek(uint8_t& byte) const {
        const uint32_t length = __atomic_load_n(&published_, __ATOMIC_ACQUIRE);
        if (!length || cursor_ >= length) return false;
        byte = bytes_[cursor_];
        return true;
    }
    bool advance() {
        const uint32_t length = __atomic_load_n(&published_, __ATOMIC_ACQUIRE);
        if (!length || cursor_ >= length) return false;
        ++cursor_;
        return true;
    }
    bool drained() const {
        const uint32_t length = __atomic_load_n(&published_, __ATOMIC_ACQUIRE);
        return length && cursor_ == length;
    }
    // Consumer only, AFTER detaching TX IRQ and saving completion metadata.
    // The last byte accepted by UART is still owned until this publication.
    // Do not touch this packet/cursor after the release: producer may reuse it.
    bool finish() {
        const uint32_t length = __atomic_load_n(&published_, __ATOMIC_ACQUIRE);
        if (!length || cursor_ != length) return false;
        __atomic_store_n(&published_, uint32_t(0), __ATOMIC_RELEASE);
        return true;
    }
    // Owner only, with producer quiescent and the consumer IRQ stopped.
    // Abort may discard any remaining bytes; it is not a TX-complete receipt.
    void resetStopped() {
        cursor_ = 0;
        __atomic_store_n(&published_, uint32_t(0), __ATOMIC_RELEASE);
    }
private:
    static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
                  "TxMailbox requires inline lock-free 32-bit atomics");
    alignas(uint32_t) uint32_t published_;
    uint32_t cursor_; // Producer initializes before publish; consumer then owns it.
    uint8_t bytes_[Capacity];
};
}}
