#ifndef GIGA_BREDR_BTH_CPB_H
#define GIGA_BREDR_BTH_CPB_H
/* Connectionless Peripheral Broadcast (CPB, formerly Connectionless Slave
 * Broadcast, Bluetooth 4.1+) host procedures. Pure state machine: it emits
 * one HCI command intent at a time and observes HCI events. No I/O.
 *
 * Transmitter: Set Event Mask Page 2 -> Set Reserved LT_ADDR -> Write
 * Synchronization Train Parameters -> Set CPB (enable) -> Set CPB Data
 * fragments -> Start Synchronization Train. Stop: Set CPB (disable) ->
 * Delete Reserved LT_ADDR.
 * Receiver: Set Event Mask Page 2 -> Receive Synchronization Train ->
 * (Synchronization Train Received event) -> Set CPB Receive (enable).
 * A CPB Timeout event re-enters Receive Synchronization Train. */
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
    BTH_CPB_COMMAND_LIMIT = 255,
    /* Set CPB Data carries LT_ADDR, Fragment and Data_Length before data. */
    BTH_CPB_FRAGMENT_LIMIT = 252,
    /* ACL packet-type bits usable for CPB: DM1 DH1 DM3 DH3 DM5 DH5. */
    BTH_CPB_PACKET_TYPES = 0xcc18
};
typedef enum {
    BTH_CPB_OFF = 0,
    BTH_CPB_TX_SETUP,
    BTH_CPB_TX_ACTIVE,
    BTH_CPB_RX_SETUP,
    BTH_CPB_RX_SCANNING,   /* Receive Synchronization Train in progress. */
    BTH_CPB_RX_SYNCING,    /* Set CPB Receive pending. */
    BTH_CPB_RX_ACTIVE,
    BTH_CPB_RX_RETRY_WAIT,
    BTH_CPB_STOPPING,
    BTH_CPB_FAILED
} bth_cpb_state_t;
typedef enum { BTH_CPB_ROLE_NONE = 0, BTH_CPB_ROLE_TRANSMITTER, BTH_CPB_ROLE_RECEIVER } bth_cpb_role_t;
/* Synchronization Train Received parameter order. Broadcom controllers
 * (manufacturer 15) report Service_Data before LT_ADDR (BlueZ tools/3dsp.c). */
typedef enum { BTH_CPB_LAYOUT_AUTO = 0, BTH_CPB_LAYOUT_STANDARD, BTH_CPB_LAYOUT_BROADCOM } bth_cpb_layout_t;
typedef struct {
    uint8_t lt_addr;                 /* 1..7 */
    bool lpo_allowed;
    uint16_t packet_types;           /* Subset of BTH_CPB_PACKET_TYPES, nonzero. */
    uint16_t interval_min, interval_max;      /* Slots (0.625 ms), even, 2..0xFFFE. */
    uint16_t supervision_timeout;    /* Slots, 2..0xFFFE. */
    uint16_t train_interval_min, train_interval_max; /* Slots, 0x20..0xFFFE. */
    uint32_t train_timeout;          /* Slots, 2..0x07FFFFFE. */
    uint8_t service_data;
    bool continuous_train;           /* Restart the train each time it completes. */
    uint8_t fragment_bytes;          /* 0: largest enabled packet payload (<=252). */
} bth_cpb_tx_config_t;
typedef struct {
    uint8_t transmitter[6];          /* HCI byte order (least significant first). */
    uint8_t lt_addr;                 /* 0 accepts the LT_ADDR the train reports. */
    uint16_t scan_timeout, scan_window, scan_interval; /* Slots: >=0x22, >=0x11, >=2. */
    uint16_t supervision_timeout;    /* Slots, 2..0xFFFE. */
    uint8_t remote_timing_accuracy;  /* ppm */
    uint8_t skip;                    /* CPB instants the receiver may skip. */
    uint16_t packet_types;           /* Subset of BTH_CPB_PACKET_TYPES, nonzero. */
    uint16_t retry_ms;               /* Wait after a failed scan before rescanning. */
    bth_cpb_layout_t layout;
} bth_cpb_rx_config_t;
typedef struct {
    uint16_t opcode;
    uint8_t length;
    uint8_t params[BTH_CPB_COMMAND_LIMIT];
} bth_cpb_command_t;
typedef struct {
    uint32_t commands, command_errors;
    /* Transmitter */
    uint32_t data_updates, fragments_sent, trains_started, trains_completed, train_errors, channel_map_changes;
    /* Receiver */
    uint32_t scans, scan_failures, syncs, sync_losses, sync_rejects;
    uint32_t packets, packet_errors, fragments_received, messages, messages_overwritten, reassembly_errors;
} bth_cpb_counters_t;
typedef struct {
    uint8_t transmitter[6], lt_addr, service_data, map[10];
    uint16_t interval;
    uint32_t clock_offset, next_instant;
} bth_cpb_sync_t;
typedef struct {
    bth_cpb_state_t state;
    bth_cpb_role_t role;
    uint16_t manufacturer;
    bth_cpb_tx_config_t tx;
    bth_cpb_rx_config_t rx;
    bool initialized, pending, pending_enable, stop_requested;
    uint16_t pending_opcode;
    bool mask_written, reserved, cpb_enabled, train_params, train_started, train_running, train_restart;
    bool rx_enabled, scan_active, tx_dirty, tx_restart, tx_loaded;
    uint8_t chunk;
    uint16_t granted_interval, train_interval;
    uint8_t last_status;
    uint16_t last_opcode;
    uint32_t deadline_ms;
    /* Caller-owned storage (lengths fixed by init). */
    uint8_t *tx_data; size_t tx_capacity, tx_length, tx_sent;
    uint8_t *rx_work; size_t rx_capacity, rx_fill; bool rx_assembling;
    uint8_t *rx_ready; size_t rx_ready_length; uint32_t rx_ready_clock; bool rx_unread;
    uint32_t message_generation;
    bth_cpb_sync_t sync;
    bth_cpb_counters_t counters;
} bth_cpb_t;

/* Storage pointers may be null with zero capacity for a role that does not
 * use them. Receiver storage is two equal buffers: assembly and ready. */
bool bth_cpb_init(bth_cpb_t *, uint16_t manufacturer, uint8_t *tx, size_t tx_capacity,
                  uint8_t *rx_work, uint8_t *rx_ready, size_t rx_capacity);
/* Default parameters used by the public API (documented there). */
void bth_cpb_tx_defaults(bth_cpb_tx_config_t *);
void bth_cpb_rx_defaults(bth_cpb_rx_config_t *);
bool bth_cpb_tx_valid(const bth_cpb_tx_config_t *);
bool bth_cpb_rx_valid(const bth_cpb_rx_config_t *);
/* Largest single-packet payload of the enabled packet types, 0 if none. */
unsigned bth_cpb_packet_payload(uint16_t packet_types);
/* Start from OFF only. BTH_ERR_STATE otherwise; BTH_ERR_PARAM for invalid config. */
bth_status_t bth_cpb_start_transmit(bth_cpb_t *, const bth_cpb_tx_config_t *);
bth_status_t bth_cpb_start_receive(bth_cpb_t *, const bth_cpb_rx_config_t *);
/* Replace the broadcast payload (copied). Valid while transmitting or setting
 * up; the controller repeats the latest payload at each CPB instant. */
bth_status_t bth_cpb_set_data(bth_cpb_t *, const uint8_t *data, size_t length);
/* Release controller resources and return to OFF (idempotent). */
void bth_cpb_stop(bth_cpb_t *);
/* BTH_OK fills one command and marks it pending; BTH_IGNORED when idle or
 * waiting. The caller must submit the command exactly once. */
bth_status_t bth_cpb_next(bth_cpb_t *, uint32_t now_ms, bth_cpb_command_t *);
/* Observe every HCI event (never consumes). Returns BTH_OK when the event
 * belonged to CPB, BTH_IGNORED otherwise. */
bth_status_t bth_cpb_on_event(bth_cpb_t *, const bth_h4_frame_t *, uint32_t now_ms);
/* Copies the latest complete received message if it was not read yet.
 * Returns its full length (may exceed capacity: copy is truncated). */
bool bth_cpb_take(bth_cpb_t *, uint8_t *out, size_t capacity, size_t *length, uint32_t *clock, uint32_t *generation);
/* The controller was reset (HCI Reset at host start): forget every controller
 * resource and return to OFF. Storage, the loaded payload and counters stay;
 * a loaded payload is resent by the next start. */
void bth_cpb_controller_reset(bth_cpb_t *);
#ifdef __cplusplus
}
#endif
#endif
