#pragma once
#include "Types.h"
#include "GigaBle.h"
#include "AudioDiagnostics.h"
#include "AudioClock.h"
#include "I2sConfig.h"

namespace giga_bredr {
// Zero fields inherit globals, then negotiated-codec defaults. A resolved
// startup_packets=0 uses the real-PCM startup_buffer_ms watermark instead.
struct AudioBufferConfig {
    size_t maximum_packet_bytes = 0; // Nonzero: 64..65535.
    size_t maximum_access_unit_bytes = 0; // Nonzero: 64..65535.
    uint8_t queue_packets = 0; // Nonzero: 1..64.
    uint8_t startup_packets = 0; // Nonzero: 1..64, then clamped to selected queue.
    uint16_t pcm_buffer_ms = 0; // Nonzero: 2..320; frames scale with selected output rate.
    uint16_t startup_buffer_ms = 0; // Real PCM watermark; exclusive with startup_packets in this layer.
};
using CodecBufferConfig = AudioBufferConfig;
struct ReceiverConfig {
    const char* name = "GIGA Audio";
    uint32_t codecs = 0; // zero selects the compiled codec set
    size_t maximum_packet_bytes = 0; // Zero selects the advertised 2048-byte SDU ceiling.
    size_t maximum_access_unit_bytes = 0; // Zero selects 8192 bytes; allocated only when needed.
    uint8_t queue_packets = 0; // Zero selects a bounded codec/configuration capacity.
    uint8_t startup_packets = 0; // Nonzero selects the explicit legacy packet-startup path.
#if defined(GIGA_BREDR_I2S_BUFFER_MS)
    uint16_t pcm_buffer_ms = GIGA_BREDR_I2S_BUFFER_MS;
#else
    uint16_t pcm_buffer_ms = 0; // Zero sizes the ring for its real PCM startup reserve.
#endif
    CodecBufferConfig codec_buffers[static_cast<size_t>(Codec::Count)] = {}; // Index by Codec; data only until configured.
    bool external_buffers = false; // caller supplies/initializes external memory
    bool enable_i2s = true;
    AudioClockConfig clock;
    bool apply_remote_volume = true;
    BleConfig ble;
    size_t host_stack_bytes = 8192;
    size_t decoder_stack_bytes = 32768;
    void (*diagnostic)(const char* message, void* user) = nullptr;
    void (*configuration_changed)(const CodecConfig&, void* user) = nullptr;
    void (*volume_changed)(uint8_t absolute_volume_0_127, void* user) = nullptr;
    void (*metadata_changed)(uint8_t attribute_id, const char* text, size_t bytes, void* user) = nullptr;
    void* user = nullptr;
    I2sPins i2s; // Hardware-compatible output route; optional MCLK=-1.
    uint16_t startup_buffer_ms = 0; // Zero selects the negotiated-codec real-PCM reserve.
};
struct ReceiverStats {
    AudioClockStats clock;
    RadioStats radio;
    OutputStats output;
    MemoryStats memory;
    CodecConfig configuration;
    AudioBufferConfig buffers; // Selected profile only; no allocation for inactive overrides.
    Status last_error = Status::Ok;
    uint64_t decoded_frames = 0;
    uint32_t decoded_packets = 0, dropped_packets = 0, decode_errors = 0;
    uint32_t maximum_decode_us = 0, output_dropped_frames = 0, diagnostic_drops = 0;
    size_t decoder_memory_bytes = 0;
    uint8_t absolute_volume = 127;
    bool configured = false, playing = false;
    // Cumulative per begin(). A block is one successful encoded LHDC frame;
    // frames are PCM sample frames (one sample per channel), not samples.
    // Both categories can apply to one block and must not be added as a total.
    uint64_t lhdc_right_concealed_blocks = 0, lhdc_right_concealed_frames = 0;
    uint64_t lhdc_frame_muted_blocks = 0, lhdc_frame_muted_frames = 0;
};
class GigaBredr {
public:
    GigaBredr() = default;
    ~GigaBredr() { end(); }
    GigaBredr(const GigaBredr&) = delete;
    GigaBredr& operator=(const GigaBredr&) = delete;
    Status begin(const ReceiverConfig& config = ReceiverConfig());
    // Call from loop() to deliver bounded diagnostic/configuration callbacks.
    // Radio and decoder workers run independently on GIGA.
    // A callback may end/restart this receiver; remaining old callbacks are
    // discarded. Keep this object alive and serialize begin/end/poll calls.
    void poll();
    void end();
    ReceiverStats stats() const;
    AudioDiagnostics audioDiagnostics() const;
    BleStats bleStats() const;
    // Copies the readable TX value; notifies only while connected/subscribed.
    // Ok is admission, not remote delivery. Busy/OutputFull ask for a retry.
    // Foreground data calls do not wait for the radio owner; contention is Busy.
    Status bleNotify(const uint8_t* data, size_t length);
    // Same-begin connection-bound reply: stale/disconnected origins return
    // InvalidArgument without updating TX. Discard origins after end()/begin().
    Status bleNotify(const BleWrite& origin);
    // Same-begin generation from bleStats(): queues one LE-only HCI Disconnect.
    // Discard the scalar after end()/begin(), as with BleWrite origins. Ok is
    // admission, not physical departure; only a controller event clears gen.
    // false holds future advertising until resume; Busy retries change no hold.
    // Matching admitted duplicates are Ok without requeue/deadline extension;
    // different options or an automatic close in progress return Busy.
    Status bleDisconnect(uint32_t expected_generation,bool advertise_after_disconnect=true);
    // Idempotent release of an advertising hold. Busy while a held peer is
    // attached/disconnecting; actual advertising follows normal GAP/RX admission.
    Status bleResumeAdvertising();
    // Pops one copied write from the GATT RX characteristic, if available.
    // Returns false without changing value when empty or the radio owner is busy.
    bool bleReadWrite(BleWrite& value);
    static uint32_t availableCodecs();
    using Allocate = void* (*)(size_t bytes, void* user);
    using Free = void (*)(void* pointer, void* user);
    static bool setExternalAllocator(Allocate alloc, Free free_fn, void* user = nullptr);
private:
    struct Impl;
    Impl* impl_ = nullptr;
    uint64_t lifetime_ = 0; // Survives Impl replacement, including address reuse.
};
}
