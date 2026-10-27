#ifndef SPEAKER_BTH_LINKS_H
#define SPEAKER_BTH_LINKS_H
#include "bth_cmdq.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { BTH_PEER_EMPTY, BTH_PEER_PENDING, BTH_PEER_CONNECTED } bth_peer_state_t;
typedef struct { uint8_t key[16], key_type, pin_len; } bth_bond_t;
typedef enum { BTH_CONFIRM_REJECT, BTH_CONFIRM_ACCEPT, BTH_CONFIRM_DEFER } bth_confirmation_t;
typedef struct {
    uint8_t io_capability, auth_requirement, min_key_size, pin_len, pin[16];
} bth_security_policy_t;
typedef struct {
    bool (*load)(void *context,const uint8_t peer[6],bth_bond_t *bond);
    bool (*save)(void *context,const uint8_t peer[6],const bth_bond_t *bond);
    bool (*remove)(void *context,const uint8_t peer[6]);
    bth_confirmation_t (*confirm)(void *context,const uint8_t peer[6],uint32_t number);
} bth_security_callbacks_t;
typedef struct {
    uint32_t owner;
    uint8_t peer[6], address_type, role;
    uint16_t handle;
    bth_link_type_t type;
    bth_peer_state_t state;
    bool authenticated, key_size_verified, bonded, key_pending, confirmation_pending, disconnecting;
    bool cached_key_pending,cached_key_accepted,cached_key_encrypted;
    uint8_t stale_size_reads,stale_key_replies;
    uint8_t encryption, key_size, key_type, last_error;
    uint8_t collision_retries,retry_results_pending;
    uint16_t retry_opcode;
    uint32_t retry_at_ms;
    bth_bond_t pending_bond;
} bth_peer_link_t;
typedef struct {
    bth_hci_flow_t *flow;
    bth_cmdq_t *commands;
    bth_security_policy_t policy;
    bth_security_callbacks_t callbacks;
    void *context;
    uint32_t next_owner;
    uint32_t security_now_ms;
    bth_peer_link_t classic, le;
    bool initialized;
} bth_links_t;
// Owner-thread only. Callbacks borrow peer/key data for the duration of the call
// and must not re-enter this state machine. Flow grants are updated separately
// exactly once by the host's central HCI dispatcher.
bool bth_links_init(bth_links_t *links,bth_hci_flow_t *flow,bth_cmdq_t *commands,
                    const bth_security_policy_t *policy,const bth_security_callbacks_t *callbacks,void *context);
bth_status_t bth_links_on_event(bth_links_t *links,const bth_h4_frame_t *event);
// Owner supplies a monotonic modulo32-bit millisecond clock before dispatch and
// periodically while connected. Retry only Classic transaction collisions, at
// least1s apart and at most five times; existing authorization deadlines remain.
bth_status_t bth_links_security_tick(bth_links_t *links,uint32_t now_ms);
const bth_peer_link_t *bth_links_find(const bth_links_t *links,uint16_t handle);
bool bth_links_secure(const bth_links_t *links,uint16_t handle);
// Idempotent teardown; authorization is revoked immediately. Owner supplies a
// bounded deadline while waiting for the controller's Disconnection Complete.
bth_status_t bth_links_disconnect(bth_links_t *links,uint16_t handle,uint8_t reason);
// Resolve a deferred numeric comparison only for the currently connected peer.
bth_status_t bth_links_confirm(bth_links_t *links,const uint8_t peer[6],bool accept);
#ifdef __cplusplus
}
#endif
#endif
