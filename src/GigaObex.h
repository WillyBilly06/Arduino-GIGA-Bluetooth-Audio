#pragma once
#include "Types.h"
namespace giga_bredr {
// OBEX over RFCOMM (GOEP 1.1): an Object Push server and a Phonebook Access
// client. Default disabled: no SDP record, EIR UUID, RFCOMM listener, storage
// or outgoing connection. Requires the Classic actor (enable_a2dp=true) and an
// encrypted link. Shares the one RFCOMM mux (HFP/HSP/SPP) and the Classic
// control FIFO rules of SPP: a data frame never queues ahead of signaling,
// AVDTP or AVRCP replies, and is paced while A2DP media streams.
// All callbacks run from GigaBredr::poll() (loop()), never on the radio thread;
// data pointers are valid only during the call. A slow loop() applies OBEX
// backpressure (the peer waits for the next response); nothing is dropped.
static constexpr uint32_t ObexUnknownLength = 0xffffffffu;
enum class PbapOperation : uint8_t { None = 0, PullPhonebook = 1, PullVcardListing = 2 };
enum class PbapState : uint8_t { Idle = 0, ServiceDiscovery, Transport, Connecting, Transferring, Closing };
struct PbapResult {
    PbapOperation operation = PbapOperation::None;
    bool ok = false, aborted = false;
    uint8_t response = 0;       // Final OBEX response code (0xA0 success); 0 = transport/SDP failure.
    uint16_t phonebook_size = 0xffff; // PhonebookSize application parameter, 0xFFFF if not sent.
    uint32_t bytes = 0;         // Body bytes delivered for this operation.
};
struct ObexConfig {
    // Object Push Profile server (receive files/vCards from a phone or PC).
    bool opp_enabled = false;
    const char* opp_service_name = "GIGA Object Push"; // SDP ServiceName, 1..32 bytes.
    uint8_t opp_channel = 4; // RFCOMM 1..30; must differ from enabled HFP(1)/HSP(2)/SPP(3) channels.
    // Phonebook Access Profile client (PCE). Advertises a PCE record (0x112E),
    // which some phones require before offering "contact sharing".
    bool pbap_enabled = false;
    const char* pbap_service_name = "GIGA Phonebook";
    // Fixed actor->loop() event ring, 2048..16384 bytes, allocated only when enabled.
    uint16_t event_buffer_bytes = 4096;
    // Minimum spacing between OBEX data frames while A2DP media streams (0 = none).
    uint8_t streaming_frame_interval_ms = 4;
    // OPP: one object at a time. length is the Length header or ObexUnknownLength.
    // name is UTF-8 (truncated at 95 bytes), type the Type header ("" if absent).
    void (*on_object_begin)(const char* name, const char* type, uint32_t length, void* user) = nullptr;
    void (*on_object_data)(const uint8_t* data, size_t length, void* user) = nullptr;
    // ok=false: aborted, disconnected or malformed before the final packet.
    void (*on_object_end)(bool ok, void* user) = nullptr;
    // PBAP: vCard (or vCard-listing XML) bytes in order, then exactly one end.
    void (*on_phonebook_data)(const uint8_t* data, size_t length, void* user) = nullptr;
    void (*on_phonebook_end)(const PbapResult& result, void* user) = nullptr;
    void* user = nullptr;
};
struct ObexStats {
    bool opp_enabled = false, pbap_enabled = false;
    bool opp_connected = false;  // OPP RFCOMM DLC open.
    uint32_t objects_completed = 0, objects_failed = 0, objects_rejected = 0;
    uint32_t object_bytes = 0;   // Body bytes received across all objects.
    PbapState pbap_state = PbapState::Idle;
    uint32_t pbap_completed = 0, pbap_failed = 0, pbap_bytes = 0;
    uint8_t pse_channel = 0;      // Cached phone PSE RFCOMM channel (0 = unknown).
    uint16_t pse_version = 0;     // PSE BluetoothProfileDescriptorList version.
    uint8_t last_response = 0;    // Last final OBEX response code observed by the PCE.
    uint32_t protocol_errors = 0; // Malformed/oversized OBEX packets (DLC closed).
    uint32_t event_bytes_pending = 0;
};
}
