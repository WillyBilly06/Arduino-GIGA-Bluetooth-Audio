#ifndef SPEAKER_BTH_GAP_H
#define SPEAKER_BTH_GAP_H
#include "bth_hci_flow.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_GAP_UUID_LIMIT=16 };
// Current BTDM SDK RAM patch rejects a zero synchronous count even with zero
// synchronous connections configured. Reserve one real transport discard buffer
// of 4+64 bytes; SCO flow control remains off (ACL-only mode1). No SCO profile.
enum { BTH_GAP_SYNC_BYTES=64, BTH_GAP_SYNC_PACKETS=1 };
typedef enum {
    BTH_GAP_SCAN_OFF, BTH_GAP_EVENT_MASK, BTH_GAP_LE_MASK, BTH_GAP_HOST_BUFFERS,
    BTH_GAP_RX_FLOW, BTH_GAP_SSP, BTH_GAP_NAME, BTH_GAP_CLASS,
    BTH_GAP_INQUIRY_MODE, BTH_GAP_EIR, BTH_GAP_READY, BTH_GAP_FAILED
} bth_gap_stage_t;
typedef struct {
    bth_span_t name;
    const uint16_t *service_uuids;
    uint32_t class_of_device;
    uint16_t host_acl_length,host_acl_packets;
    uint8_t uuid_count;
    bool le_ecc_events;
} bth_gap_config_t;
typedef struct {
    bth_gap_config_t config;
    bth_gap_stage_t stage;
    uint16_t pending_opcode;
    uint8_t error,scan,desired_scan;
    bool pending,scan_update,initialized;
} bth_gap_t;
// Name/UUID storage remains valid until initialization completes. Advertise only
// service backends that are actually ready; this setup leaves all scans OFF.
// ESP32's current ROM host-buffer helper requires ACL length >=1021. The packet
// count must match the actual separately reserved host receive-storage budget.
bool bth_gap_start(bth_gap_t *gap,const bth_gap_config_t *config);
bth_status_t bth_gap_next_command(bth_gap_t *gap,bth_hci_flow_t *flow,uint8_t *out,size_t capacity,size_t *written);
// The central owner updates HCI grants exactly once before dispatching here.
bth_status_t bth_gap_on_event(bth_gap_t *gap,const bth_h4_frame_t *event);
// HCI scan bits:1 inquiry,2 page. Enabling either requires ready profile backend.
// Commit scan state only after matching success completion. Disabling is allowed
// regardless of backend readiness. Uses ordinary command grants.
bth_status_t bth_gap_set_scan(bth_gap_t *gap,bth_hci_flow_t *flow,bool backend_ready,uint8_t scan,
    uint8_t *out,size_t capacity,size_t *written);
// Owner deadline hook. Pending/unknown controller state becomes terminal failure
// and requires controller reset; a timer never supplies permission or credits.
bth_status_t bth_gap_expire(bth_gap_t *gap);
#ifdef __cplusplus
}
#endif
#endif
