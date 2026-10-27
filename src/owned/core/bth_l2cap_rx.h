#ifndef SPEAKER_BTH_L2CAP_RX_H
#define SPEAKER_BTH_L2CAP_RX_H

#include "bth_packet.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;
    size_t capacity, used, expected;
    uint16_t handle;
    bool active, initialized;
} bth_l2cap_rx_t;
typedef struct {
    uint16_t handle, cid;
    bth_span_t payload;
    bool reassembled;
} bth_l2cap_pdu_t;

bool bth_l2cap_rx_init(bth_l2cap_rx_t *rx, uint16_t handle, uint8_t *storage, size_t capacity);
// Complete input PDUs borrow the input packet without copying. Fragmented PDUs
// borrow caller storage until the next feed/init on this instance. The caller
// must retain/transfer the corresponding owned packet/storage before advancing.
// Use one instance per ACL handle; packets for another handle are ignored.
bth_status_t bth_l2cap_rx_feed(bth_l2cap_rx_t *rx, const bth_acl_view_t *acl, bth_l2cap_pdu_t *out);

#ifdef __cplusplus
}
#endif
#endif
