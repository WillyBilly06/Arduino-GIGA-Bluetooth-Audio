#ifndef SPEAKER_BTH_RX_FLOW_H
#define SPEAKER_BTH_RX_FLOW_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef SPEAKER_BTH_RX_SLOT_LIMIT
#define SPEAKER_BTH_RX_SLOT_LIMIT 16
#endif
// The full speaker retains16 metadata slots. Only the isolated receive-credit
// diagnostics compile20/40, consistently for the application and this component.
enum { BTH_RX_SLOT_LIMIT=SPEAKER_BTH_RX_SLOT_LIMIT, BTH_RX_LINK_LIMIT=2 };
typedef struct { uint32_t serial; uint8_t slot; } bth_rx_lease_t;
typedef struct { uint32_t serial; uint8_t slot; } bth_rx_admission_t;
typedef struct { uint32_t serial,owner; uint16_t handle; bool busy; } bth_rx_slot_t;
typedef struct { uint32_t owner,admission_serial; uint16_t handle,outstanding,pending; bool active,reserved; } bth_rx_link_t;
typedef struct {
    bth_rx_slot_t slots[BTH_RX_SLOT_LIMIT];
    bth_rx_link_t links[BTH_RX_LINK_LIMIT];
    uint16_t budget,outstanding;
    uint32_t next_admission_serial;
    uint8_t slot_count;
    bool initialized;
} bth_rx_flow_t;
// Single event-loop owner, including release. A worker sends its lease back to
// the owner after its final borrowed packet reference is retired. Slot ownership
// survives disconnect until that release; detached packets never ACK a new link.
// Reserve two controller windows: one may remain held after disconnect while
// the other live link receives the replenished window. New connection admission
// is gated until all detached leases retire. Reserve an admission ticket BEFORE
// HCI Accept / connectable advertising; consume it on asynchronous completion.
// A can_attach check alone does not preserve storage across that interval.
bool bth_rx_flow_init(bth_rx_flow_t *rx,uint8_t slots,uint16_t advertised_packets);
bool bth_rx_flow_can_attach(const bth_rx_flow_t *rx);
// Reserve before HCI accept/connectable advertising. A completion may arrive
// after the other peer detached; this pre-reserved window remains real storage.
// Owner0 is for an LE advertisement whose future link owner is not known yet.
bool bth_rx_flow_reserve(bth_rx_flow_t *rx,uint32_t owner,bth_rx_admission_t *admission);
bool bth_rx_flow_cancel_admission(bth_rx_flow_t *rx,bth_rx_admission_t admission);
bool bth_rx_flow_attach_reserved(bth_rx_flow_t *rx,bth_rx_admission_t admission,uint16_t handle,uint32_t owner);
bool bth_rx_flow_attach(bth_rx_flow_t *rx,uint16_t handle,uint32_t owner);
bool bth_rx_flow_detach(bth_rx_flow_t *rx,uint16_t handle,uint32_t owner);
bth_status_t bth_rx_flow_received(bth_rx_flow_t *rx,uint8_t slot,uint16_t handle,uint32_t owner,bth_rx_lease_t *lease);
// OK: pending controller acknowledgment; IGNORED: valid retired-link lease,
// storage released without ACK; error: stale/duplicate lease, do not free twice.
bth_status_t bth_rx_flow_release(bth_rx_flow_t *rx,bth_rx_lease_t lease);
// Call only immediately before an available VHCI send. Encodes credit-exempt
// Host Number Of Completed Packets (0x0c35), commits pending acknowledgments.
// Never take an ordinary HCI command grant or wait for a Command Complete.
bth_status_t bth_rx_flow_next_completed(bth_rx_flow_t *rx,uint8_t *out,size_t capacity,size_t *written);
#ifdef __cplusplus
}
#endif
#endif
