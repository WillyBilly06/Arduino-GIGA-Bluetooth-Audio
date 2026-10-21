#ifndef SPEAKER_BTH_L2CAP_SIG_H
#define SPEAKER_BTH_L2CAP_SIG_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_CHANNEL_LIMIT=6, BTH_SERVICE_LIMIT=6, BTH_SIGNAL_MTU=672 };
typedef enum {
    BTH_CHANNEL_CLOSED, BTH_CHANNEL_WAIT_SECURITY, BTH_CHANNEL_CONNECTING,
    BTH_CHANNEL_CONFIGURING, BTH_CHANNEL_OPEN, BTH_CHANNEL_DISCONNECTING
} bth_channel_state_t;
typedef struct { uint16_t psm, receive_mtu; bool secure; } bth_l2cap_service_t;
typedef struct {
    uint16_t psm, local_cid, remote_cid, local_mtu, remote_mtu;
    uint16_t pending_remote_mtu, config_error;
    uint8_t connect_id, config_id, disconnect_id, retries;
    bth_channel_state_t state;
    bool local_configured, remote_configured, config_continuation, cancel_connect;
} bth_l2cap_channel_t;
typedef struct {
    bth_l2cap_channel_t channels[BTH_CHANNEL_LIMIT];
    bth_l2cap_service_t services[BTH_SERVICE_LIMIT];
    uint16_t handle;
    uint8_t service_count, next_id;
    bool initialized;
} bth_l2cap_sig_t;
// BR/EDR Basic-mode signaling, one instance per ACL. No heap or transport calls.
bool bth_l2cap_sig_init(bth_l2cap_sig_t *s,uint16_t handle,const bth_l2cap_service_t *services,size_t count);
const bth_l2cap_channel_t *bth_l2cap_sig_find(const bth_l2cap_sig_t *s,uint16_t local_cid);
// Input/output are disjoint signaling payloads (without L2CAP header). Output
// contains whole commands for the transport to pack into signaling PDUs. On
// any error written=0 and state is unchanged; discard any partial output bytes.
bth_status_t bth_l2cap_sig_process(bth_l2cap_sig_t *s,const uint8_t *in,size_t len,bool secure,
                                  uint8_t *out,size_t capacity,size_t *written);
// Call only after link security policy is satisfied. Completes pending requests.
bth_status_t bth_l2cap_sig_security_ready(bth_l2cap_sig_t *s,uint8_t *out,size_t capacity,size_t *written);
bth_status_t bth_l2cap_sig_connect(bth_l2cap_sig_t *s,uint16_t psm,bool secure,
                                  uint8_t *out,size_t capacity,size_t *written,uint16_t *local_cid);
bth_status_t bth_l2cap_sig_disconnect(bth_l2cap_sig_t *s,uint16_t local_cid,
                                     uint8_t *out,size_t capacity,size_t *written);
// Cancel any channel state. An outgoing pending connect is retained until its
// response supplies a remote CID, then disconnected without configuration.
bth_status_t bth_l2cap_sig_cancel(bth_l2cap_sig_t *s,uint16_t local_cid,
    uint8_t *out,size_t capacity,size_t *written);
#ifdef __cplusplus
}
#endif
#endif
