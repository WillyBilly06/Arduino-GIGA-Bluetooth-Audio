#pragma once
#include "Types.h"
namespace giga_bredr {
// OBEX over RFCOMM (GOEP 1.1): an Object Push server, a Phonebook Access
// client and a Message Access client (MCE). Default disabled: no SDP record,
// EIR UUID, RFCOMM listener, storage or outgoing connection. Requires the Classic actor (enable_a2dp=true) and an
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
// MAP client (MCE) operations on the phone's Message Access Server (MAS).
enum class MapOperation : uint8_t { None = 0, GetFolderListing = 1, GetMessageListing = 2, GetMessage = 3 };
// Same stages as PbapState; the PBAP and MAP clients share one OBEX client link.
enum class MapState : uint8_t { Idle = 0, ServiceDiscovery, Transport, Connecting, Transferring, Closing };
static constexpr uint16_t MapNoListingSize = 0xffff;
static constexpr uint8_t MapNoNewMessage = 0xff;
struct MapResult {
    MapOperation operation = MapOperation::None;
    bool ok = false, aborted = false;
    uint8_t response = 0;       // Final OBEX response code (0xA0 success); 0 = transport/SDP failure.
    // FolderListingSize (folder listing) or MessagesListingSize (message
    // listing) application parameter; MapNoListingSize if not sent.
    uint16_t listing_size = MapNoListingSize;
    uint8_t new_message = MapNoNewMessage; // NewMessage parameter (1 = unread since last listing, 0 = none).
    char mse_time[24] = {};     // MSETime parameter (e.g. "20261120T093000+0100"), "" if not sent.
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
    // Message Access Profile client (MCE): folder listing, message listing and
    // message pull from the phone's MAS over RFCOMM. No Message Notification
    // Server: no SDP record or EIR UUID is added and no new-message push
    // arrives; poll with GigaBredr::mapGetMessageListing instead.
    bool map_enabled = false;
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
    // MAP: folder-listing XML, MAP-msg-listing XML or bMessage bytes in order,
    // then exactly one end.
    void (*on_map_data)(const uint8_t* data, size_t length, void* user) = nullptr;
    void (*on_map_end)(const MapResult& result, void* user) = nullptr;
    void* user = nullptr;
};
struct ObexStats {
    bool opp_enabled = false, pbap_enabled = false, map_enabled = false;
    bool opp_connected = false;  // OPP RFCOMM DLC open.
    uint32_t objects_completed = 0, objects_failed = 0, objects_rejected = 0;
    uint32_t object_bytes = 0;   // Body bytes received across all objects.
    PbapState pbap_state = PbapState::Idle;
    uint32_t pbap_completed = 0, pbap_failed = 0, pbap_bytes = 0;
    uint8_t pse_channel = 0;      // Cached phone PSE RFCOMM channel (0 = unknown).
    uint16_t pse_version = 0;     // PSE BluetoothProfileDescriptorList version.
    MapState map_state = MapState::Idle;
    uint32_t map_completed = 0, map_failed = 0, map_bytes = 0;
    uint8_t mas_channel = 0;      // Cached phone MAS RFCOMM channel (0 = unknown).
    uint16_t mas_version = 0;     // MAS BluetoothProfileDescriptorList version.
    uint8_t mas_instance_id = 0, mas_message_types = 0; // MASInstanceID / SupportedMessageTypes.
    uint32_t mas_features = 0;    // MapSupportedFeatures (0 if absent).
    uint16_t mas_l2cap_psm = 0;   // GOEP L2CAP PSM; reported only (RFCOMM is used).
    uint8_t last_response = 0;    // Last final OBEX response code observed by the PCE/MCE.
    uint32_t protocol_errors = 0; // Malformed/oversized OBEX packets (DLC closed).
    uint32_t event_bytes_pending = 0;
};
}
