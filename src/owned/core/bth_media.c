#include "bth_media.h"
#include <string.h>
static uint16_t read16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8)|p[1]; }
static uint32_t read32(const uint8_t *p) { return ((uint32_t)read16(p)<<16)|read16(p+2); }
bth_status_t bth_rtp_decode(const uint8_t *p,size_t len,bth_rtp_view_t *out) {
    if (!out) return BTH_ERR_PARAM;
    memset(out,0,sizeof *out);
    if (!p) return BTH_ERR_PARAM;
    if (len<12) return BTH_ERR_LENGTH;
    if (p[0]>>6!=2) return BTH_ERR_PARAM;
    const uint8_t csrc=p[0]&15;
    size_t header=12+(size_t)csrc*4;
    if (header>len) return BTH_ERR_LENGTH;
    bth_rtp_view_t view={0};
    if (p[0]&0x10) {
        if (len-header<4) return BTH_ERR_LENGTH;
        const size_t extra=(size_t)read16(p+header+2)*4;
        if (extra>len-header-4) return BTH_ERR_LENGTH;
        view.extension_profile=read16(p+header); view.extension=(bth_span_t){p+header+4,extra};
        header+=4+extra;
    }
    size_t end=len;
    if (p[0]&0x20) {
        const uint8_t padding=p[len-1];
        if (!padding || padding>len-header) return BTH_ERR_LENGTH;
        end-=padding;
    }
    view.sequence=read16(p+2); view.timestamp=read32(p+4); view.ssrc=read32(p+8);
    view.payload_type=p[1]&0x7f; view.marker=(p[1]&0x80)!=0; view.csrc_count=csrc;
    view.payload=(bth_span_t){p+header,end-header}; *out=view; return BTH_OK;
}
bool bth_sbc_media_init(bth_sbc_media_t *rx,uint8_t *storage,size_t cap) {
    if (!rx) return false;
    memset(rx,0,sizeof *rx);
    if (!storage || !cap) return false;
    rx->storage=storage; rx->capacity=cap; rx->initialized=true; return true;
}
static bth_status_t reset(bth_sbc_media_t *rx,bth_status_t status) {
    rx->active=false; rx->used=0; rx->remaining=0; return status;
}
bth_status_t bth_sbc_media_feed(bth_sbc_media_t *rx,const bth_rtp_view_t *r,bth_sbc_frames_t *out) {
    if (!out) return BTH_ERR_PARAM;
    memset(out,0,sizeof *out);
    if (!rx || !rx->initialized || !r || !r->payload.data) return BTH_ERR_PARAM;
    const bth_span_t p=r->payload;
    if (p.len<2) return reset(rx,BTH_ERR_LENGTH);
    if ((uintptr_t)p.data<=(uintptr_t)rx->storage?(uintptr_t)rx->storage-(uintptr_t)p.data<p.len:
        (uintptr_t)p.data-(uintptr_t)rx->storage<rx->capacity) return reset(rx,BTH_ERR_PARAM);
    const bool fragmented=(p.data[0]&0x80)!=0, start=(p.data[0]&0x40)!=0, last=(p.data[0]&0x20)!=0;
    const uint8_t count=p.data[0]&15;
    if (!count || (!fragmented && (start || last)) || (fragmented && (last!=(count==1) || (start && last)))) return reset(rx,BTH_ERR_PARAM);
    if (!fragmented) {
        (void)reset(rx,BTH_OK); out->data=(bth_span_t){p.data+1,p.len-1}; out->count=count; return BTH_OK;
    }
    if (start) {
        (void)reset(rx,BTH_OK); rx->timestamp=r->timestamp; rx->ssrc=r->ssrc;
    } else if (!rx->active || r->sequence!=rx->next_sequence || r->timestamp!=rx->timestamp ||
               r->ssrc!=rx->ssrc || count!=rx->remaining) return reset(rx,BTH_ERR_STATE);
    if (p.len-1>rx->capacity-rx->used) return reset(rx,BTH_ERR_CAPACITY);
    memcpy(rx->storage+rx->used,p.data+1,p.len-1); rx->used+=p.len-1;
    rx->next_sequence=(uint16_t)(r->sequence+1); rx->remaining=(uint8_t)(count-1); rx->active=!last;
    if (!last) return BTH_MORE;
    out->data=(bth_span_t){rx->storage,rx->used}; out->count=1; out->reassembled=true; return BTH_OK;
}
