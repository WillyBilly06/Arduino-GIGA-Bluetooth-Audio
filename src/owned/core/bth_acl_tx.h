#ifndef SPEAKER_BTH_ACL_TX_H
#define SPEAKER_BTH_ACL_TX_H
#include "bth_hci_flow.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bth_span_t data;
    uint32_t owner,position;
    uint16_t handle,cid;
    bool active;
} bth_acl_tx_t;
void bth_acl_tx_init(bth_acl_tx_t *tx);
// Borrows immutable SDU bytes until completion or cancellation. No copy/heap.
// One instance per active outgoing SDU; caller schedules Classic/LE fairly.
bth_status_t bth_acl_tx_begin(bth_acl_tx_t *tx,uint16_t handle,uint32_t owner,uint16_t remote_cid,
    bth_span_t data,uint16_t peer_mtu);
bool bth_acl_tx_cancel(bth_acl_tx_t *tx,uint32_t owner);
// Call only when VHCI can send, with canonical current link generation. Output
// and borrowed SDU storage are disjoint. Success consumes one controller packet
// credit and advances exactly one H4 ACL fragment. active=false means complete.
// Uses non-automatically-flushable PB=0 start (valid for Classic and LE), PB=1
// continuation; the controller pool lengths can split even the L2CAP header.
bth_status_t bth_acl_tx_next(bth_acl_tx_t *tx,uint32_t current_owner,bth_hci_flow_t *flow,
    uint8_t *out,size_t capacity,size_t *written);
#ifdef __cplusplus
}
#endif
#endif
