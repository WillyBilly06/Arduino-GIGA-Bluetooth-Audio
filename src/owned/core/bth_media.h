#ifndef SPEAKER_BTH_MEDIA_H
#define SPEAKER_BTH_MEDIA_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t sequence, extension_profile;
    uint32_t timestamp, ssrc;
    uint8_t payload_type, csrc_count;
    bool marker;
    bth_span_t extension, payload;
} bth_rtp_view_t;
// Borrowed, checked RTP v2 views. Does not retain or duplicate packet storage.
bth_status_t bth_rtp_decode(const uint8_t *data,size_t length,bth_rtp_view_t *view);
typedef struct {
    uint8_t *storage;
    size_t capacity, used;
    uint32_t timestamp, ssrc;
    uint16_t next_sequence;
    uint8_t remaining;
    bool initialized, active;
} bth_sbc_media_t;
typedef struct { bth_span_t data; uint8_t count; bool reassembled; } bth_sbc_frames_t;
bool bth_sbc_media_init(bth_sbc_media_t *rx,uint8_t *storage,size_t capacity);
// One instance per stream. Complete packets borrow the RTP slot; a fragmented
// frame borrows assembly storage until the next feed/init. Storage is disjoint
// from input. Decoder ownership must be released/transferred before advancing.
// Frame count is metadata only: codec frame boundaries/CRC are checked by decode.
bth_status_t bth_sbc_media_feed(bth_sbc_media_t *rx,const bth_rtp_view_t *rtp,bth_sbc_frames_t *frames);
#ifdef __cplusplus
}
#endif
#endif
