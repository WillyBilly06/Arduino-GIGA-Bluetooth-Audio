#ifndef SPEAKER_BTH_LE_GAP_H
#define SPEAKER_BTH_LE_GAP_H
#include "bth_cmdq.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { BTH_LE_STOP,BTH_LE_PARAMETERS,BTH_LE_DATA,BTH_LE_SCAN_DATA,BTH_LE_DATA_LENGTH,BTH_LE_READY,BTH_LE_FAILED } bth_le_gap_stage_t;
typedef struct { bth_span_t advertisement,scan_response; uint16_t interval_min,interval_max; bool data_length_extension; } bth_le_gap_config_t;
typedef struct {
    bth_cmdq_t *commands;
    bth_le_gap_config_t config;
    uint32_t deadline;
    uint16_t pending_opcode;
    bth_le_gap_stage_t stage;
    uint8_t error;
    bool initialized,pending,advertising,connected,requested_enable;
} bth_le_gap_t;
// Legacy public/connectable undirected advertising; caller-owned AD storage.
// Controller/features/ATT backend must be ready before starting this instance.
bool bth_le_gap_start(bth_le_gap_t *g,bth_cmdq_t *commands,const bth_le_gap_config_t *config);
// Queues one owned command; the central dispatcher alone updates grants and
// retires cmdq completion records. want_advertising requires a reserved RX ticket.
bth_status_t bth_le_gap_next(bth_le_gap_t *g,bool want_advertising,uint32_t now_ms);
bth_status_t bth_le_gap_on_event(bth_le_gap_t *g,const bth_h4_frame_t *event);
// Correlated replies for an established LE peer: accept valid connection
// parameters, reject invalid parameters, and reject LTKs until SMP is installed.
// Does not consume command completions or update HCI grants.
bth_status_t bth_le_gap_peer_event(bth_le_gap_t *g,uint32_t owner,uint16_t handle,const bth_h4_frame_t *event);
void bth_le_gap_connection(bth_le_gap_t *g,bool connected);
bth_status_t bth_le_gap_expire(bth_le_gap_t *g,uint32_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
