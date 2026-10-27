#include "bth_avrcp_ct.h"
#include <string.h>
enum { KEY=1,CAPS=2,NOTIFICATION=3,REPLY_TIMEOUT=3000 };
void bth_avrcp_ct_init(bth_avrcp_ct_t *s) { if (s) { memset(s,0,sizeof(*s)); s->initialized=true; } }
static bth_status_t output_ready(bth_avrcp_ct_t *s,bool secure,uint8_t *out,size_t capacity,size_t need,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || !out || !written) return BTH_ERR_PARAM;
    if (!secure || s->protocol_rejected) return BTH_ERR_STATE;
    return capacity<need?BTH_ERR_CAPACITY:BTH_OK;
}
static int label(const bth_avrcp_ct_t *s) {
    for (unsigned i=0;i<16;++i) {
        const unsigned n=(s->next_label+i)&15;
        if (!s->pending[n].kind && !(s->retired_labels&(1u<<n))) return (int)n;
    }
    return -1;
}
static void commit(bth_avrcp_ct_t *s,unsigned n,bth_avrcp_ct_pending_t pending,uint32_t now) {
    pending.deadline=now+REPLY_TIMEOUT; s->pending[n]=pending; s->next_label=(uint8_t)((n+1)&15);
}
static void vendor(uint8_t *out,uint8_t n,uint8_t type,uint8_t pdu,size_t params) {
    const uint8_t header[]={0,0x11,0x0e,type,0x48,0,0,0x19,0x58,pdu,0,0,0};
    memcpy(out,header,sizeof header); out[0]=(uint8_t)(n<<4); out[11]=(uint8_t)(params>>8); out[12]=(uint8_t)params;
}
bth_status_t bth_avrcp_ct_capabilities(bth_avrcp_ct_t *s,bool secure,uint32_t now_ms,uint8_t *out,size_t capacity,size_t *written) {
    bth_status_t st=output_ready(s,secure,out,capacity,14,written); if (st!=BTH_OK) return st;
    for (unsigned i=0;i<16;++i) if (s->pending[i].kind==CAPS) return BTH_MORE;
    int n=label(s); if (n<0) return BTH_MORE;
    vendor(out,(uint8_t)n,1,0x10,1); out[13]=3; *written=14;
    commit(s,(unsigned)n,(bth_avrcp_ct_pending_t){.kind=CAPS},now_ms); return BTH_OK;
}
bth_status_t bth_avrcp_ct_passthrough(bth_avrcp_ct_t *s,bool secure,uint8_t operation,bool released,uint32_t now_ms,uint8_t *out,size_t capacity,size_t *written) {
    bth_status_t st=output_ready(s,secure,out,capacity,8,written); if (st!=BTH_OK) return st;
    if (operation!=0x44 && operation!=0x45 && operation!=0x46 && operation!=0x48 && operation!=0x49 && operation!=0x4b && operation!=0x4c) return BTH_UNSUPPORTED;
    int n=label(s); if (n<0) return BTH_MORE;
    const uint8_t message[]={0,0x11,0x0e,0,0x48,0x7c,0,0}; memcpy(out,message,sizeof message);
    out[0]=(uint8_t)(n<<4); out[6]=(uint8_t)(operation|(released?0x80:0)); *written=sizeof message;
    commit(s,(unsigned)n,(bth_avrcp_ct_pending_t){.kind=KEY,.operation=operation,.released=released},now_ms); return BTH_OK;
}
bth_status_t bth_avrcp_ct_register(bth_avrcp_ct_t *s,bool secure,uint8_t event,uint32_t interval_seconds,uint32_t now_ms,uint8_t *out,size_t capacity,size_t *written) {
    bth_status_t st=output_ready(s,secure,out,capacity,18,written); if (st!=BTH_OK) return st;
    if (event!=1 && event!=2 && event!=5 && event!=13) return BTH_UNSUPPORTED;
    if ((event==5 && !interval_seconds) || (event!=5 && interval_seconds)) return BTH_ERR_PARAM;
    if (!s->capabilities_known) return BTH_MORE;
    if (!(s->supported_events&(1u<<event)) || (s->disabled_events&(1u<<event))) return BTH_UNSUPPORTED;
    if (s->registered_events&(1u<<event)) return BTH_MORE;
    int n=label(s); if (n<0) return BTH_MORE;
    vendor(out,(uint8_t)n,3,0x31,5); out[13]=event;
    out[14]=(uint8_t)(interval_seconds>>24); out[15]=(uint8_t)(interval_seconds>>16); out[16]=(uint8_t)(interval_seconds>>8); out[17]=(uint8_t)interval_seconds;
    *written=18; commit(s,(unsigned)n,(bth_avrcp_ct_pending_t){.kind=NOTIFICATION,.event=event},now_ms);
    s->registered_events|=(uint16_t)(1u<<event); return BTH_OK;
}
static bth_status_t response(bth_avrcp_ct_t *s,uint8_t n,const uint8_t *p,size_t length,bth_avrcp_ct_event_t *event) {
    if (length<3) return BTH_ERR_LENGTH;
    const bth_avrcp_ct_pending_t pending=s->pending[n];
    if (!pending.kind) return BTH_IGNORED;
    const uint8_t type=p[0]&15;
    const bool rejected=type==8 || type==10;
    bth_avrcp_ct_event_t result={.label=n,.response=type,.event=pending.event,.operation=pending.operation,.released=pending.released};
    if (p[1]!=0x48) return BTH_ERR_PARAM;
    if (pending.kind==KEY) {
        if (length<5 || p[4]!=length-5) return BTH_ERR_LENGTH;
        if (p[2]!=0x7c || p[3]!=(uint8_t)(pending.operation|(pending.released?0x80:0)) || p[4]!=0 || (type!=9 && !rejected)) return BTH_ERR_PARAM;
        result.actions=BTH_AVRCP_CT_KEY|(rejected?BTH_AVRCP_CT_REJECTED:0);
        memset(&s->pending[n],0,sizeof s->pending[n]); *event=result; return BTH_OK;
    }
    if (length<10) return BTH_ERR_LENGTH;
    if (p[2]!=0 || p[3]!=0 || p[4]!=0x19 || p[5]!=0x58 || (p[7]&3)!=0) return BTH_ERR_PARAM;
    const size_t params=((size_t)p[8]<<8)|p[9];
    if (params!=length-10) return BTH_ERR_LENGTH;
    const uint8_t expected=pending.kind==CAPS?0x10:0x31;
    if (p[6]!=expected) return BTH_ERR_PARAM;
    const uint8_t *data=p+10;
    if (rejected) {
        if (type==10 && params!=1) return BTH_ERR_LENGTH;
        result.actions=(pending.kind==CAPS?BTH_AVRCP_CT_CAPABILITIES:BTH_AVRCP_CT_NOTIFICATION)|BTH_AVRCP_CT_REJECTED;
        result.status=type==10?data[0]:0xff;
        if (pending.kind==CAPS) { s->capabilities_known=true; s->supported_events=0; }
        else {
            // App/player changes and internal/no-player errors are transient.
            // Only unsupported command/event responses disable subscriptions.
            if (type==8 || result.status==0 || result.status==1) s->disabled_events|=(uint16_t)(1u<<pending.event);
            s->registered_events&=(uint16_t)~(1u<<pending.event);
        }
        memset(&s->pending[n],0,sizeof s->pending[n]); *event=result; return BTH_OK;
    }
    if (pending.kind==CAPS) {
        if (type!=12 || params<2) return BTH_ERR_PARAM;
        if (data[0]!=3 || params!=(size_t)data[1]+2) return BTH_ERR_LENGTH;
        uint16_t events=0;
        for (unsigned i=0;i<data[1];++i) if (data[i+2]>=1 && data[i+2]<=13) events|=(uint16_t)(1u<<data[i+2]);
        s->supported_events=events; s->capabilities_known=true; result.actions=BTH_AVRCP_CT_CAPABILITIES;
        memset(&s->pending[n],0,sizeof s->pending[n]); *event=result; return BTH_OK;
    }
    if (type!=15 && type!=13) return BTH_ERR_PARAM;
    const size_t expected_length=pending.event==2?9:pending.event==5?5:2;
    if (params!=expected_length) return BTH_ERR_LENGTH;
    if (data[0]!=pending.event) return BTH_ERR_PARAM;
    if ((type==13 && !pending.changed) || (type==15 && pending.changed)) return BTH_ERR_STATE;
    if (pending.event==1) {
        if (data[1]>4 && data[1]!=0xff) return BTH_ERR_PARAM;
        result.play_status=data[1];
    } else if (pending.event==2) memcpy(result.track,data+1,8);
    else if (pending.event==5) result.position=((uint32_t)data[1]<<24)|((uint32_t)data[2]<<16)|((uint32_t)data[3]<<8)|data[4];
    else result.volume=data[1]&0x7f;
    result.actions=BTH_AVRCP_CT_NOTIFICATION; result.interim=type==15;
    if (type==15) s->pending[n].changed=true;
    else { memset(&s->pending[n],0,sizeof s->pending[n]); s->registered_events&=(uint16_t)~(1u<<pending.event); }
    *event=result; return BTH_OK;
}
bth_status_t bth_avrcp_ct_receive(bth_avrcp_ct_t *s,const uint8_t *in,size_t length,bool secure,bth_avrcp_ct_event_t *event) {
    if (event) memset(event,0,sizeof(*event));
    if (!s || !s->initialized || !in || !event) return BTH_ERR_PARAM;
    if (!length) return BTH_ERR_LENGTH;
    if (!secure || !(in[0]&2)) return BTH_IGNORED;
    const uint8_t n=in[0]>>4,packet=(in[0]>>2)&3;
    const size_t header=packet==1?4:packet==0?3:1;
    if (length<header) return BTH_ERR_LENGTH;
    if (packet<2 && (in[header-2]!=0x11 || in[header-1]!=0x0e)) return BTH_IGNORED;
    if (in[0]&1) {
        if (packet || length!=3) return BTH_ERR_PARAM;
        if (!s->pending[n].kind) return BTH_IGNORED;
        event->actions=BTH_AVRCP_CT_REJECTED; event->label=n; event->status=0xff;
        memset(&s->pending[n],0,sizeof s->pending[n]); s->protocol_rejected=true; return BTH_OK;
    }
    bth_avrcp_ct_t next=*s; const uint8_t *p=in+header; size_t count=length-header;
    if (packet==1) {
        if (in[1]<2 || !count) return BTH_ERR_LENGTH;
        if (count>sizeof next.rx) return BTH_ERR_CAPACITY;
        memcpy(next.rx,p,count); next.rx_used=(uint16_t)count; next.rx_label=n; next.rx_remaining=(uint8_t)(in[1]-1); next.rx_active=true;
        *s=next; return BTH_MORE;
    }
    if (packet>=2) {
        if (!next.rx_active || n!=next.rx_label || (packet==2 && next.rx_remaining<=1) || (packet==3 && next.rx_remaining!=1)) {
            bth_avrcp_ct_expire_assembly(s); return BTH_ERR_STATE;
        }
        if (!count) { bth_avrcp_ct_expire_assembly(s); return BTH_ERR_LENGTH; }
        if (count>sizeof next.rx-next.rx_used) { bth_avrcp_ct_expire_assembly(s); return BTH_ERR_CAPACITY; }
        memcpy(next.rx+next.rx_used,p,count); next.rx_used+=(uint16_t)count; --next.rx_remaining;
        if (packet==2) { *s=next; return BTH_MORE; }
        p=next.rx; count=next.rx_used;
    }
    next.rx_active=false; next.rx_remaining=0; next.rx_used=0;
    bth_status_t st=response(&next,n,p,count,event);
    if (st>=0) *s=next;
    return st;
}
uint16_t bth_avrcp_ct_expire(bth_avrcp_ct_t *s,uint32_t now_ms) {
    if (!s || !s->initialized) return 0;
    uint16_t expired=0; s->expired_keys=0;
    for (unsigned i=0;i<16;++i) if (s->pending[i].kind && !s->pending[i].changed && (int32_t)(now_ms-s->pending[i].deadline)>=0) {
        expired|=(uint16_t)(1u<<i);
        if (s->pending[i].kind==KEY) s->expired_keys|=(uint16_t)(1u<<i);
        if (s->pending[i].kind==NOTIFICATION) {
            s->disabled_events|=(uint16_t)(1u<<s->pending[i].event);
            s->registered_events&=(uint16_t)~(1u<<s->pending[i].event);
        }
        memset(&s->pending[i],0,sizeof s->pending[i]);
    }
    s->retired_labels|=expired; return expired;
}
void bth_avrcp_ct_expire_assembly(bth_avrcp_ct_t *s) { if (s) { s->rx_active=false; s->rx_used=0; s->rx_remaining=0; } }
