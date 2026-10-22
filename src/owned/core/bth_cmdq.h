#ifndef SPEAKER_BTH_CMDQ_H
#define SPEAKER_BTH_CMDQ_H
#include "bth_hci_flow.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_CMD_QUEUE_LIMIT=8, BTH_CMD_PARAM_LIMIT=255 };
typedef struct { uint32_t owner; uint16_t opcode; uint8_t len, params[BTH_CMD_PARAM_LIMIT]; } bth_command_t;
// The first parameter word occupies the former two padding bytes. Known
// handle-bearing LE commands use it for completion preflight, even after detach.
typedef struct { uint32_t owner; uint16_t opcode,handle; } bth_command_sent_t;
typedef struct {
    bth_command_t slots[BTH_CMD_QUEUE_LIMIT];
    uint8_t order[2][BTH_CMD_QUEUE_LIMIT], read[2], count[2];
    uint8_t free_mask, used;
    bth_command_sent_t sent[BTH_CMD_QUEUE_LIMIT];
    uint8_t sent_count;
    bool initialized;
} bth_cmdq_t;
// Owner-thread operations. Parameters are copied into static storage. Urgent
// security/control replies precede normal traffic; each class remains FIFO.
void bth_cmdq_init(bth_cmdq_t *q);
unsigned bth_cmdq_available(const bth_cmdq_t *q);
bth_status_t bth_cmdq_push(bth_cmdq_t *q,uint16_t opcode,const uint8_t *params,size_t len,bool urgent);
// Nonzero owner identifies one link attempt/generation, not a reusable handle.
bth_status_t bth_cmdq_push_owned(bth_cmdq_t *q,uint16_t opcode,const uint8_t *params,size_t len,bool urgent,uint32_t owner);
unsigned bth_cmdq_cancel_owner(bth_cmdq_t *q,uint32_t owner);
// Cancel only still-queued commands with both generation and opcode matching.
// Other security replies and already-sent ownership records remain intact.
unsigned bth_cmdq_cancel_owned(bth_cmdq_t *q,uint32_t owner,uint16_t opcode);
// Consume the oldest sent command matching this completion/status opcode.
// Canceled links retain sent tombstones so late replies keep their old owner.
// This does not update controller grants. Dispatch each HCI response once.
bth_status_t bth_cmdq_complete(bth_cmdq_t *q,const bth_h4_frame_t *event,uint32_t *owner);
// Call only when VHCI can send. On success consumes one command credit and
// transfers exactly one encoded command into external caller-owned output.
bth_status_t bth_cmdq_next(bth_cmdq_t *q,bth_hci_flow_t *flow,uint8_t *out,size_t capacity,size_t *written);
#ifdef __cplusplus
}
#endif
#endif
