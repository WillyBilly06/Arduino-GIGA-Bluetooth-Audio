#pragma once
#include "../GigaCpb.h"
#include "../Types.h"
#include <cstring>
extern "C" {
#include "core/bth_cpb.h"
}
namespace giga_bredr { namespace detail {
// Connectionless Peripheral Broadcast endpoint. Every method runs under the
// radio owner's serialization (the actor, or a foreground call holding the
// radio mutex). Storage is caller-owned (storageBytes()).
class CpbPort {
public:
    static constexpr uint16_t MaximumPayload = 1024;
    static void toTx(const CpbConfig& c, bth_cpb_tx_config_t& t) {
        bth_cpb_tx_defaults(&t);
        t.lt_addr = c.lt_addr; t.lpo_allowed = c.lpo_allowed; t.packet_types = c.packet_types;
        t.interval_min = c.interval_min_slots; t.interval_max = c.interval_max_slots;
        t.supervision_timeout = c.supervision_timeout_slots;
        t.train_interval_min = c.train_interval_min_slots; t.train_interval_max = c.train_interval_max_slots;
        t.train_timeout = c.train_timeout_slots; t.service_data = c.service_data;
        t.continuous_train = c.continuous_train; t.fragment_bytes = c.fragment_bytes;
    }
    static void toRx(const CpbConfig& c, bth_cpb_rx_config_t& r) {
        bth_cpb_rx_defaults(&r);
        for (unsigned i = 0; i < 6; ++i) r.transmitter[i] = c.transmitter_address[5 - i];
        r.lt_addr = c.lt_addr; r.packet_types = c.packet_types; r.supervision_timeout = c.supervision_timeout_slots;
        r.scan_timeout = c.scan_timeout_slots; r.scan_window = c.scan_window_slots; r.scan_interval = c.scan_interval_slots;
        r.remote_timing_accuracy = c.remote_timing_accuracy_ppm; r.skip = c.skip; r.retry_ms = c.retry_ms;
    }
    static bool validate(const CpbConfig& c) {
        if (c.role == CpbRole::Off) return true;
        if (!c.max_payload_bytes || c.max_payload_bytes > MaximumPayload) return false;
        if (c.role == CpbRole::Transmitter) { bth_cpb_tx_config_t t; toTx(c, t); return bth_cpb_tx_valid(&t); }
        if (c.role != CpbRole::Receiver) return false;
        bool any = false; for (uint8_t b : c.transmitter_address) any |= b != 0;
        bth_cpb_rx_config_t r; toRx(c, r);
        return any && bth_cpb_rx_valid(&r);
    }
    static size_t storageBytes(const CpbConfig& c) {
        return c.role == CpbRole::Transmitter ? c.max_payload_bytes : c.role == CpbRole::Receiver ? 2u * c.max_payload_bytes : 0u;
    }
    bool begin(const CpbConfig& c, uint8_t* storage) {
        if (!validate(c) || c.role == CpbRole::Off || !storage) return false;
        config_ = c; wanted_ = c.start_at_begin;
        const size_t n = c.max_payload_bytes;
        const bool ok = c.role == CpbRole::Transmitter ? bth_cpb_init(&cpb_, 0, storage, n, nullptr, nullptr, 0)
                                                       : bth_cpb_init(&cpb_, 0, nullptr, 0, storage, storage + n, n);
        return ok;
    }
    void end() { wanted_ = false; std::memset(&cpb_, 0, sizeof cpb_); }
    // Actor: every OwnedHost::begin follows an HCI Reset.
    void hostStarted() { bth_cpb_controller_reset(&cpb_); }
    // Actor: at most one command intent per call while the host is ready.
    bool next(uint32_t now, uint16_t manufacturer, bth_cpb_command_t& out) {
        if (!cpb_.initialized) return false;
        cpb_.manufacturer = manufacturer;
        if (wanted_ && cpb_.state == BTH_CPB_OFF && !startRole()) wanted_ = false;
        return bth_cpb_next(&cpb_, now, &out) == BTH_OK;
    }
    void event(const bth_h4_frame_t& e, uint32_t now) { (void)bth_cpb_on_event(&cpb_, &e, now); }
    // Foreground (radio mutex held).
    Status start() {
        if (!cpb_.initialized) return Status::Unsupported;
        wanted_ = true;
        // Failed or running: release controller resources first; next() restarts from Off.
        if (cpb_.state == BTH_CPB_FAILED) bth_cpb_stop(&cpb_);
        return Status::Ok;
    }
    Status stop() {
        if (!cpb_.initialized) return Status::Unsupported;
        wanted_ = false; bth_cpb_stop(&cpb_); return Status::Ok;
    }
    Status setData(const uint8_t* data, size_t length) {
        if (!cpb_.initialized || config_.role != CpbRole::Transmitter) return Status::Unsupported;
        if ((!data && length) || length > config_.max_payload_bytes) return Status::InvalidArgument;
        const auto result = bth_cpb_set_data(&cpb_, data, length);
        return result == BTH_OK ? Status::Ok : result == BTH_ERR_STATE ? Status::Busy : Status::InvalidArgument;
    }
    bool read(uint8_t* data, size_t capacity, size_t& length, uint32_t* clock) {
        length = 0;
        if (!cpb_.initialized || config_.role != CpbRole::Receiver) return false;
        uint32_t c = 0, generation = 0;
        if (!bth_cpb_take(&cpb_, data, capacity, &length, &c, &generation)) return false;
        if (clock) *clock = c;
        return true;
    }
    CpbStats stats() const {
        CpbStats s;
        s.role = config_.role;
        switch (cpb_.state) {
        case BTH_CPB_OFF: s.state = CpbState::Off; break;
        case BTH_CPB_TX_SETUP: case BTH_CPB_RX_SETUP: s.state = CpbState::Starting; break;
        case BTH_CPB_TX_ACTIVE: s.state = CpbState::Transmitting; break;
        case BTH_CPB_RX_SCANNING: s.state = CpbState::Scanning; break;
        case BTH_CPB_RX_SYNCING: s.state = CpbState::Synchronizing; break;
        case BTH_CPB_RX_ACTIVE: s.state = CpbState::Receiving; break;
        case BTH_CPB_RX_RETRY_WAIT: s.state = CpbState::Waiting; break;
        case BTH_CPB_STOPPING: s.state = CpbState::Stopping; break;
        default: s.state = CpbState::Failed; break;
        }
        if (config_.role == CpbRole::Transmitter) { s.lt_addr = cpb_.tx.lt_addr; s.interval_slots = cpb_.granted_interval; }
        else { s.lt_addr = cpb_.sync.lt_addr; s.interval_slots = cpb_.sync.interval; s.service_data = cpb_.sync.service_data; }
        s.train_interval_slots = cpb_.train_interval; s.train_running = cpb_.train_running;
        s.last_opcode = cpb_.last_opcode; s.last_status = cpb_.last_status;
        const auto& k = cpb_.counters;
        s.commands = k.commands; s.command_errors = k.command_errors;
        s.data_updates = k.data_updates; s.fragments_sent = k.fragments_sent; s.trains_started = k.trains_started;
        s.trains_completed = k.trains_completed; s.train_errors = k.train_errors; s.channel_map_changes = k.channel_map_changes;
        s.scans = k.scans; s.scan_failures = k.scan_failures; s.syncs = k.syncs; s.sync_losses = k.sync_losses; s.sync_rejects = k.sync_rejects;
        s.packets = k.packets; s.packet_errors = k.packet_errors; s.fragments_received = k.fragments_received;
        s.messages = k.messages; s.messages_overwritten = k.messages_overwritten; s.reassembly_errors = k.reassembly_errors;
        return s;
    }
private:
    bool startRole() {
        if (config_.role == CpbRole::Transmitter) { bth_cpb_tx_config_t t; toTx(config_, t); return bth_cpb_start_transmit(&cpb_, &t) == BTH_OK; }
        bth_cpb_rx_config_t r; toRx(config_, r); return bth_cpb_start_receive(&cpb_, &r) == BTH_OK;
    }
    CpbConfig config_;
    bth_cpb_t cpb_{};
    bool wanted_ = false;
};
}}
