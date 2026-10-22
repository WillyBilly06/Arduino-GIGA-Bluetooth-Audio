#include "bth_le_transport.h"
#include <string.h>
static void put16(uint8_t *out,uint16_t value) {out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);}
bool bth_le_transport_policy_valid(const bth_le_transport_policy_t *p) {
    return p && p->interval_min>=6 && p->interval_max<=3200 && p->interval_min<=p->interval_max &&
        p->latency<=499 && p->timeout>=10 && p->timeout<=3200 &&
        (uint32_t)p->timeout*4>((uint32_t)p->latency+1)*p->interval_max;
}
static void cancel_optional(bth_le_transport_t *t) {
    if (!t->commands || !t->owner) return;
    (void)bth_cmdq_cancel_owned(t->commands,t->owner,0x2016);
    (void)bth_cmdq_cancel_owned(t->commands,t->owner,0x2013);
}
static bth_status_t finish(bth_le_transport_t *t,uint8_t error) {
    cancel_optional(t);t->stage=BTH_LE_TRANSPORT_DONE;t->error=error;t->deadline=0;t->command_opcode=0;return BTH_OK;
}
bool bth_le_transport_configure(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,
                               const uint8_t features[8],const bth_le_transport_policy_t *policy) {
    if (!t || !policy) return false;
    const bth_le_transport_policy_t copy=*policy;uint8_t bits[8]={0};if(features)memcpy(bits,features,8);
    memset(t,0,sizeof *t);t->stage=BTH_LE_TRANSPORT_DONE;
    if (!owner || handle>0x0eff || role>1 || !bth_le_transport_policy_valid(&copy)) return false;
    t->owner=owner;t->handle=handle;t->role=role;t->identifier=1;t->policy=copy;
    t->last_update_status=0xff;
    t->local_features_known=features!=NULL;memcpy(t->local_feature_set,bits,8);t->local_features=bits[0];
    t->stage=BTH_LE_TRANSPORT_IDLE;return true;
}
bth_status_t bth_le_transport_start(bth_le_transport_t *t,uint32_t now) {
    if (!t || !t->owner) return BTH_ERR_PARAM;
    if (t->started) return BTH_IGNORED;
    t->started=true;t->attempt_timed=true;t->attempt_deadline=now+65000;
    if (t->parameters_known && t->matches_preference) return finish(t,0);
    if (!t->local_features_known) return finish(t,0xfd);
    // Bit1 controls the parameter procedure. Bit3 only controls whether the
    // peripheral can initiate feature exchange; its absence cannot justify
    // an L2CAP fallback when both sides might support the LL procedure.
    if (t->remote_features_known)
        t->stage=t->role==0 || ((t->local_features&t->remote_features&2)!=0)?BTH_LE_TRANSPORT_UPDATE:BTH_LE_TRANSPORT_SIGNAL;
    else t->stage=t->role==0 || (t->local_features&2)?BTH_LE_TRANSPORT_FEATURES:BTH_LE_TRANSPORT_SIGNAL;
    return BTH_OK;
}
bool bth_le_transport_attach(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,uint8_t features) {
    const bth_le_transport_policy_t legacy={18,24,0,400};const uint8_t bits[8]={features};
    if (!bth_le_transport_configure(t,owner,handle,role,bits,&legacy)) return false;
    (void)bth_le_transport_start(t,0);t->attempt_timed=false; // Legacy clock starts on first next/expire.
    return true;
}
bth_status_t bth_le_transport_yield(bth_le_transport_t *t,uint8_t error) {
    if (!t || !t->owner) return BTH_ERR_PARAM;
    if (!t->started || t->stage==BTH_LE_TRANSPORT_DONE) return BTH_IGNORED;
    return finish(t,error);
}
bth_status_t bth_le_transport_cancel(bth_le_transport_t *t,uint32_t owner) {
    if (!t) return BTH_ERR_PARAM;
    if (!owner || t->owner!=owner) return BTH_IGNORED;
    cancel_optional(t);memset(t,0,sizeof *t);t->stage=BTH_LE_TRANSPORT_DONE;return BTH_OK;
}
static bth_status_t signal_fallback(bth_le_transport_t *t) {
    if (t->role!=1 || t->fallback_used) return finish(t,0x1a);
    cancel_optional(t);t->stage=BTH_LE_TRANSPORT_SIGNAL;t->command_opcode=0;t->deadline=0;return BTH_OK;
}
static bth_status_t observed(bth_le_transport_t *t,uint16_t interval,uint16_t latency,uint16_t timeout,uint8_t source) {
    const bth_le_transport_policy_t actual={interval,interval,latency,timeout};
    if (!bth_le_transport_policy_valid(&actual)) return BTH_ERR_PARAM;
    t->interval=interval;t->latency=latency;t->timeout=timeout;t->parameters_known=true;t->parameters_source=source;
    t->matches_preference=interval>=t->policy.interval_min && interval<=t->policy.interval_max &&
        latency==t->policy.latency && timeout==t->policy.timeout;
    if ((t->started && t->stage!=BTH_LE_TRANSPORT_DONE && t->matches_preference) ||
        t->stage==BTH_LE_TRANSPORT_UPDATE_WAIT || t->stage==BTH_LE_TRANSPORT_SIGNAL_WAIT) return finish(t,0);
    return BTH_OK;
}
bth_status_t bth_le_transport_next(bth_le_transport_t *t,bth_cmdq_t *q,uint32_t now,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!t || !t->owner || !q || !q->initialized || !out || !written) return BTH_ERR_PARAM;
    if (t->commands && t->commands!=q) return BTH_ERR_STATE;
    t->commands=q;
    if (t->stage==BTH_LE_TRANSPORT_DONE || t->stage==BTH_LE_TRANSPORT_IDLE) return BTH_IGNORED;
    (void)bth_le_transport_expire(t,now);
    if (t->stage==BTH_LE_TRANSPORT_DONE) return BTH_IGNORED;
    if (t->stage==BTH_LE_TRANSPORT_SIGNAL) {
        if (cap<12) return BTH_ERR_CAPACITY;
        uint8_t p[]={0x12,t->identifier,8,0,0,0,0,0,0,0,0,0};
        put16(p+4,t->policy.interval_min);put16(p+6,t->policy.interval_max);put16(p+8,t->policy.latency);put16(p+10,t->policy.timeout);
        memcpy(out,p,sizeof p);*written=sizeof p;t->fallback_used=true;t->stage=BTH_LE_TRANSPORT_SIGNAL_WAIT;t->deadline=now+30000;
        return BTH_OK;
    }
    if (t->stage!=BTH_LE_TRANSPORT_FEATURES && t->stage!=BTH_LE_TRANSPORT_UPDATE) return BTH_MORE;
    if (t->stage==BTH_LE_TRANSPORT_FEATURES && t->role==1 && !(t->local_features&8)) {
        // Wait for the central's feature exchange. If it never arrives, skip
        // this optional optimization rather than guess unsupported features.
        t->stage=BTH_LE_TRANSPORT_FEATURES_WAIT;t->deadline=now+5000;return BTH_OK;
    }
    uint8_t p[14]={0};put16(p,t->handle);put16(p+2,t->policy.interval_min);put16(p+4,t->policy.interval_max);
    put16(p+6,t->policy.latency);put16(p+8,t->policy.timeout); // CE lengths remain0/0.
    const bool features=t->stage==BTH_LE_TRANSPORT_FEATURES;
    bth_status_t st=bth_cmdq_push_owned(q,features?0x2016:0x2013,p,features?2:sizeof p,false,t->owner);
    if (st==BTH_ERR_CAPACITY) return BTH_MORE;
    if (st==BTH_OK) {t->command_opcode=features?0x2016:0x2013;t->stage=features?BTH_LE_TRANSPORT_FEATURES_WAIT:BTH_LE_TRANSPORT_UPDATE_WAIT;t->deadline=now+(features?5000:30000);}
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
        if (command_owner!=t->owner || opcode!=t->command_opcode ||
            !((opcode==0x2016 && t->stage==BTH_LE_TRANSPORT_FEATURES_WAIT) ||
              (opcode==0x2013 && t->stage==BTH_LE_TRANSPORT_UPDATE_WAIT))) return BTH_IGNORED;
        t->command_opcode=0;
        if (!p.data[0]) return BTH_OK;
        if (opcode==0x2013 && p.data[0]==0x1a && t->role==1) return signal_fallback(t);
        return finish(t,p.data[0]);
    }
    if (e->event.code!=0x3e || !p.len) return BTH_IGNORED;
    if (p.data[0]==1 || p.data[0]==0x0a) {
        const bool enhanced=p.data[0]==0x0a;
        if (p.len!=(enhanced?31u:19u)) return BTH_ERR_LENGTH;
        if (p.data[1] || bth_read_le16(p.data+2)!=t->handle) return BTH_IGNORED;
        if (p.data[4]!=t->role || p.data[5]>(enhanced?3u:1u) || p.data[p.len-1]>7) return BTH_ERR_PARAM;
        const unsigned at=enhanced?24u:12u;
        return observed(t,bth_read_le16(p.data+at),bth_read_le16(p.data+at+2),bth_read_le16(p.data+at+4),p.data[0]);
    }
    if (p.data[0]==4) {
        if (p.len!=12) return BTH_ERR_LENGTH;
        if (bth_read_le16(p.data+2)!=t->handle) return BTH_IGNORED;
        if (t->stage!=BTH_LE_TRANSPORT_FEATURES_WAIT && t->stage!=BTH_LE_TRANSPORT_FEATURES &&
            t->stage!=BTH_LE_TRANSPORT_IDLE && t->stage!=BTH_LE_TRANSPORT_DONE) return BTH_IGNORED;
        if (p.data[1] && (t->stage==BTH_LE_TRANSPORT_IDLE || t->stage==BTH_LE_TRANSPORT_DONE)) return BTH_IGNORED;
        if (p.data[1]) return finish(t,p.data[1]);
        memcpy(t->remote_feature_set,p.data+4,8);t->remote_features_known=true;t->remote_features=p.data[4];
        if (t->stage==BTH_LE_TRANSPORT_IDLE || t->stage==BTH_LE_TRANSPORT_DONE) return BTH_OK;
        cancel_optional(t);t->command_opcode=0;t->deadline=0;
        t->stage=t->role==0 || ((t->local_features&t->remote_features&2)!=0)?BTH_LE_TRANSPORT_UPDATE:BTH_LE_TRANSPORT_SIGNAL;
        return BTH_OK;
    }
    if (p.data[0]==3) {
        if (p.len!=10) return BTH_ERR_LENGTH;
        if (bth_read_le16(p.data+2)!=t->handle) return BTH_IGNORED;
        if (p.data[1]) {
            t->update_reported=true;t->last_update_status=p.data[1];
            if (t->stage!=BTH_LE_TRANSPORT_UPDATE_WAIT) return BTH_OK;
            if (p.data[1]==0x1a && t->role==1) return signal_fallback(t);
            return finish(t,p.data[1]);
        }
        const uint16_t interval=bth_read_le16(p.data+4),latency=bth_read_le16(p.data+6),timeout=bth_read_le16(p.data+8);
        const bth_status_t result=observed(t,interval,latency,timeout,3);
        if (result==BTH_OK) {t->update_reported=true;t->last_update_status=0;}
        return result;
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
    if (t->stage==BTH_LE_TRANSPORT_DONE || t->stage==BTH_LE_TRANSPORT_IDLE) return BTH_IGNORED;
    if (!t->attempt_timed) {t->attempt_timed=true;t->attempt_deadline=now+65000;}
    if ((int32_t)(now-t->attempt_deadline)>=0) return finish(t,0xff);
    if (t->stage==BTH_LE_TRANSPORT_FEATURES || t->stage==BTH_LE_TRANSPORT_UPDATE || t->stage==BTH_LE_TRANSPORT_SIGNAL) return BTH_MORE;
    if ((int32_t)(now-t->deadline)<0) return BTH_MORE;
    return finish(t,0xff);
}
bth_status_t bth_le_transport_peer_reply(const bth_le_transport_t *t,uint32_t owner,const bth_h4_frame_t *e,
                                        bool enforce,uint8_t *out,size_t cap,size_t *written,uint16_t *opcode) {
    if (written) *written=0;if (opcode) *opcode=0;
    if (!t || !e || !out || !written || !opcode) return BTH_ERR_PARAM;
    if (!owner || owner!=t->owner || e->type!=BTH_H4_EVENT || e->event.code!=0x3e) return BTH_IGNORED;
    const bth_span_t p=e->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    if (!p.len || p.data[0]!=6) return BTH_IGNORED;
    if (p.len!=11) return BTH_ERR_LENGTH;
    if (bth_read_le16(p.data+1)!=t->handle) return BTH_IGNORED;
    bth_le_transport_policy_t peer={bth_read_le16(p.data+3),bth_read_le16(p.data+5),bth_read_le16(p.data+7),bth_read_le16(p.data+9)};
    bool valid=bth_le_transport_policy_valid(&peer);
    if (valid && enforce) {
        if (peer.interval_min<t->policy.interval_min) peer.interval_min=t->policy.interval_min;
        if (peer.interval_max>t->policy.interval_max) peer.interval_max=t->policy.interval_max;
        valid=bth_le_transport_policy_valid(&peer);
    }
    uint8_t reply[14]={0};put16(reply,t->handle);
    if (valid) {put16(reply+2,peer.interval_min);put16(reply+4,peer.interval_max);put16(reply+6,peer.latency);put16(reply+8,peer.timeout);}
    else reply[2]=0x3b;
    const size_t n=valid?14:3;
    if (cap<n) return BTH_ERR_CAPACITY;
    memcpy(out,reply,n);*written=n;*opcode=valid?0x2020:0x2021;return BTH_OK;
}
