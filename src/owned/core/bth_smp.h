#ifndef SPEAKER_BTH_SMP_H
#define SPEAKER_BTH_SMP_H
#include "bth_smp_crypto.h"
#include "bth_smp_ecc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*bth_smp_random_fn)(void *context,uint8_t *out,size_t length);
typedef struct {uint8_t type,value[6];} bth_smp_address_t;
typedef struct {uint32_t cooldown_until;uint8_t failures;} bth_smp_rate_t;
typedef struct {uint32_t sent,completed,smp_target;uint16_t remaining;bool smp_fragment,marked;} bth_smp_ack_t;
// Track the same live handle's actually submitted ACL packets and validated
// Number Of Completed Packets counts. The watermark excludes later ATT traffic.
bool bth_smp_ack_sent(bth_smp_ack_t *a,const bth_acl_view_t *packet);
bool bth_smp_ack_completed(bth_smp_ack_t *a,uint16_t packets);
bool bth_smp_ack_ready(const bth_smp_ack_t *a);
// Wire-order keys/addresses. Identity is provisional until encrypted reconnect.
typedef struct {
    uint8_t ltk[16],rand[8],irk[16];
    bth_smp_address_t connection,identity;
    uint16_t ediv;
    uint8_t key_size;
    bool secure_connections,has_ltk,has_irk,has_identity,identity_verified;
} bth_le_bond_t;
typedef struct {
    const bth_smp_crypto_t *crypto;
    bth_smp_ecc_t *ecc;
    bth_smp_random_fn random;
    void *random_context;
    // Optional owner-retained per-identity backoff, surviving physical reconnect.
    bth_smp_rate_t *rate;
    bth_smp_address_t local,peer;
    uint8_t local_irk[16],minimum_key_size;
    bool secure_connections;
} bth_smp_config_t;
typedef enum {
    BTH_SMP_IDLE,BTH_SMP_CONFIRM,BTH_SMP_RANDOM,BTH_SMP_PUBLIC,
    BTH_SMP_LOCAL_PUBLIC,BTH_SMP_DHKEY,BTH_SMP_SC_RANDOM,BTH_SMP_CHECK,
    BTH_SMP_ENCRYPTION,BTH_SMP_DISTRIBUTE,BTH_SMP_KEYS,BTH_SMP_COMPLETE,
    BTH_SMP_FAILED,BTH_SMP_TIMED_OUT
} bth_smp_state_t;
typedef struct {
    bth_smp_config_t config;
    bth_le_bond_t bond,previous_bond;
    uint32_t owner,deadline,cooldown_until;
    bth_smp_state_t state;
    uint8_t request[7],response[7],local_public[64],remote_public[64];
    uint8_t local_random[16],remote_random[16],remote_confirm[16],dhkey[32];
    uint8_t check_a[16],check_b[16],session_key[16],tx[65];
    uint8_t tx_length,key_size,own_keys,peer_keys,distribution_step,peer_step,error,failures;
    bool initialized,timer_active,ecc_requested,secure_connections,bonding;
    bool phase2_verified,key_issued,encrypted,verified,own_keys_acked,bond_pending;
} bth_smp_t;
// Peripheral-only NoInputNoOutput/JustWorks. No heap, threads or ECC math.
// Each instance belongs to one live LE generation. Caller provides peer's actual
// connection address, not a resolved identity, and a validated candidate bond.
bool bth_smp_init(bth_smp_t *s,uint32_t owner,const bth_smp_config_t *config,const bth_le_bond_t *bond);
void bth_smp_detach(bth_smp_t *s);
// MORE: internal transmit slot is occupied, so input remains entirely unconsumed.
// Malformed/unexpected peer input creates a wire failure, not a host-fatal error.
bth_status_t bth_smp_receive(bth_smp_t *s,const uint8_t *p,size_t length,uint32_t now_ms);
bth_status_t bth_smp_advance(bth_smp_t *s,uint32_t now_ms);
// Reserve transport first. Successful take means this PDU is QUEUED in-order by
// the caller. Short capacity leaves state unchanged. Timer resets on queuing.
bth_status_t bth_smp_take_tx(bth_smp_t *s,uint8_t *out,size_t capacity,size_t *length,uint32_t now_ms);
// Only after ALL queued SMP SDUs have actually been baseband-acknowledged on the
// same live generation. A VHCI send, command complete or empty FIFO is not ACK.
void bth_smp_transport_drained(bth_smp_t *s);
bool bth_smp_ltk_request(bth_smp_t *s,uint16_t ediv,const uint8_t rand[8],uint8_t out[16],uint32_t now_ms);
void bth_smp_encryption(bth_smp_t *s,bool enabled,uint8_t status,uint32_t now_ms);
bool bth_smp_take_bond(bth_smp_t *s,bth_le_bond_t *out);
bool bth_smp_bond_matches(const bth_smp_crypto_t *crypto,const bth_le_bond_t *bond,bth_smp_address_t address);
#ifdef __cplusplus
}
#endif
#endif
