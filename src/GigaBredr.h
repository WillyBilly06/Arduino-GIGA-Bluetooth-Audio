#pragma once
#include "Types.h"
#include "GigaBredrCodecs.h"
#include "GigaBle.h"
#include "GigaAvrcp.h"
#include "AudioDiagnostics.h"
#include "AudioClock.h"
#include "I2sConfig.h"
#include "GigaVoice.h"
#include "GigaSpp.h"
#include "GigaHid.h"
#include "GigaCpb.h"
#include "GigaObex.h"
#include "BondStorage.h"

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
    // Opt-in: place the I2S DMA/PCM ring in D2 SRAM 0x30000000..0x3003FFFF
    // (256 KB the GIGA M7 linker map leaves unused) instead of the heap, stored
    // as packed 24-bit samples (6 bytes per frame; ~224 ms at 192 kHz). Only
    // set this when no M4 image, sketch or other library uses that SRAM. The
    // automatic BLE coexistence reserve is then capped to fit the window.
    bool pcm_ring_d2_sram = false;
    // Fill RTP sequence gaps of up to 64 packets / 400 ms with a faded replay
    // of the last real PCM, exactly the lost duration, so a source that drops
    // packets does not drain the PCM reserve into long underrun holds. Not
    // applied to AAC (an access unit can span packets).
    bool conceal_lost_packets = true;
    uint16_t startup_buffer_ms = 0; // Zero selects the negotiated-codec real-PCM reserve.
    // false selects real BLE-only operation: no Classic admission/audio worker.
    // At least enable_a2dp or ble.enabled must be true. Audio options are unused
    // in BLE-only mode; enable_i2s=false alone still leaves Classic enabled.
    bool enable_a2dp = true;
    VoiceConfig voice; // Experimental, default disabled; requires Classic+I2S.
    ClassicBondStorage classic_bonds; // Optional application-owned store; default RAM-only.
    SppConfig spp; // Default disabled Serial Port Profile server; requires enable_a2dp.
    HidConfig hid; // Default disabled HID Device (keyboard/mouse/gamepad); requires enable_a2dp.
    CpbConfig cpb; // Default Off Connectionless Peripheral Broadcast; requires enable_a2dp.
    ObexConfig obex; // Default disabled OBEX Object Push server / PBAP client / MAP client; requires enable_a2dp.
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
    // Streams opened with the base reserve because the BLE coexistence margin's
    // wider PCM ring could not be allocated (cumulative per begin()).
    uint32_t coexistence_reserve_fallbacks = 0;
    uint8_t absolute_volume = 127;
    bool configured = false, playing = false;
    // Cumulative per begin(). A block is one successful encoded LHDC frame;
    // frames are PCM sample frames (one sample per channel), not samples.
    // Both categories can apply to one block and must not be added as a total.
    uint64_t lhdc_right_concealed_blocks = 0, lhdc_right_concealed_frames = 0;
    uint64_t lhdc_frame_muted_blocks = 0, lhdc_frame_muted_frames = 0;
    // RTP sequence gaps filled by conceal_lost_packets and the PCM frames written.
    uint64_t concealed_gaps = 0, concealed_frames = 0;
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
    // Copied current AVRCP session state. Metadata/status are received facts;
    // song_position_ms is not extrapolated. Snapshots serialize with the radio.
    AvrcpStatus avrcpStatus() const;
    AvrcpMetadata avrcpMetadata() const;
    // Nonblocking foreground admission of ONE typed tap. Ok copies intent,
    // not phone execution. Poll the matching command_generation/command_id.
    // Both press/release must receive ACCEPTED for Completed; no automatic retry.
    // Discard generations/IDs after end()/begin(). Busy leaves request_id intact.
    Status avrcpCommand(AvrcpCommand command,uint32_t expected_generation,uint32_t& request_id);
    // Before queue commitment: cancel without sending. After commitment: both
    // transactions still retire (release is retained); this cannot unsend a key.
    // A matching repeated cancellation is idempotent. Stale sessions reject.
    Status avrcpCancel(uint32_t expected_generation,uint32_t request_id);
    // Nonblocking registration. Ok copies callbacks; only the existing audio
    // worker invokes them, for the actually selected CVSD/mSBC PCM format.
    // None is the default and sends silence. Provider callbacks must be bounded,
    // allocation/logging-free and must not call receiver lifecycle operations.
    Status setVoiceInput(VoicePcmProvider,VoiceInputToken&);
    // Ok fences all old callbacks before return. Busy changes nothing: retry
    // before destroying the provider context. Captured/queued audio is retained.
    // Tokens are specific to this receiver's begin/end lifetime.
    Status detachVoiceInput(VoiceInputToken);
    Status voiceStatus(VoiceStatus&)const;
    // Copies one explicit typed HF intent; Ok is admission, not phone execution.
    Status voiceCommand(HfpCommand,uint32_t expected_generation,const char* argument=nullptr);
    // Explicit HSP button intent using a copied voiceStatus().headset_command_token.
    // AG/state dependent: an idle button can redial/place a call. Never auto-retry.
    // Ok is one admitted intent, not execution; no per-request unsend is promised.
    // HF state cannot authorize HS. End/restart/another receiver rejects old tokens.
    Status headsetCommand(HspCommand,HspCommandToken expected);
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
    // Serial Port Profile (config.spp.enabled). Foreground data calls never wait
    // for the radio owner: contention is Busy (or 0 bytes); retry from loop().
    // Ok copies up to length bytes into the fixed TX ring; accepted may be less
    // (partial write). OutputFull: ring full, nothing copied. InvalidArgument:
    // no open SPP connection. Ok is admission, not remote delivery: frames go
    // out as RFCOMM credits and the peer N1 allow, behind Classic control.
    Status sppWrite(const uint8_t* data,size_t length,size_t& accepted);
    // Drains up to capacity received bytes; freed space returns RFCOMM credits.
    // Bytes received before a disconnect stay readable until the next connect.
    size_t sppRead(uint8_t* data,size_t capacity);
    bool sppConnected() const;
    // Copied counters; Unsupported unless SPP is enabled for this begin().
    Status sppStats(SppStats& value) const;
    // HID Device (config.hid.enabled). hidSend copies one input report (the
    // descriptor's exact length; report_id 0 without report IDs) into a fixed
    // 8-report queue without waiting for the radio owner: Busy on contention,
    // OutputFull when the queue is full, InvalidArgument when no host is
    // connected or the ID/length is not an input report. Ok is admission, not
    // delivery; reports go out behind Classic control. Output reports (LEDs)
    // reach config.hid.output_report from poll().
    Status hidSend(uint8_t report_id,const uint8_t* data,size_t length);
    bool hidConnected() const;
    Status hidStats(HidStats& value) const;
    // Connectionless Peripheral Broadcast (config.cpb.role). Foreground calls
    // never wait for the radio owner: contention is Busy (or false).
    // Transmitter: copies the payload the controller repeats at every CPB
    // instant (at most config.cpb.max_payload_bytes). Ok is admission; the new
    // payload replaces the old one after the next host turn. Busy while stopping.
    Status cpbSetData(const uint8_t* data,size_t length);
    // Receiver: true copies the newest complete message not read yet (length is
    // its full size; the copy is truncated to capacity). clock is the CPB clock
    // of its last fragment. Older unread messages are dropped and counted in
    // CpbStats::messages_overwritten.
    bool cpbRead(uint8_t* data,size_t capacity,size_t& length,uint32_t* clock=nullptr);
    // Start (also after Failed: resources are released first) or stop the
    // configured role. Ok is admission; follow cpbStats().state.
    Status cpbStart();
    Status cpbStop();
    Status cpbStats(CpbStats& value) const;
    // Phonebook Access client (config.obex.pbap_enabled). Nonblocking admission
    // of ONE operation on the connected phone: SDP lookup of its PSE, RFCOMM
    // DLC, OBEX CONNECT/GET/DISCONNECT. Ok is admission only; vCard bytes and
    // exactly one PbapResult arrive through the ObexConfig callbacks from poll().
    // Busy: radio owner busy or an operation still active. max_count 0xFFFF =
    // no limit (0 returns only phonebook_size). Phones may prompt their user.
    Status pbapPullPhonebook(const char* path="telecom/pb.vcf",uint16_t max_count=0xffff,uint16_t offset=0);
    // vCard-listing XML of a folder (SETPATH per segment, then GET).
    Status pbapPullVcardListing(const char* folder="telecom/pb",uint16_t max_count=0xffff,uint16_t offset=0);
    // Ok: an OBEX ABORT (or teardown before OBEX) follows; on_phonebook_end
    // still arrives with aborted=true. InvalidArgument when nothing is active.
    Status pbapAbort();
    // Message Access client (config.obex.map_enabled), MCE role. Same admission
    // rules as PBAP: ONE operation on the connected phone's MAS (SDP lookup,
    // RFCOMM DLC, OBEX CONNECT, SETPATH per folder segment from the MAS root,
    // GET, DISCONNECT); Ok is admission only; bytes and exactly one MapResult
    // arrive through on_map_data/on_map_end from poll(). Busy: radio owner busy
    // or a PBAP/MAP operation still active (both share one OBEX client link).
    // max_count is MaxListCount (0 returns only listing_size; 1024 is the MAP
    // default). No notification server: new messages are found by listing again.
    // x-obex/folder-listing XML of a folder ("" = MAS root).
    Status mapGetFolderListing(const char* folder="telecom/msg",uint16_t max_count=1024,uint16_t offset=0);
    // x-bt/MAP-msg-listing XML of a folder. subject_length 1..255 asks the phone
    // to truncate subjects (SubjectLength); 0 omits the parameter.
    Status mapGetMessageListing(const char* folder="telecom/msg/inbox",uint16_t max_count=1024,uint16_t offset=0,uint8_t subject_length=0);
    // x-bt/message (bMessage) by handle from a listing: 1..16 hex digits,
    // attachments off, UTF-8 charset. InvalidArgument for any other handle.
    Status mapGetMessage(const char* handle);
    // Ok: an OBEX ABORT (or teardown before OBEX) follows; on_map_end still
    // arrives with aborted=true. InvalidArgument when no MAP operation is active.
    Status mapAbort();
    // Copied counters; Unsupported unless OPP, PBAP or MAP is enabled for this begin().
    Status obexStats(ObexStats& value) const;
    static uint32_t availableCodecs();
    using Allocate = void* (*)(size_t bytes, void* user);
    using Free = void (*)(void* pointer, void* user);
    static bool setExternalAllocator(Allocate alloc, Free free_fn, void* user = nullptr);
private:
    struct Impl;
    Impl* impl_ = nullptr;
    uint64_t lifetime_ = 0; // Per-object callback invalidation; not voice-token identity.
};
}
