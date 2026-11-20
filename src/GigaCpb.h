#pragma once
#include <stddef.h>
#include <stdint.h>
namespace giga_bredr {
// Connectionless Peripheral Broadcast (CPB; Bluetooth 4.1 "Connectionless
// Slave Broadcast"). One GIGA transmits a small payload at a fixed interval to
// any number of synchronized receivers, with no connection, pairing or
// acknowledgement. The GIGA radio (CYW4343W) supports both roles.
//
// Default Off: nothing is allocated and no HCI command is sent. Requires the
// Classic actor (enable_a2dp=true); A2DP, BLE and the other profiles keep
// running, and the broadcast shares their airtime.
//
// Delivery is "latest value": the controller repeats the current payload at
// every CPB instant, receivers keep only the newest complete message, and a
// lost instant is not retransmitted. Payload bytes are public (unencrypted).
enum class CpbRole : uint8_t { Off, Transmitter, Receiver };
enum class CpbState : uint8_t {
    Off, Starting, Transmitting,
    Scanning,       // Receiver: listening for the transmitter's synchronization train.
    Synchronizing,  // Receiver: train found, enabling reception.
    Receiving,
    Waiting,        // Receiver: scan failed or sync lost; rescans after retry_ms.
    Stopping, Failed
};
// ACL packet-type bits accepted for CPB (basic rate only).
constexpr uint16_t CpbDm1 = 0x0008, CpbDh1 = 0x0010, CpbDm3 = 0x0400, CpbDh3 = 0x0800, CpbDm5 = 0x4000, CpbDh5 = 0x8000;
struct CpbConfig {
    CpbRole role = CpbRole::Off;
    bool start_at_begin = true;          // false: wait for cpbStart().
    // Payload storage, 1..1024 bytes (transmitter once, receiver twice).
    uint16_t max_payload_bytes = 64;
    uint16_t packet_types = CpbDm1 | CpbDh1;
    // Transmitter: the reserved LT_ADDR, 1..7. Receiver: 0 accepts any.
    uint8_t lt_addr = 1;
    // Times are in baseband slots of 0.625 ms.
    uint16_t supervision_timeout_slots = 0x2000;   // 5.12 s
    // ---- Transmitter ----
    uint16_t interval_min_slots = 0x50, interval_max_slots = 0xa0; // 50..100 ms, even
    uint16_t train_interval_min_slots = 0x80, train_interval_max_slots = 0x100;
    uint32_t train_timeout_slots = 0x2a000;        // ~107 s per train
    bool continuous_train = true;  // Restart the train so late receivers can join.
    uint8_t service_data = 0;      // Carried in the train; receivers see it in stats.
    bool lpo_allowed = false;
    // Bytes per Set CPB Data fragment; 0 = largest enabled packet payload.
    uint8_t fragment_bytes = 0;
    // ---- Receiver ----
    // Transmitter BD_ADDR as RadioStats::address prints it (most significant first).
    uint8_t transmitter_address[6] = {};
    uint16_t scan_timeout_slots = 0x4000, scan_window_slots = 0x100, scan_interval_slots = 0x200;
    uint8_t remote_timing_accuracy_ppm = 250;
    uint8_t skip = 0;              // CPB instants the receiver may sleep through.
    uint16_t retry_ms = 1000;      // Delay before rescanning after a failed scan.
};
struct CpbStats {
    CpbRole role = CpbRole::Off;
    CpbState state = CpbState::Off;
    uint8_t lt_addr = 0;
    uint16_t interval_slots = 0;        // Granted (transmitter) or synchronized (receiver).
    uint16_t train_interval_slots = 0;  // Transmitter.
    uint8_t service_data = 0;           // Receiver: from the last train.
    bool train_running = false;
    // Last failing HCI command and its status (0 when none failed).
    uint16_t last_opcode = 0;
    uint8_t last_status = 0;
    uint32_t commands = 0, command_errors = 0;
    uint32_t data_updates = 0, fragments_sent = 0, trains_started = 0, trains_completed = 0, train_errors = 0, channel_map_changes = 0;
    uint32_t scans = 0, scan_failures = 0, syncs = 0, sync_losses = 0, sync_rejects = 0;
    uint32_t packets = 0, packet_errors = 0, fragments_received = 0, messages = 0, messages_overwritten = 0, reassembly_errors = 0;
};
}
