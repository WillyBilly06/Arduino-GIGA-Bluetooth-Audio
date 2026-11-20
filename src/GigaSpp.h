#pragma once
#include "Types.h"
namespace giga_bredr {
// Bluetooth Classic Serial Port Profile server (one RFCOMM DLC at a time).
// Default disabled: no SDP record, no EIR UUID, no RFCOMM listener and no
// storage. Requires the Classic actor (enable_a2dp=true) and an encrypted link.
struct SppConfig {
    bool enabled = false;
    const char* service_name = "GIGA Serial"; // SDP ServiceName, 1..32 bytes.
    // RFCOMM server channel 1..30. The default avoids the HFP (1) and HSP (2)
    // defaults; it must differ from any enabled voice channel.
    uint8_t channel = 3;
    // Fixed rings, 512..16384 bytes each, allocated only by an enabled begin().
    uint16_t rx_buffer_bytes = 2048, tx_buffer_bytes = 2048;
    // Minimum spacing between SPP TX frames while A2DP media streams (0 = no
    // spacing). Outside streaming SPP still sends at most one frame per host turn.
    uint8_t streaming_frame_interval_ms = 4;
};
struct SppStats {
    bool enabled = false, connected = false, credit_flow = false;
    uint32_t generation = 0;   // Nonzero per opened DLC; changes on reconnect.
    uint32_t connections = 0;  // DLCs opened during this begin().
    uint16_t frame_bytes = 0;  // Negotiated RFCOMM N1 (maximum payload per frame).
    uint16_t peer_credits = 0; // TX credits currently granted by the peer.
    uint32_t rx_frames = 0, tx_frames = 0;
    uint32_t rx_bytes = 0;          // Bytes stored into the RX ring.
    uint32_t tx_bytes = 0;          // Bytes framed for the peer.
    uint32_t rx_dropped_bytes = 0;  // Ring overflow (legacy peers) or unread at reconnect.
    uint32_t tx_dropped_bytes = 0;  // Unsent TX bytes discarded at disconnect.
    uint32_t tx_rejected_bytes = 0; // sppWrite() bytes not accepted (ring full).
    uint32_t rx_available = 0, tx_pending = 0;
};
}
