#ifndef SPEAKER_BTH_SDP_H
#define SPEAKER_BTH_SDP_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t handle; bth_span_t attributes; } bth_sdp_record_t;
/* Attributes may be the bare ordered pairs or a complete sequence-wrapped
 * public SDP record. Both forms remain borrowed and immutable. */
typedef struct {
    const bth_sdp_record_t *records;
    size_t count;
    uint32_t fingerprint;
    bool initialized;
} bth_sdp_t;
// Attribute storage is immutable and borrowed for this server's lifetime. Give
// each ACL/session a fresh token. Continuation correlation is not authorization.
bool bth_sdp_init(bth_sdp_t *s,const bth_sdp_record_t *records,size_t count,uint32_t session_token);
bool bth_sdp_init_empty(bth_sdp_t *s,uint32_t session_token);
typedef struct { uint32_t fingerprint; bool valid; } bth_sdp_cursor_t;
/* Compact opaque offsets keep query/session correlation in the caller-owned
 * cursor. Use a fresh zeroed cursor for each SDP channel. */
bth_status_t bth_sdp_process_compact(const bth_sdp_t *s,bth_sdp_cursor_t *cursor,
    const uint8_t *in,size_t len,uint8_t *out,size_t capacity,size_t *written);
// Checked complete SDP PDU -> one response. Capacity is the negotiated outbound
// MTU/storage limit. Input/output must be disjoint. Protocol errors produce an
// SDP Error Response; caller errors return a negative status with written=0.
bth_status_t bth_sdp_process(const bth_sdp_t *s,const uint8_t *in,size_t len,
                            uint8_t *out,size_t capacity,size_t *written);
// Sink, AVRCP target, AVRCP controller records. Register only ready backends.
const bth_sdp_record_t *bth_sdp_speaker_records(size_t *count);
#ifdef __cplusplus
}
#endif
#endif
