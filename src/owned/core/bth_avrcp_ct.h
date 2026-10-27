#ifndef SPEAKER_BTH_AVRCP_CT_H
#define SPEAKER_BTH_AVRCP_CT_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_AVRCP_CT_LABELS=16, BTH_AVRCP_CT_RX=512 };
enum { BTH_AVRCP_CT_CAPABILITIES=1, BTH_AVRCP_CT_NOTIFICATION=2, BTH_AVRCP_CT_KEY=4, BTH_AVRCP_CT_REJECTED=8 };
typedef struct { uint32_t deadline; uint8_t kind,event,operation; bool released,changed; } bth_avrcp_ct_pending_t;
typedef struct {
    bth_avrcp_ct_pending_t pending[BTH_AVRCP_CT_LABELS];
    uint16_t supported_events,disabled_events,registered_events,retired_labels,expired_keys,rx_used;
    uint8_t next_label,rx_label,rx_remaining;
    bool initialized,capabilities_known,rx_active,protocol_rejected;
    uint8_t rx[BTH_AVRCP_CT_RX];
} bth_avrcp_ct_t;
typedef struct {
    uint32_t actions,position;
    uint8_t label,event,operation,response,status,volume,play_status,track[8];
    bool released,interim;
} bth_avrcp_ct_event_t;
void bth_avrcp_ct_init(bth_avrcp_ct_t *s);
// Reserve a transport reply slot first. All output is a complete single AVCTP
// packet. Short output or unavailable labels leaves controller state unchanged.
bth_status_t bth_avrcp_ct_capabilities(bth_avrcp_ct_t *s,bool secure,uint32_t now_ms,
    uint8_t *out,size_t capacity,size_t *written);
bth_status_t bth_avrcp_ct_passthrough(bth_avrcp_ct_t *s,bool secure,uint8_t operation,bool released,uint32_t now_ms,
    uint8_t *out,size_t capacity,size_t *written);
bth_status_t bth_avrcp_ct_register(bth_avrcp_ct_t *s,bool secure,uint8_t event,uint32_t interval_seconds,uint32_t now_ms,
    uint8_t *out,size_t capacity,size_t *written);
// Responses only; the companion target handles command packets. State belongs
// to one AVRCP channel generation, and is reinitialized after channel teardown.
bth_status_t bth_avrcp_ct_receive(bth_avrcp_ct_t *s,const uint8_t *in,size_t length,bool secure,bth_avrcp_ct_event_t *event);
// Returns timed-out label bits. Retired labels are never reused on this channel.
uint16_t bth_avrcp_ct_expire(bth_avrcp_ct_t *s,uint32_t now_ms);
void bth_avrcp_ct_expire_assembly(bth_avrcp_ct_t *s);
#ifdef __cplusplus
}
#endif
#endif
