#ifndef GIGA_BREDR_BTH_SCO_SETUP_H
#define GIGA_BREDR_BTH_SCO_SETUP_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {BTH_SCO_SETUP_NONE=0,BTH_SCO_SETUP_CONTROLLER,BTH_SCO_SETUP_BUFFERS,BTH_SCO_SETUP_ROUTE_QUERY,BTH_SCO_SETUP_ROUTE_REQUIRED,BTH_SCO_SETUP_ROUTE_WRITE,BTH_SCO_SETUP_ROUTE_MISMATCH,BTH_SCO_SETUP_FLOW_UNAVAILABLE,BTH_SCO_SETUP_MALFORMED,BTH_SCO_SETUP_TIMEOUT} bth_sco_setup_reason_t;
typedef struct {uint16_t manufacturer,tx_packets;uint8_t tx_bytes;bool allow_route_configuration,allow_rx_pacing;} bth_sco_setup_config_t;
typedef struct {uint32_t serial;uint16_t opcode;uint8_t step;} bth_sco_setup_ticket_t;
typedef struct {uint16_t opcode;uint8_t parameters[5],length;bth_sco_setup_ticket_t ticket;} bth_sco_setup_command_t;
typedef struct {
 bth_sco_setup_config_t config;uint8_t commands[64],baseline_route[5],step;uint32_t serial,deadline;
 bth_sco_setup_ticket_t ticket;bool initialized,pending,done,available,controller_flow,rx_paced,route_written,requires_reset,have_route,flow_write_supported;
 uint8_t last_status; bth_sco_setup_reason_t reason;
} bth_sco_setup_t;
// Startup-only after a known HCI Reset. Emits command intents; performs no I/O.
// The optional documented Broadcom route write is reachable only after actual
// manufacturer15 and successful FC1D status+five-parameter read. It changes
// only routing=1, preserves the four returned parameters, then verifies all5.
bool bth_sco_setup_init(bth_sco_setup_t*,const bth_sco_setup_config_t*);
bth_status_t bth_sco_setup_next(bth_sco_setup_t*,uint32_t now_ms,bth_sco_setup_command_t*);
bth_status_t bth_sco_setup_queued(bth_sco_setup_t*,bth_sco_setup_ticket_t,uint32_t now_ms);
bth_status_t bth_sco_setup_on_event(bth_sco_setup_t*,const bth_h4_frame_t*,uint32_t now_ms);
bth_status_t bth_sco_setup_expire(bth_sco_setup_t*,uint32_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
