#include "bth_l2cap_sig.h"
#include <string.h>

typedef struct { uint8_t *data; size_t capacity, used; } writer_t;
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static uint8_t *command(writer_t *w,uint8_t code,uint8_t id,size_t len) {
    if (len>BTH_SIGNAL_MTU-4 || w->capacity-w->used<len+4) return NULL;
    uint8_t *p=w->data+w->used; w->used+=len+4;
    p[0]=code; p[1]=id; put16(p+2,(uint16_t)len); return p+4;
}
static bth_status_t reject(writer_t *w,uint8_t id,uint16_t reason,uint16_t dcid,uint16_t scid) {
    uint8_t *p=command(w,1,id,reason==2?6:2);
    if (!p) return BTH_ERR_CAPACITY;
    put16(p,reason);
    if (reason==2) { put16(p+2,dcid); put16(p+4,scid); }
    return BTH_OK;
}
static const bth_l2cap_service_t *service(const bth_l2cap_sig_t *s,uint16_t psm) {
    for (unsigned i=0;i<s->service_count;++i) if (s->services[i].psm==psm) return &s->services[i];
    return NULL;
}
static bth_l2cap_channel_t *channel(bth_l2cap_sig_t *s,uint16_t cid) {
    if (cid<0x40 || cid>=0x40+BTH_CHANNEL_LIMIT) return NULL;
    bth_l2cap_channel_t *c=&s->channels[cid-0x40];
    return c->state!=BTH_CHANNEL_CLOSED?c:NULL;
}
const bth_l2cap_channel_t *bth_l2cap_sig_find(const bth_l2cap_sig_t *s,uint16_t cid) {
    if (!s || !s->initialized || cid<0x40 || cid>=0x40+BTH_CHANNEL_LIMIT) return NULL;
    const bth_l2cap_channel_t *c=&s->channels[cid-0x40];
    return c->state!=BTH_CHANNEL_CLOSED?c:NULL;
}
static bth_l2cap_channel_t *free_channel(bth_l2cap_sig_t *s) {
    for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) if (s->channels[i].state==BTH_CHANNEL_CLOSED) return &s->channels[i];
    return NULL;
}
static bool remote_used(const bth_l2cap_sig_t *s,uint16_t cid) {
    for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i)
        if (s->channels[i].state!=BTH_CHANNEL_CLOSED && s->channels[i].remote_cid==cid) return true;
    return false;
}
static uint8_t identifier(bth_l2cap_sig_t *s) {
    for (;;) {
        const uint8_t id=s->next_id++;
        if (!s->next_id) s->next_id=1;
        bool used=false;
        for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) {
            const bth_l2cap_channel_t *c=&s->channels[i];
            if (c->state!=BTH_CHANNEL_CLOSED && (c->connect_id==id || c->config_id==id || c->disconnect_id==id)) used=true;
        }
        if (id && !used) return id;
    }
}
static void make_channel(bth_l2cap_sig_t *s,bth_l2cap_channel_t *c,const bth_l2cap_service_t *p) {
    const uint16_t cid=(uint16_t)(0x40+(c-s->channels));
    memset(c,0,sizeof *c); c->local_cid=cid; c->psm=p->psm;
    c->local_mtu=p->receive_mtu; c->remote_mtu=672;
}
bool bth_l2cap_sig_init(bth_l2cap_sig_t *s,uint16_t handle,const bth_l2cap_service_t *p,size_t count) {
    if (!s) return false;
    memset(s,0,sizeof *s);
    if (handle>0x0eff || !p || !count || count>BTH_SERVICE_LIMIT) return false;
    for (size_t i=0;i<count;++i) {
        if (!(p[i].psm&1) || (p[i].psm&0x100) || p[i].receive_mtu<48) return false;
        for (size_t j=0;j<i;++j) if (p[i].psm==p[j].psm) return false;
    }
    memcpy(s->services,p,count*sizeof *p); s->service_count=(uint8_t)count;
    s->handle=handle; s->next_id=1; s->initialized=true; return true;
}
static bth_status_t configure(bth_l2cap_sig_t *s,bth_l2cap_channel_t *c,writer_t *w,bool empty) {
    const uint8_t id=identifier(s);
    uint8_t *p=command(w,4,id,empty?4:8);
    if (!p) return BTH_ERR_CAPACITY;
    put16(p,c->remote_cid); put16(p+2,0);
    if (!empty) { p[4]=1; p[5]=2; put16(p+6,c->local_mtu); }
    c->config_id=id; c->local_configured=false; c->state=BTH_CHANNEL_CONFIGURING;
    return BTH_OK;
}
static void open_if_configured(bth_l2cap_channel_t *c) {
    if (c->local_configured && c->remote_configured) c->state=BTH_CHANNEL_OPEN;
}
static bth_status_t connect_response(writer_t *w,uint8_t id,uint16_t local,uint16_t remote,uint16_t result,uint16_t status) {
    uint8_t *p=command(w,3,id,8); if (!p) return BTH_ERR_CAPACITY;
    put16(p,local); put16(p+2,remote); put16(p+4,result); put16(p+6,status); return BTH_OK;
}
static bth_status_t connection_request(bth_l2cap_sig_t *s,uint8_t id,bth_span_t p,bool secure,writer_t *w) {
    if (p.len!=4) return BTH_ERR_LENGTH;
    const uint16_t psm=bth_read_le16(p.data), remote=bth_read_le16(p.data+2);
    const bth_l2cap_service_t *svc=service(s,psm);
    bth_l2cap_channel_t *c=free_channel(s);
    uint16_t failure=!svc?2:(remote<0x40?6:(remote_used(s,remote)?7:(!c?4:0)));
    if (failure) return connect_response(w,id,0,remote,failure,0);
    make_channel(s,c,svc); c->remote_cid=remote;
    const bool waiting=svc->secure && !secure;
    c->state=waiting?BTH_CHANNEL_WAIT_SECURITY:BTH_CHANNEL_CONFIGURING;
    c->connect_id=waiting?id:0;
    bth_status_t result=connect_response(w,id,c->local_cid,remote,waiting?1:0,waiting?1:0);
    return result!=BTH_OK || waiting?result:configure(s,c,w,false);
}
static bth_status_t disconnect_channel(bth_l2cap_sig_t *s,bth_l2cap_channel_t *c,writer_t *w) {
    const uint8_t id=identifier(s); uint8_t *p=command(w,6,id,4);
    if (!p) return BTH_ERR_CAPACITY;
    put16(p,c->remote_cid); put16(p+2,c->local_cid);
    c->disconnect_id=id; c->connect_id=c->config_id=0; c->cancel_connect=false; c->state=BTH_CHANNEL_DISCONNECTING;
    return BTH_OK;
}
static bth_status_t connection_response(bth_l2cap_sig_t *s,uint8_t id,bth_span_t p,writer_t *w) {
    if (p.len!=8) return BTH_ERR_LENGTH;
    bth_l2cap_channel_t *c=channel(s,bth_read_le16(p.data+2));
    if (!c || c->state!=BTH_CHANNEL_CONNECTING || c->connect_id!=id) return BTH_OK;
    const uint16_t result=bth_read_le16(p.data+4), remote=bth_read_le16(p.data);
    if (result==1) return BTH_OK;
    if (result) { memset(c,0,sizeof *c); return BTH_OK; }
    if (remote<0x40 || remote_used(s,remote) || bth_read_le16(p.data+6)) return BTH_ERR_PARAM;
    c->remote_cid=remote; c->connect_id=0;
    return c->cancel_connect?disconnect_channel(s,c,w):configure(s,c,w,false);
}
// Parse complete options. Request-side unknown mandatory options are echoed;
// hints are ignored. Basic-mode RFC is accepted; enhanced modes are negotiated
// down explicitly rather than advertised as implemented.
static bth_status_t options(bth_span_t p,uint16_t *mtu,uint16_t *result,writer_t *unknown) {
    for (size_t pos=0;pos<p.len;) {
        if (p.len-pos<2 || p.len-pos-2<p.data[pos+1]) return BTH_ERR_LENGTH;
        const uint8_t raw=p.data[pos], type=raw&0x7f, n=p.data[pos+1];
        const uint8_t *data=p.data+pos+2;
        if (type==1) {
            if (n!=2) return BTH_ERR_LENGTH;
            const uint16_t value=bth_read_le16(data);
            if (value<48) { if (*result!=3) *result=1; }
            else *mtu=value;
        } else if (type==2) { if (n!=2) return BTH_ERR_LENGTH; }
        else if (type==4) {
            if (n!=9) return BTH_ERR_LENGTH;
            if (data[0]!=0 && *result!=3) *result=1;
        }
        else if (!(raw&0x80)) {
            *result=3;
            if (unknown) {
                if (unknown->capacity-unknown->used<(size_t)n+2) return BTH_ERR_CAPACITY;
                memcpy(unknown->data+unknown->used,p.data+pos,(size_t)n+2); unknown->used+=(size_t)n+2;
            }
        }
        pos+=(size_t)n+2;
    }
    return BTH_OK;
}
static bth_status_t configuration_request(bth_l2cap_sig_t *s,uint8_t id,bth_span_t p,writer_t *w) {
    if (p.len<4) return BTH_ERR_LENGTH;
    const uint16_t dcid=bth_read_le16(p.data), flags=bth_read_le16(p.data+2)&1;
    bth_l2cap_channel_t *c=channel(s,dcid);
    if (!c || (c->state!=BTH_CHANNEL_CONFIGURING && c->state!=BTH_CHANNEL_OPEN)) return reject(w,id,2,dcid,0);
    uint16_t mtu=c->config_continuation?c->pending_remote_mtu:c->remote_mtu, result=0;
    // Reserve response header, collect only the unrecognized mandatory options.
    uint8_t *reply=command(w,5,id,6); if (!reply) return BTH_ERR_CAPACITY;
    writer_t unknown={w->data+w->used,w->capacity-w->used,0};
    const bth_status_t parsed=options((bth_span_t){p.data+4,p.len-4},&mtu,&result,&unknown);
    if (parsed!=BTH_OK) return parsed;
    if (unknown.used>BTH_SIGNAL_MTU-10) return BTH_ERR_CAPACITY;
    size_t extra=0;
    if (result==3) extra=unknown.used;
    else if (result==1) {
        for (size_t pos=4;pos<p.len;pos+=(size_t)p.data[pos+1]+2) {
            const uint8_t type=p.data[pos]&0x7f;
            const bool small_mtu=type==1 && bth_read_le16(p.data+pos+2)<48;
            const bool enhanced=type==4 && p.data[pos+2]!=0;
            const size_t count=small_mtu?4:(enhanced?11:0);
            if (!count) continue;
            if (w->capacity-w->used-extra<count || extra+count>BTH_SIGNAL_MTU-10) return BTH_ERR_CAPACITY;
            uint8_t *option=w->data+w->used+extra;
            memset(option,0,count); option[0]=type; option[1]=(uint8_t)(count-2);
            if (small_mtu) put16(option+2,48);
            extra+=count;
        }
    }
    put16(reply,c->remote_cid); put16(reply+2,flags); put16(reply+4,result);
    put16(reply-2,(uint16_t)(6+extra)); w->used+=extra;
    // A rejected reconfiguration leaves the existing OPEN channel unchanged.
    if (c->state==BTH_CHANNEL_OPEN && result) return BTH_OK;
    c->state=BTH_CHANNEL_CONFIGURING;
    if (!c->config_continuation) c->config_error=0;
    if (result) c->config_error=result;
    c->pending_remote_mtu=mtu; c->config_continuation=flags!=0;
    if (!flags) {
        c->remote_configured=c->config_error==0;
        if (c->remote_configured) c->remote_mtu=mtu;
        c->config_error=0; open_if_configured(c);
    } else c->remote_configured=false;
    return BTH_OK;
}
static bth_status_t configuration_response(bth_l2cap_sig_t *s,uint8_t id,bth_span_t p,writer_t *w) {
    if (p.len<6) return BTH_ERR_LENGTH;
    bth_l2cap_channel_t *c=channel(s,bth_read_le16(p.data));
    if (!c || c->config_id!=id || c->state!=BTH_CHANNEL_CONFIGURING) return BTH_OK;
    const uint16_t result=bth_read_le16(p.data+4);
    uint16_t mtu=c->local_mtu, invalid=0;
    bth_status_t status=options((bth_span_t){p.data+6,p.len-6},&mtu,&invalid,NULL);
    if (status!=BTH_OK) return status;
    if (mtu>c->local_mtu || invalid) { memset(c,0,sizeof *c); return BTH_OK; }
    if (result==4) return BTH_OK;
    if (result==1 && c->retries++<2) { c->local_mtu=mtu; return configure(s,c,w,false); }
    if (result) { memset(c,0,sizeof *c); return BTH_OK; }
    c->local_mtu=mtu;
    if (bth_read_le16(p.data+2)&1) return configure(s,c,w,true);
    c->local_configured=true; c->config_id=0; open_if_configured(c); return BTH_OK;
}
static bth_status_t disconnection_request(bth_l2cap_sig_t *s,uint8_t id,bth_span_t p,writer_t *w) {
    if (p.len!=4) return BTH_ERR_LENGTH;
    const uint16_t dcid=bth_read_le16(p.data), scid=bth_read_le16(p.data+2);
    bth_l2cap_channel_t *c=channel(s,dcid);
    if (!c || c->remote_cid!=scid) return reject(w,id,2,dcid,scid);
    uint8_t *reply=command(w,7,id,4); if (!reply) return BTH_ERR_CAPACITY;
    memcpy(reply,p.data,4); memset(c,0,sizeof *c); return BTH_OK;
}
static bth_status_t information_request(uint8_t id,bth_span_t p,writer_t *w) {
    if (p.len!=2) return BTH_ERR_LENGTH;
    const uint16_t type=bth_read_le16(p.data);
    const size_t extra=type==2?4:(type==3?8:0);
    uint8_t *reply=command(w,11,id,4+extra); if (!reply) return BTH_ERR_CAPACITY;
    put16(reply,type); put16(reply+2,extra?0:1);
    if (extra) { memset(reply+4,0,extra); reply[4]=type==2?0x80:2; }
    return BTH_OK;
}
static bth_status_t process_command(bth_l2cap_sig_t *s,uint8_t code,uint8_t id,bth_span_t p,bool secure,writer_t *w) {
    switch (code) {
    case 1: {
        if (p.len<2) return BTH_ERR_LENGTH;
        const uint16_t reason=bth_read_le16(p.data);
        if (reason>2) return BTH_ERR_PARAM;
        if (p.len!=(reason==0?2u:(reason==1?4u:6u))) return BTH_ERR_LENGTH;
        if (reason==1 && bth_read_le16(p.data+2)<48) return BTH_ERR_PARAM;
        for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) {
            bth_l2cap_channel_t *c=&s->channels[i];
            if (reason==2) {
                const uint16_t remote=bth_read_le16(p.data+2), local=bth_read_le16(p.data+4);
                if (c->config_id==id) { if (remote!=c->remote_cid || local) continue; }
                else if (remote!=c->remote_cid || local!=c->local_cid) continue;
            }
            if ((c->state==BTH_CHANNEL_CONNECTING && c->connect_id==id) || c->config_id==id || c->disconnect_id==id)
                memset(c,0,sizeof *c);
        }
        return BTH_OK;
    }
    case 2: return connection_request(s,id,p,secure,w);
    case 3: return connection_response(s,id,p,w);
    case 4: return configuration_request(s,id,p,w);
    case 5: return configuration_response(s,id,p,w);
    case 6: return disconnection_request(s,id,p,w);
    case 7: {
        if (p.len!=4) return BTH_ERR_LENGTH;
        bth_l2cap_channel_t *c=channel(s,bth_read_le16(p.data+2));
        if (c && c->state==BTH_CHANNEL_DISCONNECTING && c->disconnect_id==id && c->remote_cid==bth_read_le16(p.data)) memset(c,0,sizeof *c);
        return BTH_OK;
    }
    case 8: {
        uint8_t *reply=command(w,9,id,p.len); if (!reply) return BTH_ERR_CAPACITY;
        if (p.len) memcpy(reply,p.data,p.len);
        return BTH_OK;
    }
    case 9: return BTH_OK;
    case 10: return information_request(id,p,w);
    case 11: return p.len>=4?BTH_OK:BTH_ERR_LENGTH;
    default: return reject(w,id,0,0,0);
    }
}
static bool valid_output(const bth_l2cap_sig_t *s,uint8_t *out,size_t capacity,size_t *written) {
    if (written) *written=0;
    return s && s->initialized && written && (out || !capacity);
}
bth_status_t bth_l2cap_sig_process(bth_l2cap_sig_t *s,const uint8_t *in,size_t len,bool secure,
                                  uint8_t *out,size_t capacity,size_t *written) {
    if (!valid_output(s,out,capacity,written) || (!in && len)) return BTH_ERR_PARAM;
    if (len && capacity && ((uintptr_t)in<=(uintptr_t)out?(uintptr_t)out-(uintptr_t)in<len:(uintptr_t)in-(uintptr_t)out<capacity)) return BTH_ERR_PARAM;
    bth_l2cap_sig_t next=*s; writer_t w={out,capacity,0};
    if (len>BTH_SIGNAL_MTU) {
        if (!in[1]) return BTH_ERR_PARAM;
        uint8_t *p=command(&w,1,in[1],4); if (!p) return BTH_ERR_CAPACITY;
        put16(p,1); put16(p+2,BTH_SIGNAL_MTU); *written=w.used; return BTH_OK;
    }
    for (size_t pos=0;pos<len;) {
        if (len-pos<4) return BTH_ERR_LENGTH;
        const size_t n=bth_read_le16(in+pos+2);
        if (n>len-pos-4) return BTH_ERR_LENGTH;
        if (!in[pos+1]) return BTH_ERR_PARAM;
        bth_status_t result=process_command(&next,in[pos],in[pos+1],(bth_span_t){in+pos+4,n},secure,&w);
        if (result!=BTH_OK) return result;
        pos+=n+4;
    }
    *s=next; *written=w.used; return BTH_OK;
}
bth_status_t bth_l2cap_sig_security_ready(bth_l2cap_sig_t *s,uint8_t *out,size_t capacity,size_t *written) {
    if (!valid_output(s,out,capacity,written)) return BTH_ERR_PARAM;
    bth_l2cap_sig_t next=*s; writer_t w={out,capacity,0};
    for (unsigned i=0;i<BTH_CHANNEL_LIMIT;++i) {
        bth_l2cap_channel_t *c=&next.channels[i];
        if (c->state!=BTH_CHANNEL_WAIT_SECURITY) continue;
        bth_status_t result=connect_response(&w,c->connect_id,c->local_cid,c->remote_cid,0,0);
        if (result!=BTH_OK) return result;
        c->connect_id=0; result=configure(&next,c,&w,false); if (result!=BTH_OK) return result;
    }
    *s=next; *written=w.used; return BTH_OK;
}
bth_status_t bth_l2cap_sig_connect(bth_l2cap_sig_t *s,uint16_t psm,bool secure,
                                  uint8_t *out,size_t capacity,size_t *written,uint16_t *local_cid) {
    if (local_cid) *local_cid=0;
    if (!valid_output(s,out,capacity,written) || !local_cid) return BTH_ERR_PARAM;
    const bth_l2cap_service_t *svc=service(s,psm);
    if (!svc) return BTH_UNSUPPORTED;
    if (svc->secure && !secure) return BTH_ERR_STATE;
    bth_l2cap_sig_t next=*s; bth_l2cap_channel_t *c=free_channel(&next);
    if (!c) return BTH_ERR_CAPACITY;
    make_channel(&next,c,svc); c->connect_id=identifier(&next); c->state=BTH_CHANNEL_CONNECTING;
    writer_t w={out,capacity,0}; uint8_t *p=command(&w,2,c->connect_id,4);
    if (!p) return BTH_ERR_CAPACITY;
    put16(p,psm); put16(p+2,c->local_cid);
    *local_cid=c->local_cid; *s=next; *written=w.used; return BTH_OK;
}
bth_status_t bth_l2cap_sig_disconnect(bth_l2cap_sig_t *s,uint16_t local_cid,
                                     uint8_t *out,size_t capacity,size_t *written) {
    if (!valid_output(s,out,capacity,written)) return BTH_ERR_PARAM;
    bth_l2cap_sig_t next=*s; bth_l2cap_channel_t *c=channel(&next,local_cid);
    if (!c || c->state==BTH_CHANNEL_CONNECTING || c->state==BTH_CHANNEL_WAIT_SECURITY || c->state==BTH_CHANNEL_DISCONNECTING) return BTH_ERR_STATE;
    writer_t w={out,capacity,0}; bth_status_t st=disconnect_channel(&next,c,&w); if (st!=BTH_OK) return st;
    *s=next; *written=w.used; return BTH_OK;
}
bth_status_t bth_l2cap_sig_cancel(bth_l2cap_sig_t *s,uint16_t local_cid,uint8_t *out,size_t capacity,size_t *written) {
    if (!valid_output(s,out,capacity,written)) return BTH_ERR_PARAM;
    bth_l2cap_sig_t next=*s; bth_l2cap_channel_t *c=channel(&next,local_cid);
    if (!c || c->state==BTH_CHANNEL_DISCONNECTING) return BTH_IGNORED;
    writer_t w={out,capacity,0}; bth_status_t st=BTH_OK;
    if (c->state==BTH_CHANNEL_CONNECTING) {
        if (c->cancel_connect) return BTH_IGNORED;
        c->cancel_connect=true;
    } else if (c->state==BTH_CHANNEL_WAIT_SECURITY) {
        if (!c->connect_id) return BTH_ERR_STATE;
        st=connect_response(&w,c->connect_id,0,c->remote_cid,4,0);
        if (st==BTH_OK) memset(c,0,sizeof *c);
    } else st=disconnect_channel(&next,c,&w);
    if (st!=BTH_OK) return st;
    *s=next; *written=w.used; return BTH_OK;
}
