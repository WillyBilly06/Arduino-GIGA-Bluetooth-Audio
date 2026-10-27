#ifndef GIGA_BREDR_RFCOMM_LINK_H
#define GIGA_BREDR_RFCOMM_LINK_H
#include "bth_avrcp_link.h"
#ifdef __cplusplus
extern "C" {
#endif
// Same atomic signaling boundary as the existing AVRCP policy, adding one
// RFCOMM mux per Classic peer. Duplicates receive No Resources, not another mux.
bth_status_t bth_rfcomm_link_receive(bth_l2cap_sig_t*,const uint8_t*,size_t,bool secure,bool avrcp_recovering,uint8_t*,size_t,size_t*,bool* avrcp_collision);
#ifdef __cplusplus
}
#endif
#endif
