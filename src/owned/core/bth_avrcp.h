#ifndef SPEAKER_BTH_AVRCP_H
#define SPEAKER_BTH_AVRCP_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_AVRCP_RX_LIMIT=512, BTH_AVRCP_VOLUME=1 };
typedef struct {
    uint8_t volume,notification_label,rx_label,rx_remaining;
    uint16_t rx_used;
    bool initialized,notification_active,notification_due,rx_active;
    uint8_t rx[BTH_AVRCP_RX_LIMIT];
} bth_avrcp_t;
// AVRCP category-2 target: absolute volume, volume notification, capability,
// unit/subunit queries. No browsing or player metadata is advertised here.
bool bth_avrcp_init(bth_avrcp_t *s,uint8_t volume);
// Owner must reserve output before processing. Short output preserves all state.
// AVCTP fragments borrow no caller storage. An owner timer expires incomplete RX.
bth_status_t bth_avrcp_receive(bth_avrcp_t *s,const uint8_t *in,size_t length,bool secure,
    uint8_t *out,size_t capacity,size_t *written,uint32_t *actions);
bth_status_t bth_avrcp_set_volume(bth_avrcp_t *s,uint8_t volume);
bth_status_t bth_avrcp_notification(bth_avrcp_t *s,uint8_t *out,size_t capacity,size_t *written);
void bth_avrcp_expire(bth_avrcp_t *s);
#ifdef __cplusplus
}
#endif
#endif
