#ifndef SPEAKER_BTH_AVRCP_LINK_H
#define SPEAKER_BTH_AVRCP_LINK_H
#include "bth_l2cap_sig.h"
// AVRCP permits one control channel. Reject a colliding new attempt, then let
// the owner cancel the existing attempt(s) and retry after100..1000ms. Same-CID
// retransmissions are not new attempts. Other PSMs retain normal L2CAP behavior.
bth_status_t bth_avrcp_link_receive(bth_l2cap_sig_t *s,const uint8_t *in,size_t length,bool secure,bool recovering,
    uint8_t *out,size_t capacity,size_t *written,bool *collision);
bth_status_t bth_avrcp_link_cancel(bth_l2cap_sig_t *s,uint8_t *out,size_t capacity,size_t *written);
unsigned bth_avrcp_link_count(const bth_l2cap_sig_t *s);
#endif
