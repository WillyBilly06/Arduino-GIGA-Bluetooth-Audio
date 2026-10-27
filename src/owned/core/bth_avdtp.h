#ifndef SPEAKER_BTH_AVDTP_H
#define SPEAKER_BTH_AVDTP_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_AVDTP_CODEC_LIMIT=32, BTH_AVDTP_ENDPOINT_LIMIT=16 };
typedef enum {
    BTH_AVDTP_IDLE, BTH_AVDTP_CONFIGURED, BTH_AVDTP_OPENING, BTH_AVDTP_OPEN,
    BTH_AVDTP_STREAMING, BTH_AVDTP_CLOSING, BTH_AVDTP_ABORTING
} bth_avdtp_state_t;
enum {
    BTH_AVDTP_CONFIGURED_ACTION=1, BTH_AVDTP_OPEN_TRANSPORT=2,
    BTH_AVDTP_START_AUDIO=4, BTH_AVDTP_SUSPEND_AUDIO=8,
    BTH_AVDTP_CLOSE_TRANSPORT=16, BTH_AVDTP_ABORT_AUDIO=32,
    BTH_AVDTP_DELAY_ACK=64, BTH_AVDTP_DELAY_FAILED=128, BTH_AVDTP_SIGNAL_EXPIRED=256
};
enum { BTH_AVDTP_EXPIRE_ASSEMBLY=1, BTH_AVDTP_EXPIRE_DELAY=2 };
typedef struct {
    uint8_t seid;
    bth_span_t codec_caps; // Full Media Codec payload: media type, codec type, CIE.
    // Pure validation only; no allocation, decoder setup or reentry. Return an
    // AVDTP/A2DP error octet on failure. A successful reply commits configuration.
    uint8_t (*validate)(bth_span_t selected,bth_span_t capabilities);
    bool delay_reporting;
} bth_avdtp_endpoint_t;
typedef struct {
    const bth_avdtp_endpoint_t *endpoints;
    size_t endpoint_count;
    uint8_t *rx_storage;
    size_t rx_capacity, rx_used, rx_packet_size;
    bth_avdtp_state_t state;
    uint8_t local_seid, remote_seid, codec_len, codec[BTH_AVDTP_CODEC_LIMIT];
    uint8_t rx_label, rx_message, rx_signal, rx_remaining, delay_label;
    bool initialized, rx_active, delay_selected, delay_pending;
} bth_avdtp_t;
// One active media stream per signaling channel; multiple codec endpoints.
// Endpoint/capability and assembly storage remain valid for the instance lifetime.
bool bth_avdtp_init(bth_avdtp_t *s,const bth_avdtp_endpoint_t *endpoints,size_t count,uint8_t *assembly,size_t capacity);
// Complete AVDTP signaling packet, without L2CAP header. Caller supplies actual
// link-policy satisfaction; state-changing commands cannot succeed without it.
// Output/input/assembly storage are disjoint. MORE means fragment consumption;
// short output leaves stream and assembly metadata unchanged for a caller retry.
bth_status_t bth_avdtp_receive(bth_avdtp_t *s,const uint8_t *in,size_t len,bool secure,
                              uint8_t *out,size_t capacity,size_t *written,uint32_t *actions);
bth_status_t bth_avdtp_media_connected(bth_avdtp_t *s);
bth_status_t bth_avdtp_transport_closed(bth_avdtp_t *s);
bth_status_t bth_avdtp_delay_report(bth_avdtp_t *s,uint16_t delay_100us,uint8_t label,
                                   uint8_t *out,size_t capacity,size_t *written);
// Owner timer/cancellation hook. Retires only the selected pending procedure;
// reports recovery actions without pretending a media transport has closed.
bth_status_t bth_avdtp_expire(bth_avdtp_t *s,uint8_t pending,uint32_t *actions);
// Production SBC selection validator. Other codec families supply validators
// backed by their existing decoder formats rather than generic bit assumptions.
uint8_t bth_avdtp_sbc_validate(bth_span_t selected,bth_span_t capabilities);
#ifdef __cplusplus
}
#endif
#endif
