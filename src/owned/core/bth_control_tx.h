#ifndef SPEAKER_BTH_CONTROL_TX_H
#define SPEAKER_BTH_CONTROL_TX_H
#include "bth_acl_tx.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_CONTROL_SLOTS=4, BTH_CONTROL_BYTES=672, BTH_CONTROL_PEERS=2, BTH_CONTROL_PEER_LIMIT=3 };
typedef struct { uint32_t serial; uint8_t slot; } bth_control_token_t;
typedef struct { bth_control_token_t token; uint8_t *data; size_t capacity; } bth_control_reservation_t;
typedef struct {
    uint8_t data[BTH_CONTROL_BYTES];
    uint32_t serial,owner;
    uint16_t length,cid,mtu;
    uint8_t peer,state;
} bth_control_slot_t;
typedef struct {
    bth_acl_tx_t tx;
    uint32_t owner;
    uint16_t handle;
    uint8_t order[BTH_CONTROL_SLOTS],read,count,owned;
    bool attached;
    uint16_t cursor; // Current FIFO-head offset; uses existing tail padding.
} bth_control_peer_t;
typedef struct {
    bth_control_slot_t slots[BTH_CONTROL_SLOTS];
    bth_control_peer_t peers[BTH_CONTROL_PEERS];
    uint8_t free_mask,used,next_peer;
    bool initialized;
} bth_control_tx_t;
// Single owner, initialize only with all outstanding reservations retired.
// Actor selects first peer and data eligibility without changing queue state.
typedef struct { uint8_t first_peer; bool le_data_allowed; uint16_t le_data_max_payload; bool le_control_pending; } bth_control_tx_policy_t;
typedef struct { uint16_t payload_bytes; uint8_t peer; bool le_data,urgent_bypass,paced,shared_reserved; } bth_control_tx_info_t;
void bth_control_tx_init(bth_control_tx_t *q);
bool bth_control_tx_attach(bth_control_tx_t *q,uint8_t peer,uint16_t handle,uint32_t owner);
// Entire disconnected generation only; never cancel a partially sent SDU on a
// still-live handle. Already sent controller credits are owned by HCI flow.
bool bth_control_tx_detach(bth_control_tx_t *q,uint8_t peer,uint32_t owner);
unsigned bth_control_tx_available(const bth_control_tx_t *q,uint8_t peer);
// Reserve before invoking a state-changing protocol parser. Build the reply
// directly here, then commit. Mutable pointer is invalid after commit/cancel.
bth_status_t bth_control_tx_reserve(bth_control_tx_t *q,uint8_t peer,bth_control_reservation_t *reservation);
bth_status_t bth_control_tx_commit(bth_control_tx_t *q,bth_control_token_t token,uint16_t cid,size_t length,uint16_t peer_mtu);
bth_status_t bth_control_tx_cancel(bth_control_tx_t *q,bth_control_token_t token);
// Canonical current owners come from the live link table. Per-peer FIFO, round
// robin fragments across peers; a peer without credits cannot block the other.
// Call only when VHCI can send. CID1 commands use separate L2CAP SDUs,
// retaining one queue slot through every command and its final ACL fragment.
// Other CIDs retain their original SDU boundaries.
bth_status_t bth_control_tx_next(bth_control_tx_t *q,const uint32_t owners[BTH_CONTROL_PEERS],bth_hci_flow_t *flow,
    uint8_t *out,size_t capacity,size_t *written);
// CID5/6 control bypasses ATT pacing. If queued behind ATT, drain that FIFO
// prefix without pacing; never interleave SDUs on the same handle.
bth_status_t bth_control_tx_next_policy(bth_control_tx_t *q,const uint32_t owners[BTH_CONTROL_PEERS],bth_hci_flow_t *flow,
    const bth_control_tx_policy_t *policy,bth_control_tx_info_t *info,
    uint8_t *out,size_t capacity,size_t *written);
#ifdef __cplusplus
}
#endif
#endif
