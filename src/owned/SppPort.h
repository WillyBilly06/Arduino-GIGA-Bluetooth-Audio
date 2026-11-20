#pragma once
#include "../GigaSpp.h"
#include <atomic>
#include <cstring>
extern "C" {
#include "core/bth_sdp.h"
}
namespace giga_bredr { namespace detail {
// One Serial Port Profile DLC endpoint: an immutable SDP record plus fixed
// caller-reserved RX/TX byte rings. No allocation, clock or protocol state.
// Every method except connected() runs under the radio owner's serialization
// (the actor itself, or a foreground call holding the radio mutex).
class SppPort {
public:
    static constexpr size_t NameLimit = 32, RecordBytes = 112, MinimumRing = 512, MaximumRing = 16384;
    static constexpr uint32_t RecordHandle = 0x10006;
    SppPort() = default;
    SppPort(const SppPort&) = delete;
    SppPort& operator=(const SppPort&) = delete;
    // Returns the exact SDP attribute bytes for a Serial Port record.
    static size_t buildRecord(uint8_t* out, size_t capacity, uint8_t channel, const char* name) {
        if (!out || !name || !channel || channel > 30) return 0;
        size_t length = 0; while (length <= NameLimit && name[length]) ++length;
        if (!length || length > NameLimit) return 0;
        const uint8_t head[] = {
            0x09, 0x00, 0x00, 0x0a, uint8_t(RecordHandle >> 24), uint8_t(RecordHandle >> 16), uint8_t(RecordHandle >> 8), uint8_t(RecordHandle),
            0x09, 0x00, 0x01, 0x35, 0x03, 0x19, 0x11, 0x01,                                     // ServiceClassIDList: SerialPort
            0x09, 0x00, 0x04, 0x35, 0x0c, 0x35, 0x03, 0x19, 0x01, 0x00,                         // ProtocolDescriptorList: L2CAP
            0x35, 0x05, 0x19, 0x00, 0x03, 0x08, channel,                                        //   RFCOMM(channel)
            0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02,                                     // BrowseGroupList: public
            0x09, 0x00, 0x06, 0x35, 0x09, 0x09, 0x65, 0x6e, 0x09, 0x00, 0x6a, 0x09, 0x01, 0x00, // Language base: en, UTF-8, 0x0100
            0x09, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x01, 0x09, 0x01, 0x02,       // Profile: SerialPort 1.2
            0x09, 0x01, 0x00, 0x25, uint8_t(length)};                                           // ServiceName
        if (capacity < sizeof head + length) return 0;
        memcpy(out, head, sizeof head); memcpy(out + sizeof head, name, length);
        return sizeof head + length;
    }
    bool begin(const char* name, uint8_t channel, uint8_t* rx, size_t rx_size, uint8_t* tx, size_t tx_size, uint8_t streaming_interval_ms) {
        end();
        if (!rx || !tx || rx_size < MinimumRing || rx_size > MaximumRing || tx_size < MinimumRing || tx_size > MaximumRing) return false;
        const size_t n = buildRecord(record_bytes_, sizeof record_bytes_, channel, name);
        if (!n) return false;
        record_ = {RecordHandle, {record_bytes_, n}}; // The actor validates it at installation.
        rx_ = rx; tx_ = tx; rx_size_ = rx_size; tx_size_ = tx_size; channel_ = channel; interval_ = streaming_interval_ms;
        active_ = true; return true;
    }
    void end() {
        connected_.store(false, std::memory_order_release);
        rx_ = tx_ = nullptr; rx_size_ = tx_size_ = rx_read_ = rx_count_ = tx_read_ = tx_count_ = 0;
        record_ = {}; memset(record_bytes_, 0, sizeof record_bytes_); stats_ = {};
        generation_ = 0; frame_bytes_ = peer_credits_ = 0; channel_ = interval_ = 0; credit_ = active_ = false;
    }
    bool enabled() const { return active_; }
    uint8_t channel() const { return channel_; }
    uint8_t streamingIntervalMs() const { return interval_; }
    const bth_sdp_record_t* record() const { return active_ ? &record_ : nullptr; }
    // Credit window that keeps window*frame_bytes reserved in the RX ring.
    uint8_t creditWindow(size_t frame_bytes) const {
        if (!active_ || !frame_bytes) return 0;
        const size_t frames = rx_size_ / frame_bytes; return uint8_t(frames > 7 ? 7 : frames);
    }
    // Actor side.
    void open(uint32_t generation, uint16_t frame_bytes, bool credit) {
        if (!active_) return;
        stats_.rx_dropped_bytes += uint32_t(rx_count_); rx_read_ = rx_count_ = 0; // Unread previous session.
        stats_.tx_dropped_bytes += uint32_t(tx_count_); tx_read_ = tx_count_ = 0;
        generation_ = generation; frame_bytes_ = frame_bytes; credit_ = credit; peer_credits_ = 0; ++stats_.connections;
        connected_.store(true, std::memory_order_release);
    }
    void close() {
        if (!generation_) return;
        connected_.store(false, std::memory_order_release);
        stats_.tx_dropped_bytes += uint32_t(tx_count_); tx_read_ = tx_count_ = 0; // RX stays readable.
        generation_ = 0; frame_bytes_ = 0; credit_ = false; peer_credits_ = 0;
    }
    uint32_t generation() const { return generation_; }
    void observeCredits(uint16_t credits) { peer_credits_ = credits; }
    size_t rxFree() const { return rx_size_ - rx_count_; }
    // Stores the whole payload or drops it; never a partial frame.
    bool rxPush(const uint8_t* p, size_t n) {
        if (!active_ || !generation_ || (n && !p)) return false;
        if (n > rxFree()) { stats_.rx_dropped_bytes += uint32_t(n); return false; }
        size_t at = (rx_read_ + rx_count_) % rx_size_;
        const size_t first = n < rx_size_ - at ? n : rx_size_ - at;
        if (first) memcpy(rx_ + at, p, first);
        if (n > first) memcpy(rx_, p + first, n - first);
        rx_count_ += n; stats_.rx_bytes += uint32_t(n); ++stats_.rx_frames; return true;
    }
    size_t txPending() const { return tx_count_; }
    size_t txPeek(uint8_t* out, size_t capacity) const {
        const size_t n = capacity < tx_count_ ? capacity : tx_count_;
        const size_t first = n < tx_size_ - tx_read_ ? n : tx_size_ - tx_read_;
        if (first) memcpy(out, tx_ + tx_read_, first);
        if (n > first) memcpy(out + first, tx_, n - first);
        return n;
    }
    void txConsume(size_t n) {
        if (n > tx_count_) n = tx_count_;
        tx_read_ = (tx_read_ + n) % tx_size_; tx_count_ -= n; stats_.tx_bytes += uint32_t(n); ++stats_.tx_frames;
    }
    // Application side (radio mutex held). Ok may accept fewer bytes.
    Status write(const uint8_t* data, size_t length, size_t& accepted) {
        accepted = 0;
        if (!active_) return Status::Unsupported;
        if (!data && length) return Status::InvalidArgument;
        if (!generation_) return Status::InvalidArgument;
        if (!length) return Status::Ok;
        const size_t space = tx_size_ - tx_count_;
        const size_t n = length < space ? length : space;
        stats_.tx_rejected_bytes += uint32_t(length - n);
        if (!n) return Status::OutputFull;
        const size_t at = (tx_read_ + tx_count_) % tx_size_;
        const size_t first = n < tx_size_ - at ? n : tx_size_ - at;
        memcpy(tx_ + at, data, first);
        if (n > first) memcpy(tx_, data + first, n - first);
        tx_count_ += n; accepted = n; return Status::Ok;
    }
    size_t read(uint8_t* out, size_t capacity) {
        if (!active_ || !out || !capacity || !rx_count_) return 0;
        const size_t n = capacity < rx_count_ ? capacity : rx_count_;
        const size_t first = n < rx_size_ - rx_read_ ? n : rx_size_ - rx_read_;
        memcpy(out, rx_ + rx_read_, first);
        if (n > first) memcpy(out + first, rx_, n - first);
        rx_read_ = (rx_read_ + n) % rx_size_; rx_count_ -= n; return n;
    }
    size_t available() const { return active_ ? rx_count_ : 0; }
    bool connected() const { return connected_.load(std::memory_order_acquire); }
    SppStats stats() const {
        SppStats s = stats_;
        s.enabled = active_; s.connected = generation_ != 0; s.credit_flow = credit_; s.generation = generation_;
        s.frame_bytes = frame_bytes_; s.peer_credits = peer_credits_;
        s.rx_available = uint32_t(rx_count_); s.tx_pending = uint32_t(tx_count_); return s;
    }
private:
    uint8_t* rx_ = nullptr; uint8_t* tx_ = nullptr;
    size_t rx_size_ = 0, tx_size_ = 0, rx_read_ = 0, rx_count_ = 0, tx_read_ = 0, tx_count_ = 0;
    uint8_t record_bytes_[RecordBytes] = {};
    bth_sdp_record_t record_ = {};
    SppStats stats_;
    uint32_t generation_ = 0;
    uint16_t frame_bytes_ = 0, peer_credits_ = 0;
    uint8_t channel_ = 0, interval_ = 0;
    bool credit_ = false, active_ = false;
    std::atomic<bool> connected_{false};
};
}}
