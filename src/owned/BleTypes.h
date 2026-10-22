#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../Types.h"

namespace giga_bredr {
// Attribute values are capped independently of the negotiated ATT PDU size.
constexpr size_t BleValueCapacity = 512;
// The initial service is public. Pairing/encryption needs a qualified backend.
enum class BleSecurityPolicy : uint8_t {
    PublicUnencrypted,
    // Reserved secure mode fails with Unsupported until its backend is qualified.
    EncryptedPairing
};
struct BleConnectionPreference {
    bool enabled = true;
    // Connection interval units1.25ms; supervision timeout units10ms.
    uint16_t interval_min = 64, interval_max = 96, latency = 2, timeout = 500;
};
struct BleConfig {
    bool enabled = true;
    uint16_t maximum_mtu = 517;
    // HCI legacy advertising units: 0.625ms (80..160ms by default).
    uint16_t advertising_interval_min = 128;
    uint16_t advertising_interval_max = 256;
    BleSecurityPolicy security = BleSecurityPolicy::PublicUnencrypted;
    // During secure Classic audio, prefer Classic work and pace ATT data.
    // These host limits do not reserve controller radio airtime.
    bool audio_priority = true;
    uint16_t audio_data_interval_ms = 10; // 1..100ms between ATT ACL fragments.
    uint16_t audio_acl_fragment_bytes = 27; // 4..251 HCI payload bytes.
    // One optional preference chain per connection after audio starts and the
    // connection is at least5s old. This does not impose a controller RF floor.
    BleConnectionPreference audio_connection_preference;
};
struct BleWrite {
    uint32_t generation = 0;
    uint16_t length = 0;
    uint8_t data[BleValueCapacity] = {};
};
struct BleConnectionStats {
    uint16_t interval = 0, latency = 0, timeout = 0;
    uint8_t source = 0, update_status = 0xff; // Actual01/0a/03; never a request.
    bool known = false, matches_preference = false, update_reported = false, preference_started = false;
    uint8_t preference_stage = 0, preference_error = 0, command_status = 0xff;
    // Last2013/2020 tuple accepted by the actor send callback, not applied RF.
    uint16_t submitted_min = 0, submitted_max = 0, submitted_latency = 0, submitted_timeout = 0;
    // These counts are cumulative until begin(); tuple/status metadata above is
    // reset for each connection generation, including same-handle reconnects.
    uint32_t feature_submissions = 0, update_submissions = 0, reply_submissions = 0;
    uint32_t signaling_requests_queued = 0, command_status_reports = 0;
};
struct BleStats {
    uint32_t generation = 0;
    uint32_t received_pdus = 0, malformed_pdus = 0, writes = 0;
    uint32_t notifications_queued = 0, capacity_retries = 0;
    uint16_t connection_handle = 0, att_mtu = 23;
    bool enabled = false, advertising = false, connected = false;
    bool notify_enabled = false, security_available = false, encrypted = false;
    Status status = Status::Ok;
    // Valid RX Write Commands dropped for an unread app value or an older
    // capacity-blocked ATT request (oldest request has bounded app priority).
    // Cumulative for this host lifetime; acknowledged requests are never counted.
    uint32_t write_command_drops = 0;
    // Cumulative actor submissions during audio priority, not RF deliveries.
    uint32_t qos_data_fragments = 0, qos_data_bytes = 0;
    uint32_t qos_paced_deferrals = 0, qos_urgent_bypasses = 0;
    BleConnectionStats connection;
};
}
