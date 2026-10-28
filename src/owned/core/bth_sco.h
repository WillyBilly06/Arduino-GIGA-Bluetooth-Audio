#ifndef GIGA_BREDR_BTH_SCO_H
#define GIGA_BREDR_BTH_SCO_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_SCO_RX_LIMIT=8,BTH_SCO_CVSD=1,BTH_SCO_MSBC=2,BTH_SCO_HOST_UNIT_BYTES=60 };
typedef enum { BTH_SCO_CLOSED=0,BTH_SCO_ATTACHED,BTH_SCO_REQUESTED,BTH_SCO_WAIT_STATUS,BTH_SCO_WAIT_COMPLETE,BTH_SCO_CONNECTED,BTH_SCO_DISCONNECTING,BTH_SCO_FAILED } bth_sco_state_t;
typedef struct { uint32_t generation,serial;uint16_t count; } bth_sco_token_t;
typedef struct {uint16_t opcode;uint8_t parameters[21],length;bth_sco_token_t ticket;} bth_sco_command_t;
typedef struct {
 uint32_t owner;uint16_t acl_handle;uint8_t peer_hci_address[6],codec;
 // Facts supplied by the actual controller/backend setup, never inferred from
 // a chipset name. No command in this module configures a vendor route.
 bool route_confirmed,controller_tx_flow,host_rx_flow,allow_rx_pacing;
 uint16_t controller_tx_packets;uint8_t controller_tx_bytes,rx_slots,rx_bytes;
 uint16_t max_latency,voice_setting,packet_types;uint8_t retransmission_effort;
} bth_sco_config_t;
typedef struct {bth_span_t payload;uint8_t packet_status;bth_sco_token_t ticket;} bth_sco_rx_view_t;
typedef struct {uint16_t length;uint8_t status;bth_sco_token_t ticket;} bth_sco_rx_slot_t;
typedef struct {
 bth_sco_config_t config;bth_sco_state_t state;uint8_t* receive_storage;size_t receive_bytes;
 uint32_t generation,serial,deadline;uint16_t handle,tx_available,tx_outstanding,pending_completed;
 bth_sco_rx_slot_t receive[BTH_SCO_RX_LIMIT];uint8_t rx_read,rx_count,last_error;
 // Local fallback entitlement, NOT controller packet credits. Its bound is
 // RX storage bytes plus59 bytes of codec-unit remainder. Every accepted RX
 // byte is retained; overflow is an explicit error, never silent clipping.
 uint16_t rx_pacing_bytes;
 uint8_t interval_slots,air_mode;uint16_t air_rx_bytes,air_tx_bytes;
 uint16_t pending_opcode;bool initialized,ready,reject,command_queued,tx_reserved,completed_reserved,active_handle,close_on_complete;
 uint8_t close_reason,local_error;
 bth_sco_token_t command_ticket,tx_ticket,completed_ticket;
 uint32_t rx_packets,rx_status_errors,rx_overflows,tx_submitted,rx_pacing_overflows,rx_pacing_rejected_bytes;
} bth_sco_t;
// Single serialized host actor. Storage is caller-reserved for this selected
// configuration and retained until teardown. No allocator, IRQ, codec or worker.
bool bth_sco_init(bth_sco_t*,const bth_sco_config_t*,uint8_t* receive_storage,size_t);
// Only while definitely detached from synchronous audio; preserves generation
// and ticket counters across CVSD/mSBC changes on the same admitted ACL owner.
bth_status_t bth_sco_reconfigure(bth_sco_t*,const bth_sco_config_t*,uint8_t*,size_t);
bool bth_sco_backend_ready(const bth_sco_t*);
bth_status_t bth_sco_on_event(bth_sco_t*,const bth_h4_frame_t*,bool secure,uint32_t now_ms);
// Preparation must be acknowledged by the real output worker before ACCEPT.
bth_status_t bth_sco_audio_ready(bth_sco_t*,uint32_t owner,uint32_t generation,bool prepared);
bth_status_t bth_sco_next_command(const bth_sco_t*,bth_sco_command_t*);
bth_status_t bth_sco_command_queued(bth_sco_t*,bth_sco_token_t,uint32_t now_ms);
bth_status_t bth_sco_expire(bth_sco_t*,uint32_t now_ms);
bth_status_t bth_sco_request_close(bth_sco_t*,uint32_t owner,uint32_t generation,uint8_t reason,uint32_t now_ms);
// Caller centrally classifies handles; SCO completions must not be passed to
// the separate ACL pool. A controller report cannot exceed actual submissions.
bth_status_t bth_sco_completed(bth_sco_t*,uint16_t handle,uint16_t count);
// Caller checks current ACL authorization before data dispatch. Views borrow
// input only during receive. A receive credit returns only on consumed storage.
bth_status_t bth_sco_receive(bth_sco_t*,uint32_t owner,const bth_sco_view_t*);
bth_status_t bth_sco_peek_rx(const bth_sco_t*,uint32_t owner,uint32_t generation,bth_sco_rx_view_t*);
bth_status_t bth_sco_consume_rx(bth_sco_t*,bth_sco_token_t);
// Reserve/prepare before the synchronous H4 copy; commit immediately on true
// copy admission. These receipts are not RF delivery or remote audio proof.
bth_status_t bth_sco_prepare_tx(bth_sco_t*,uint32_t owner,uint32_t generation,const uint8_t*,size_t,uint8_t*,size_t,size_t*,bth_sco_token_t*);
bth_status_t bth_sco_commit_tx(bth_sco_t*,bth_sco_token_t);
bth_status_t bth_sco_prepare_completed(bth_sco_t*,uint8_t*,size_t,size_t*,bth_sco_token_t*);
bth_status_t bth_sco_commit_completed(bth_sco_t*,bth_sco_token_t);
#ifdef __cplusplus
}
#endif
#endif
