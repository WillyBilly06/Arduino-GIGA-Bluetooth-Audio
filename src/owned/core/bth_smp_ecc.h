#ifndef SPEAKER_BTH_SMP_ECC_H
#define SPEAKER_BTH_SMP_ECC_H
#include "bth_cmdq.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {BTH_SMP_ECC_IDLE,BTH_SMP_ECC_WAIT_PUBLIC,BTH_SMP_ECC_WAIT_DHKEY,BTH_SMP_ECC_DONE,BTH_SMP_ECC_POISONED} bth_smp_ecc_state_t;
typedef enum {BTH_SMP_ECC_NONE,BTH_SMP_ECC_PUBLIC,BTH_SMP_ECC_DHKEY} bth_smp_ecc_kind_t;
typedef struct {
    uint32_t owner;
    bth_smp_ecc_kind_t kind;
    uint8_t status,length,data[64];
} bth_smp_ecc_result_t;
typedef struct {
    bth_cmdq_t *commands;
    uint32_t owner,public_owner,deadline;
    uint16_t opcode;
    bth_smp_ecc_state_t state;
    bth_smp_ecc_kind_t kind;
    uint8_t public_key[64],dhkey[32],error;
    bool initialized,submitted,accepted,abandoned,have_public,async_received,result_pending;
} bth_smp_ecc_t;
// Global controller ECC state. Call ONLY after an actual Controller Reset and
// after clearing its old queued/sent command records. The queue must be empty.
// Reinitializing a live/timed-out operation without resetting is unsafe.
bool bth_smp_ecc_after_reset(bth_smp_ecc_t *e,bth_cmdq_t *commands);
// One generation owns both its local key and subsequent DHKey operations.
// Public coordinates and DHKey use HCI/SMP LITTLE-ENDIAN octet order.
bth_status_t bth_smp_ecc_read_public(bth_smp_ecc_t *e,uint32_t owner,uint32_t now_ms);
bth_status_t bth_smp_ecc_generate(bth_smp_ecc_t *e,uint32_t owner,const uint8_t remote_public_key[64],uint32_t now_ms);
// Notify after cmdq encodes the owned command, before handing it to VHCI.
bth_status_t bth_smp_ecc_command_sent(bth_smp_ecc_t *e,uint32_t owner,uint16_t opcode,uint32_t now_ms);
// Before central command retirement. This never retires commands/grants.
bth_status_t bth_smp_ecc_on_event(bth_smp_ecc_t *e,const bth_h4_frame_t *event,uint32_t command_owner);
// Copy one completed result, then erase the internal DHKey. Caller erases its
// copied secret after use. No result may be inherited by a different owner.
bth_status_t bth_smp_ecc_take(bth_smp_ecc_t *e,uint32_t owner,bth_smp_ecc_result_t *result);
// Unsent jobs are canceled precisely; sent jobs remain busy until completion.
bth_status_t bth_smp_ecc_abandon(bth_smp_ecc_t *e,uint32_t owner);
// Submitted timeout poisons global ECC until Controller Reset. A queued job
// can be safely canceled without poisoning when it demonstrably wasn't sent.
bth_status_t bth_smp_ecc_expire(bth_smp_ecc_t *e,uint32_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
