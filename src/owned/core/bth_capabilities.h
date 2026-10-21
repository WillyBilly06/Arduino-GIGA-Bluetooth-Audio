#ifndef SPEAKER_BTH_CAPABILITIES_H
#define SPEAKER_BTH_CAPABILITIES_H
#include "bth_hci_flow.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    BTH_CAP_COMMANDS, BTH_CAP_CLASSIC0, BTH_CAP_SSP, BTH_CAP_LE_HOST,
    BTH_CAP_CLASSIC1, BTH_CAP_CLASSIC2, BTH_CAP_SC_HOST, BTH_CAP_VERIFY_HOST,
    BTH_CAP_LE_FEATURES, BTH_CAP_LE_STATES, BTH_CAP_READY, BTH_CAP_FAILED
} bth_cap_stage_t;
typedef struct {
    uint8_t commands[64],classic[3][8],le_features[8],le_states[8];
    bth_cap_stage_t stage;
    uint16_t pending_opcode;
    uint8_t hci_version,max_classic_page,error;
    bool pending,initialized,ssp_enabled,le_enabled,sc_enabled,simultaneous_host,classic_sc_allowed;
} bth_capabilities_t;
// Run while scans are off, no connections, after controller Reset. The owner
// applies grants once. This requires the BTDM/SSP capabilities needed by the
// speaker, enables Classic SC if the actual controller supports it, and verifies
// host feature reporting. Missing/inconsistent mandatory support fails closed.
void bth_capabilities_start(bth_capabilities_t *caps,uint8_t hci_version);
// Platform policy may forbid Classic SC even when the controller advertises it.
// SSP, LE encryption, LE SC/ECC and the reported controller features are retained.
void bth_capabilities_start_with_policy(bth_capabilities_t *caps,uint8_t hci_version,bool classic_sc_allowed);
bth_status_t bth_capabilities_next(bth_capabilities_t *caps,bth_hci_flow_t *flow,
    uint8_t *out,size_t capacity,size_t *written);
bth_status_t bth_capabilities_on_event(bth_capabilities_t *caps,const bth_h4_frame_t *event);
bth_status_t bth_capabilities_expire(bth_capabilities_t *caps);
#ifdef __cplusplus
}
#endif
#endif
