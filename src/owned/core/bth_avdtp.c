#include "bth_avdtp.h"
#include <string.h>

enum { BAD_LENGTH=0x11, BAD_SEID=0x12, IN_USE=0x13, NOT_IN_USE=0x14,
       BAD_CATEGORY=0x17, BAD_PAYLOAD=0x18, NOT_SUPPORTED=0x19,
       INVALID_CAPABILITY=0x1a, BAD_TRANSPORT=0x23, UNSUPPORTED=0x29, BAD_STATE=0x31 };
static const bth_avdtp_endpoint_t *endpoint(const bth_avdtp_t *s,uint8_t seid) {
    for (size_t n=0;n<s->endpoint_count;++n) if (s->endpoints[n].seid==seid) return &s->endpoints[n];
    return NULL;
}
static uint8_t seid(uint8_t value) { return value>>2; }
static bool valid_seid(uint8_t value) { return value && value<63; }
static void clear_rx(bth_avdtp_t *s) {
    s->rx_active=false; s->rx_used=0; s->rx_remaining=0; s->rx_packet_size=0;
}
static void clear_stream(bth_avdtp_t *s) {
    s->state=BTH_AVDTP_IDLE; s->local_seid=0; s->remote_seid=0; s->codec_len=0;
    memset(s->codec,0,sizeof s->codec); s->delay_selected=false; s->delay_pending=false;
    clear_rx(s);
}
bool bth_avdtp_init(bth_avdtp_t *s,const bth_avdtp_endpoint_t *p,size_t count,uint8_t *storage,size_t cap) {
    if (!s) return false;
    memset(s,0,sizeof *s);
    if (!p || !count || count>BTH_AVDTP_ENDPOINT_LIMIT || !storage || cap<40) return false;
    for (size_t n=0;n<count;++n) {
        if (!valid_seid(p[n].seid) || !p[n].validate || !p[n].codec_caps.data ||
            p[n].codec_caps.len<2 || p[n].codec_caps.len>BTH_AVDTP_CODEC_LIMIT || (p[n].codec_caps.data[0]&0xf0)) return false;
        for (size_t j=0;j<n;++j) if (p[n].seid==p[j].seid) return false;
    }
    s->endpoints=p; s->endpoint_count=count; s->rx_storage=storage; s->rx_capacity=cap; s->initialized=true;
    return true;
}
static size_t capabilities(const bth_avdtp_endpoint_t *p,const uint8_t *codec,size_t length,bool delay,uint8_t *out) {
    out[0]=1; out[1]=0; out[2]=7; out[3]=(uint8_t)length;
    memcpy(out+4,codec,length);
    if (delay && p->delay_reporting) { out[length+4]=8; out[length+5]=0; return length+6; }
    return length+4;
}
typedef struct { bth_span_t codec; bool delay; uint8_t category, error; } configuration_t;
static configuration_t configuration(bth_span_t p,bool reconfigure,const bth_avdtp_endpoint_t *ep) {
    configuration_t result={0}; bool transport=false;
    uint16_t seen=0;
    for (size_t pos=0;pos<p.len;) {
        result.category=p.data[pos];
        if (p.len-pos<2 || p.data[pos+1]>p.len-pos-2) { result.error=BAD_LENGTH; return result; }
        const uint8_t category=p.data[pos], count=p.data[pos+1];
        if (!category || category>8 || (seen&(1u<<category))) { result.error=BAD_CATEGORY; return result; }
        seen|=(uint16_t)(1u<<category);
        if (reconfigure && category!=7 && category!=4) { result.error=INVALID_CAPABILITY; return result; }
        switch (category) {
        case 1:
            if (count) { result.error=BAD_TRANSPORT; return result; }
            transport=true; break;
        case 7:
            if (count<2 || count>BTH_AVDTP_CODEC_LIMIT) { result.error=BAD_LENGTH; return result; }
            result.codec=(bth_span_t){p.data+pos+2,count};
            if ((result.codec.data[0]&0xf0)!=(ep->codec_caps.data[0]&0xf0) || result.codec.data[1]!=ep->codec_caps.data[1]) {
                result.error=UNSUPPORTED; return result;
            }
            result.error=ep->validate(result.codec,ep->codec_caps);
            if (result.error) return result;
            break;
        case 8:
            if (count) { result.error=BAD_PAYLOAD; return result; }
            if (!ep->delay_reporting) { result.error=UNSUPPORTED; return result; }
            result.delay=true; break;
        default: result.error=UNSUPPORTED; return result;
        }
        pos+=(size_t)count+2;
    }
    if (!reconfigure && !transport) { result.category=1; result.error=BAD_CATEGORY; }
    else if (!result.codec.data) { result.category=7; result.error=BAD_CATEGORY; }
    return result;
}
static size_t rejection(uint8_t signal,uint8_t category,uint8_t bad_seid,uint8_t error,uint8_t *out) {
    if (signal==3 || signal==5) { out[0]=category; out[1]=error; return 2; }
    if (signal==7 || signal==9) { out[0]=(uint8_t)(bad_seid<<2); out[1]=error; return 2; }
    out[0]=error; return 1;
}
static bth_status_t transaction(bth_avdtp_t *s,uint8_t label,uint8_t message,uint8_t signal,bth_span_t p,bool secure,
                               uint8_t *out,size_t cap,size_t *written,uint32_t *actions) {
    if (message) {
        if (!s->delay_pending || label!=s->delay_label || signal!=13) return BTH_IGNORED;
        if ((message==2 || message==1) && p.len) return BTH_ERR_LENGTH;
        if (message==3 && p.len!=1) return BTH_ERR_LENGTH;
        s->delay_pending=false; *actions=message==2?BTH_AVDTP_DELAY_ACK:BTH_AVDTP_DELAY_FAILED;
        return BTH_OK;
    }
    bth_avdtp_t next=*s; uint8_t payload[BTH_AVDTP_CODEC_LIMIT+6]; size_t reply_len=0;
    uint8_t error=0, category=0, target=p.len?seid(p.data[0]):0, reply_message=2;
    uint32_t action=0;
    const bth_avdtp_endpoint_t *ep=endpoint(s,target);
    switch (signal) {
    case 1:
        if (p.len) { error=BAD_LENGTH; break; }
        for (size_t n=0;n<s->endpoint_count;++n) {
            const uint8_t id=s->endpoints[n].seid;
            payload[reply_len++]=(uint8_t)((id<<2)|((s->state!=BTH_AVDTP_IDLE && id==s->local_seid)?2:0));
            payload[reply_len++]=8; // Audio sink endpoint.
        }
        break;
    case 2: case 12:
        if (p.len!=1) { error=BAD_LENGTH; break; }
        if (!ep) { error=BAD_SEID; break; }
        reply_len=capabilities(ep,ep->codec_caps.data,ep->codec_caps.len,signal==12,payload); break;
    case 3: case 5: {
        const bool reconfigure=signal==5;
        const size_t prefix=reconfigure?1:2;
        if (p.len<prefix) { error=BAD_LENGTH; break; }
        if (!ep || (!reconfigure && !valid_seid(seid(p.data[1])))) { error=BAD_SEID; break; }
        if (!secure) { error=BAD_STATE; break; }
        if ((!reconfigure && s->state!=BTH_AVDTP_IDLE) || (reconfigure && s->local_seid!=target)) { error=IN_USE; break; }
        if (reconfigure && s->state!=BTH_AVDTP_OPEN) { error=BAD_STATE; break; }
        const configuration_t config=configuration((bth_span_t){p.data+prefix,p.len-prefix},reconfigure,ep);
        if (config.error) { category=config.category; error=config.error; break; }
        memcpy(next.codec,config.codec.data,config.codec.len); next.codec_len=(uint8_t)config.codec.len;
        next.codec[0]&=0xf0;
        if (!reconfigure) {
            next.local_seid=target; next.remote_seid=seid(p.data[1]); next.delay_selected=config.delay;
            next.state=BTH_AVDTP_CONFIGURED;
        }
        action=BTH_AVDTP_CONFIGURED_ACTION; break;
    }
    case 4:
        if (p.len!=1) { error=BAD_LENGTH; break; }
        if (!ep) { error=BAD_SEID; break; }
        if (s->state==BTH_AVDTP_IDLE || s->local_seid!=target) { error=NOT_IN_USE; break; }
        reply_len=capabilities(ep,s->codec,s->codec_len,s->delay_selected,payload); break;
    case 6:
        if (p.len!=1) { error=BAD_LENGTH; break; }
        if (!ep || s->local_seid!=target) { error=BAD_SEID; break; }
        if (!secure || s->state!=BTH_AVDTP_CONFIGURED) { error=BAD_STATE; break; }
        next.state=BTH_AVDTP_OPENING; action=BTH_AVDTP_OPEN_TRANSPORT; break;
    case 7: case 9:
        if (!p.len) { error=BAD_LENGTH; break; }
        for (size_t n=0;n<p.len;++n) {
            target=seid(p.data[n]);
            if (!endpoint(s,target) || target!=s->local_seid) { error=BAD_SEID; break; }
            if (n) { error=BAD_PAYLOAD; break; }
        }
        if (error) break;
        if (!secure || s->state!=(signal==7?BTH_AVDTP_OPEN:BTH_AVDTP_STREAMING)) { error=BAD_STATE; break; }
        next.state=signal==7?BTH_AVDTP_STREAMING:BTH_AVDTP_OPEN;
        action=signal==7?BTH_AVDTP_START_AUDIO:BTH_AVDTP_SUSPEND_AUDIO; break;
    case 8:
        if (p.len!=1) { error=BAD_LENGTH; break; }
        if (!ep || (s->state!=BTH_AVDTP_IDLE && s->local_seid!=target)) { error=BAD_SEID; break; }
        if (!secure) { error=BAD_STATE; break; }
        if (s->state!=BTH_AVDTP_OPEN && s->state!=BTH_AVDTP_STREAMING) { error=BAD_STATE; break; }
        next.state=BTH_AVDTP_CLOSING; action=BTH_AVDTP_CLOSE_TRANSPORT;
        break;
    case 10:
        if (p.len!=1) { error=BAD_LENGTH; break; }
        if (!ep) return BTH_IGNORED; // AVDTP 8.16.2: invalid Abort SEID gets no response.
        // Abort of another valid, inactive SEP acknowledges only that SEP.
        // It cannot stop the selected stream or change its codec configuration.
        if (s->state==BTH_AVDTP_IDLE || target!=s->local_seid) break;
        action=BTH_AVDTP_ABORT_AUDIO;
        if (s->state==BTH_AVDTP_CONFIGURED) clear_stream(&next);
        else { next.state=BTH_AVDTP_ABORTING; action|=BTH_AVDTP_CLOSE_TRANSPORT; next.delay_pending=false; }
        break;
    case 11: case 13: error=NOT_SUPPORTED; break;
    default: reply_message=1; break; // General Reject has no error payload.
    }
    if (error) { reply_message=3; reply_len=rejection(signal,category,target,error,payload); action=0; }
    if (cap<2+reply_len) return BTH_ERR_CAPACITY;
    out[0]=(uint8_t)((label<<4)|reply_message); out[1]=signal;
    if (reply_len) memcpy(out+2,payload,reply_len);
    *written=2+reply_len; *actions=action;
    if (!error) *s=next;
    return BTH_OK;
}
static bool overlap(const uint8_t *a,size_t an,const uint8_t *b,size_t bn) {
    return an && bn && ((uintptr_t)a<=(uintptr_t)b?(uintptr_t)b-(uintptr_t)a<an:(uintptr_t)a-(uintptr_t)b<bn);
}
bth_status_t bth_avdtp_receive(bth_avdtp_t *s,const uint8_t *in,size_t len,bool secure,
                              uint8_t *out,size_t cap,size_t *written,uint32_t *actions) {
    if (written) *written=0;
    if (actions) *actions=0;
    if (!s || !s->initialized || !in || !out || !written || !actions) return BTH_ERR_PARAM;
    if (!len) return BTH_ERR_LENGTH;
    if (overlap(in,len,out,cap) || overlap(in,len,s->rx_storage,s->rx_capacity) || overlap(out,cap,s->rx_storage,s->rx_capacity)) return BTH_ERR_PARAM;
    const uint8_t label=in[0]>>4, packet=(in[0]>>2)&3, message=in[0]&3;
    if (!packet) {
        if (len<2) return BTH_ERR_LENGTH;
        return transaction(s,label,message,in[1]&0x3f,(bth_span_t){in+2,len-2},secure,out,cap,written,actions);
    }
    bth_avdtp_t next=*s;
    if (packet==1) {
        if (len<4 || in[1]<2) goto bad_length;
        if (s->rx_active) return BTH_ERR_STATE;
        if (len-3>s->rx_capacity) goto too_large;
        memcpy(next.rx_storage,in+3,len-3); next.rx_used=len-3;
        next.rx_label=label; next.rx_message=message; next.rx_signal=in[2]&0x3f; next.rx_packet_size=len;
        next.rx_remaining=(uint8_t)(in[1]-1); next.rx_active=true; *s=next; return BTH_MORE;
    }
    if (!s->rx_active || label!=s->rx_label || message!=s->rx_message) goto bad_state;
    if (len<2 || (packet==3)!=(s->rx_remaining==1)) goto bad_length;
    if ((packet==2 && len!=s->rx_packet_size) || (packet==3 && len>s->rx_packet_size)) goto bad_length;
    if (len-1>s->rx_capacity-s->rx_used) goto too_large;
    memcpy(next.rx_storage+next.rx_used,in+1,len-1); next.rx_used+=len-1; --next.rx_remaining;
    if (packet==2) { *s=next; return BTH_MORE; }
    next.rx_active=false;
    bth_status_t status=transaction(&next,label,message,next.rx_signal,(bth_span_t){next.rx_storage,next.rx_used},secure,out,cap,written,actions);
    if (status==BTH_OK || status==BTH_IGNORED) { clear_rx(&next); *s=next; }
    else if (status!=BTH_ERR_CAPACITY) clear_rx(s);
    return status;
bad_length:
    clear_rx(s); return BTH_ERR_LENGTH;
too_large:
    clear_rx(s); return BTH_ERR_CAPACITY;
bad_state:
    clear_rx(s); return BTH_ERR_STATE;
}
bth_status_t bth_avdtp_media_connected(bth_avdtp_t *s) {
    if (!s || !s->initialized) return BTH_ERR_PARAM;
    if (s->state!=BTH_AVDTP_OPENING) return BTH_ERR_STATE;
    s->state=BTH_AVDTP_OPEN; return BTH_OK;
}
bth_status_t bth_avdtp_transport_closed(bth_avdtp_t *s) {
    if (!s || !s->initialized) return BTH_ERR_PARAM;
    clear_stream(s); return BTH_OK;
}
bth_status_t bth_avdtp_delay_report(bth_avdtp_t *s,uint16_t delay,uint8_t label,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || !out || !written || label>15) return BTH_ERR_PARAM;
    if (!s->delay_selected || s->state==BTH_AVDTP_IDLE || s->state==BTH_AVDTP_CLOSING || s->state==BTH_AVDTP_ABORTING || s->delay_pending) return BTH_ERR_STATE;
    if (cap<5) return BTH_ERR_CAPACITY;
    out[0]=(uint8_t)(label<<4); out[1]=13; out[2]=(uint8_t)(s->remote_seid<<2); out[3]=(uint8_t)(delay>>8); out[4]=(uint8_t)delay;
    s->delay_pending=true; s->delay_label=label; *written=5; return BTH_OK;
}
bth_status_t bth_avdtp_expire(bth_avdtp_t *s,uint8_t pending,uint32_t *actions) {
    if (actions) *actions=0;
    if (!s || !s->initialized || !actions || !pending || (pending&~3u)) return BTH_ERR_PARAM;
    if ((pending&BTH_AVDTP_EXPIRE_ASSEMBLY) && s->rx_active) {
        clear_rx(s); *actions|=BTH_AVDTP_SIGNAL_EXPIRED;
    }
    if ((pending&BTH_AVDTP_EXPIRE_DELAY) && s->delay_pending) {
        s->delay_pending=false; *actions|=BTH_AVDTP_DELAY_FAILED;
    }
    return *actions?BTH_OK:BTH_IGNORED;
}
static bool single_bit(uint8_t v) { return v && !(v&(v-1)); }
uint8_t bth_avdtp_sbc_validate(bth_span_t p,bth_span_t caps) {
    if (!p.data || !caps.data || p.len!=6 || caps.len!=6 || (p.data[0]&0xf0) || p.data[1] || (caps.data[0]&0xf0) || caps.data[1]) return UNSUPPORTED;
    if (!single_bit(p.data[2]&0xf0) || !single_bit(p.data[2]&0x0f) || !single_bit(p.data[3]&0xf0) ||
        !single_bit(p.data[3]&0x0c) || !single_bit(p.data[3]&3) || (p.data[2]&~caps.data[2]) || (p.data[3]&~caps.data[3])) return UNSUPPORTED;
    if (p.data[4]<2 || p.data[4]<caps.data[4] || p.data[5]>caps.data[5] || p.data[5]>250 || p.data[4]>p.data[5]) return UNSUPPORTED;
    const unsigned subbands=(p.data[3]&4)?8:4, multiplier=(p.data[2]&0x0c)?16:32;
    return p.data[5]>subbands*multiplier?UNSUPPORTED:0;
}
