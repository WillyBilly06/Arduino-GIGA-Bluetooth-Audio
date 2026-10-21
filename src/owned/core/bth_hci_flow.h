#ifndef SPEAKER_BTH_HCI_FLOW_H
#define SPEAKER_BTH_HCI_FLOW_H

#include "bth_packet.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BTH_LINK_CLASSIC = 0, BTH_LINK_LE = 1 } bth_link_type_t;
typedef struct { uint16_t length, total, available; } bth_acl_pool_t;
typedef struct {
    uint16_t handle, outstanding;
    bth_link_type_t type;
    bool active;
} bth_hci_link_t;
enum { BTH_HCI_LINK_LIMIT = 2 };
typedef struct {
    uint8_t command_credits;
    bth_acl_pool_t classic, le;
    bth_hci_link_t links[BTH_HCI_LINK_LIMIT];
} bth_hci_flow_t;

// All flow operations belong to the single host event-loop owner. A take is
// committed only immediately before an actually available VHCI send.
void bth_hci_flow_init(bth_hci_flow_t *flow);
bool bth_hci_flow_set_buffers(bth_hci_flow_t *flow, uint16_t classic_len,
                              uint16_t classic_packets, uint16_t le_len, uint16_t le_packets);
bool bth_hci_flow_attach(bth_hci_flow_t *flow, uint16_t handle, bth_link_type_t type);
bool bth_hci_flow_detach(bth_hci_flow_t *flow, uint16_t handle);
bool bth_hci_flow_take_command(bth_hci_flow_t *flow);
bool bth_hci_flow_take_acl(bth_hci_flow_t *flow, uint16_t handle, uint16_t payload_len);
bth_status_t bth_hci_flow_on_event(bth_hci_flow_t *flow, const bth_h4_frame_t *event);

#ifdef __cplusplus
}
#endif
#endif
