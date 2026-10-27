#include "bth_avrcp_link.h"
#include <string.h>
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
bth_status_t bth_avrcp_link_receive(bth_l2cap_sig_t *s,const uint8_t *in,size_t length,bool secure,bool recovering,
    uint8_t *out,size_t capacity,size_t *written,bool *collision) {
    if (written) *written=0;
    if (collision) *collision=false;
    if (!s || !s->initialized || (!in && length) || !out || !written || !collision) return BTH_ERR_PARAM;
    if (length>BTH_SIGNAL_MTU) return BTH_ERR_LENGTH;
    bth_l2cap_sig_t next=*s; size_t used=0; bool collided=false;
    for (size_t position=0;position<length;) {
        if (length-position<4) return BTH_ERR_LENGTH;
        const uint8_t *p=in+position; const size_t bytes=bth_read_le16(p+2);
        if (!p[1]) return BTH_ERR_PARAM;
        if (bytes>length-position-4) return BTH_ERR_LENGTH;
        bool intercepted=false;
        if (p[0]==2 && bytes==4 && bth_read_le16(p+4)==0x17) {
            const uint16_t source=bth_read_le16(p+6); const bth_l2cap_channel_t *same=NULL;
            bool allocated=false;
            for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) if (next.channels[i].state!=BTH_CHANNEL_CLOSED && next.channels[i].remote_cid==source) {
                allocated=true; if (next.channels[i].psm==0x17) same=&next.channels[i];
            }
            const bool retry=same && same->state==BTH_CHANNEL_WAIT_SECURITY && same->connect_id==p[1];
            const bool conflict=source>=0x40 && !allocated && (recovering || bth_avrcp_link_count(&next));
            if (retry || conflict) {
                if (capacity-used<12) return BTH_ERR_CAPACITY;
                uint8_t *reply=out+used; reply[0]=3; reply[1]=p[1]; put16(reply+2,8);
                put16(reply+4,retry?same->local_cid:0); put16(reply+6,source);
                put16(reply+8,retry?1:4); put16(reply+10,retry?1:0); used+=12;
                intercepted=true; collided|=conflict;
            }
        }
        if (!intercepted) {
            size_t count=0;
            bth_status_t st=bth_l2cap_sig_process(&next,p,bytes+4,secure,out+used,capacity-used,&count);
            if (st!=BTH_OK) return st;
            used+=count;
        }
        position+=bytes+4;
    }
    *s=next; *written=used; *collision=collided; return BTH_OK;
}
bth_status_t bth_avrcp_link_cancel(bth_l2cap_sig_t *s,uint8_t *out,size_t capacity,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || !out || !written) return BTH_ERR_PARAM;
    bth_l2cap_sig_t next=*s; size_t used=0; bool changed=false;
    for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) if (next.channels[i].psm==0x17 && next.channels[i].state!=BTH_CHANNEL_CLOSED) {
        size_t count=0;
        bth_status_t st=bth_l2cap_sig_cancel(&next,next.channels[i].local_cid,out+used,capacity-used,&count);
        if (st<0) return st;
        changed|=st==BTH_OK; used+=count;
    }
    *s=next; *written=used; return changed?BTH_OK:BTH_MORE;
}
unsigned bth_avrcp_link_count(const bth_l2cap_sig_t *s) {
    unsigned n=0; if (s) for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) if (s->channels[i].psm==0x17 && s->channels[i].state!=BTH_CHANNEL_CLOSED) ++n;
    return n;
}
