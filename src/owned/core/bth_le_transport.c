#include "bth_le_transport.h"
#include <string.h>
bool bth_le_transport_attach(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,uint8_t features) {
    if (!t) return false;
    memset(t,0,sizeof *t);
    if (!owner || handle>0x0eff || role>1) return false;
    t->owner=owner;t->handle=handle;t->role=role;t->local_features=features;t->identifier=1;
    // Bit1 controls the parameter procedure. Bit3 only controls whether the
    // peripheral can initiate feature exchange; its absence cannot justify
    // an L2CAP fallback when both sides might support the LL procedure.
    t->stage=role==0 || (features&2)?BTH_LE_TRANSPORT_FEATURES:BTH_LE_TRANSPORT_SIGNAL;
    return true;
}
static bth_status_t finish(bth_le_transport_t *t,uint8_t error) {
    t->stage=BTH_LE_TRANSPORT_DONE;t->error=error;return BTH_OK;
}
bth_status_t bth_le_transport_next(bth_le_transport_t *t,bth_cmdq_t *q,uint32_t now,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!t || !t->owner || !q || !out || !written) return BTH_ERR_PARAM;
    if (t->stage==BTH_LE_TRANSPORT_DONE) return BTH_IGNORED;
    if (t->stage==BTH_LE_TRANSPORT_SIGNAL) {
        if (cap<12) return BTH_ERR_CAPACITY;
        const uint8_t p[]={0x12,t->identifier,8,0,18,0,24,0,0,0,0x90,1};
        memcpy(out,p,sizeof p);*written=sizeof p;t->stage=BTH_LE_TRANSPORT_SIGNAL_WAIT;t->deadline=now+30000;
        return BTH_OK;
    }
    if (t->stage!=BTH_LE_TRANSPORT_FEATURES && t->stage!=BTH_LE_TRANSPORT_UPDATE) return BTH_MORE;
    if (t->stage==BTH_LE_TRANSPORT_FEATURES && t->role==1 && !(t->local_features&8)) {
        // Wait for the central's feature exchange. If it never arrives, skip
        // this optional optimization rather than guess unsupported features.
        t->stage=BTH_LE_TRANSPORT_FEATURES_WAIT;t->deadline=now+5000;return BTH_OK;
    }
    uint8_t p[]={ (uint8_t)t->handle,(uint8_t)(t->handle>>8),18,0,24,0,0,0,0x90,1,0,0,0,0 };
    const bool features=t->stage==BTH_LE_TRANSPORT_FEATURES;
    bth_status_t st=bth_cmdq_push_owned(q,features?0x2016:0x2013,p,features?2:sizeof p,false,t->owner);
    if (st==BTH_ERR_CAPACITY) return BTH_MORE;
    if (st==BTH_OK) {t->stage=features?BTH_LE_TRANSPORT_FEATURES_WAIT:BTH_LE_TRANSPORT_UPDATE_WAIT;t->deadline=now+(features?5000:30000);}
    return st;
}
bth_status_t bth_le_transport_event(bth_le_transport_t *t,const bth_h4_frame_t *e,uint32_t command_owner) {
    if (!t || !e) return BTH_ERR_PARAM;
    if (!t->owner || e->type!=BTH_H4_EVENT) return BTH_IGNORED;
    const bth_span_t p=e->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    if (e->event.code==0x0f) {
        if (p.len!=4) return BTH_ERR_LENGTH;
        const uint16_t opcode=bth_read_le16(p.data+2);
        if (command_owner!=t->owner ||
            !((opcode==0x2016 && t->stage==BTH_LE_TRANSPORT_FEATURES_WAIT) ||
              (opcode==0x2013 && t->stage==BTH_LE_TRANSPORT_UPDATE_WAIT))) return BTH_IGNORED;
        if (!p.data[0]) return BTH_OK;
        if (opcode==0x2013 && p.data[0]==0x1a && t->role==1) {t->stage=BTH_LE_TRANSPORT_SIGNAL;return BTH_OK;}
        return finish(t,p.data[0]);
    }
    if (e->event.code!=0x3e || !p.len) return BTH_IGNORED;
    if (p.data[0]==4) {
        if (p.len!=12) return BTH_ERR_LENGTH;
        if (bth_read_le16(p.data+2)!=t->handle ||
            (t->stage!=BTH_LE_TRANSPORT_FEATURES_WAIT && t->stage!=BTH_LE_TRANSPORT_FEATURES)) return BTH_IGNORED;
        if (p.data[1]) return finish(t,p.data[1]);
        t->remote_features=p.data[4];
        t->stage=t->role==0 || ((t->local_features&t->remote_features&2)!=0)?BTH_LE_TRANSPORT_UPDATE:BTH_LE_TRANSPORT_SIGNAL;
        return BTH_OK;
    }
    if (p.data[0]==3) {
        if (p.len!=10) return BTH_ERR_LENGTH;
        if (bth_read_le16(p.data+2)!=t->handle) return BTH_IGNORED;
        if (p.data[1]) {
            if (t->stage!=BTH_LE_TRANSPORT_UPDATE_WAIT) return BTH_IGNORED;
            if (p.data[1]==0x1a && t->role==1) {t->stage=BTH_LE_TRANSPORT_SIGNAL;return BTH_OK;}
            return finish(t,p.data[1]);
        }
        const uint16_t interval=bth_read_le16(p.data+4),latency=bth_read_le16(p.data+6),timeout=bth_read_le16(p.data+8);
        if (interval<6 || interval>3200 || latency>499 || timeout<10 || timeout>3200 ||
            (uint32_t)timeout*4<=((uint32_t)latency+1)*interval) return BTH_ERR_PARAM;
        t->interval=interval;t->latency=latency;t->timeout=timeout;
        if (t->stage==BTH_LE_TRANSPORT_UPDATE_WAIT || t->stage==BTH_LE_TRANSPORT_SIGNAL_WAIT) return finish(t,0);
        return BTH_OK;
    }
    if (p.data[0]==7) {
        if (p.len!=11) return BTH_ERR_LENGTH;
        if (bth_read_le16(p.data+1)!=t->handle) return BTH_IGNORED;
        t->tx_octets=bth_read_le16(p.data+3);t->tx_time=bth_read_le16(p.data+5);
        t->rx_octets=bth_read_le16(p.data+7);t->rx_time=bth_read_le16(p.data+9);return BTH_OK;
    }
    return BTH_IGNORED;
}
bth_status_t bth_le_transport_signal(bth_le_transport_t *t,const uint8_t *p,size_t n) {
    if (!t || (!p && n)) return BTH_ERR_PARAM;
    if (!t->owner || t->stage!=BTH_LE_TRANSPORT_SIGNAL_WAIT || n<2 || p[1]!=t->identifier || (p[0]!=0x13 && p[0]!=1)) return BTH_IGNORED;
    if (n<4 || bth_read_le16(p+2)!=n-4) return BTH_ERR_LENGTH;
    if (p[0]==1) {if (n<6) return BTH_ERR_LENGTH;return finish(t,0xfe);}
    if (n!=6 || bth_read_le16(p+4)>1) return BTH_ERR_LENGTH;
    if (p[4]) return finish(t,1);
    t->stage=BTH_LE_TRANSPORT_UPDATE_WAIT;return BTH_OK;
}
bth_status_t bth_le_transport_expire(bth_le_transport_t *t,uint32_t now) {
    if (!t || !t->owner) return BTH_ERR_PARAM;
    if (t->stage==BTH_LE_TRANSPORT_DONE || t->stage==BTH_LE_TRANSPORT_FEATURES ||
        t->stage==BTH_LE_TRANSPORT_UPDATE || t->stage==BTH_LE_TRANSPORT_SIGNAL) return BTH_IGNORED;
    if ((int32_t)(now-t->deadline)<0) return BTH_MORE;
    return finish(t,0xff);
}
