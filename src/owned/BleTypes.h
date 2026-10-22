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
struct BleConfig {
    bool enabled = true;
    uint16_t maximum_mtu = 517;
    // HCI legacy advertising units: 0.625ms (80..160ms by default).
    uint16_t advertising_interval_min = 128;
    uint16_t advertising_interval_max = 256;
    BleSecurityPolicy security = BleSecurityPolicy::PublicUnencrypted;
};
struct BleWrite {
    uint32_t generation = 0;
    uint16_t length = 0;
    uint8_t data[BleValueCapacity] = {};
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
};
}
