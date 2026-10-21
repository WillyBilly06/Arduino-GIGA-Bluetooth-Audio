#ifndef SPEAKER_BTH_LE_TRANSPORT_H
#define SPEAKER_BTH_LE_TRANSPORT_H
#include "bth_cmdq.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    BTH_LE_TRANSPORT_FEATURES, BTH_LE_TRANSPORT_FEATURES_WAIT,
    BTH_LE_TRANSPORT_UPDATE, BTH_LE_TRANSPORT_SIGNAL,
    BTH_LE_TRANSPORT_SIGNAL_WAIT, BTH_LE_TRANSPORT_UPDATE_WAIT,
    BTH_LE_TRANSPORT_DONE
} bth_le_transport_stage_t;
typedef struct {
    uint32_t owner,deadline;
    uint16_t handle,interval,latency,timeout,tx_octets,tx_time,rx_octets,rx_time;
    bth_le_transport_stage_t stage;
    uint8_t local_features,remote_features,role,identifier,error;
} bth_le_transport_t;
// One caller-owned peer generation. Original speaker preference:18..24,0,400.
bool bth_le_transport_attach(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,uint8_t local_features);
// Queues an owned HCI command, or returns one CID5 signaling SDU in out.
// Reserve transport space before calling in SIGNAL state. No completion/grants
// are consumed here; the central dispatcher retires each command exactly once.
bth_status_t bth_le_transport_next(bth_le_transport_t *t,bth_cmdq_t *commands,uint32_t now_ms,uint8_t *out,size_t capacity,size_t *written);
// command_owner is the oldest matching sent command's generation, read before
// central retirement. A bare opcode/handle is never enough to match a response.
bth_status_t bth_le_transport_event(bth_le_transport_t *t,const bth_h4_frame_t *event,uint32_t command_owner);
bth_status_t bth_le_transport_signal(bth_le_transport_t *t,const uint8_t *data,size_t length);
// Optional preference failure is reported in error; it does not poison the host.
bth_status_t bth_le_transport_expire(bth_le_transport_t *t,uint32_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
