#include "bth_avrcp.h"
#include <string.h>
bool bth_avrcp_init(bth_avrcp_t *s,uint8_t volume) {
    if (!s || volume>127) return false;
    memset(s,0,sizeof(*s)); s->initialized=true; s->volume=volume; return true;
}
static bth_status_t vendor(uint8_t label,uint8_t ctype,uint8_t pdu,const uint8_t *params,size_t length,
    uint8_t *out,size_t capacity,size_t *written) {
    if (capacity<13+length) return BTH_ERR_CAPACITY;
    const uint8_t header[]={0,0x11,0x0e,ctype,0x48,0,0,0x19,0x58,pdu,0,0,0};
    memcpy(out,header,sizeof header); out[0]=(uint8_t)((label<<4)|2);
    out[11]=(uint8_t)(length>>8); out[12]=(uint8_t)length;
    if (length) memcpy(out+13,params,length);
    *written=13+length; return BTH_OK;
}
static bth_status_t echo(uint8_t label,const uint8_t *body,size_t length,uint8_t response,
    uint8_t *out,size_t capacity,size_t *written) {
    if (capacity<length+3) return BTH_ERR_CAPACITY;
    out[0]=(uint8_t)((label<<4)|2); out[1]=0x11; out[2]=0x0e;
    memcpy(out+3,body,length); out[3]=response; *written=length+3; return BTH_OK;
}
bth_status_t bth_avrcp_set_volume(bth_avrcp_t *s,uint8_t volume) {
    if (!s || !s->initialized || volume>127) return BTH_ERR_PARAM;
    if (s->volume==volume) return BTH_IGNORED;
    s->volume=volume; if (s->notification_active) s->notification_due=true; return BTH_OK;
}
static bth_status_t body(bth_avrcp_t *s,uint8_t label,const uint8_t *p,size_t n,
    uint8_t *out,size_t capacity,size_t *written,uint32_t *actions) {
    if (n<3) return BTH_ERR_LENGTH;
    if ((p[0]&0xf0) || p[0]>4) return BTH_ERR_PARAM;
    uint8_t type=p[0],opcode=p[2];
    if (opcode==0x30 || opcode==0x31) {
        if (n!=8) return BTH_ERR_LENGTH;
        if (type!=1 || p[1]!=0xff || (opcode==0x31 && p[3]!=7))
            return echo(label,p,n,8,out,capacity,written);
        if (capacity<11) return BTH_ERR_CAPACITY;
        const uint8_t response[]={0,0x11,0x0e,12,0xff,opcode,7,0x48,0xff,0xff,0xff};
        memcpy(out,response,sizeof response); out[0]=(uint8_t)((label<<4)|2); *written=sizeof response; return BTH_OK;
    }
    if (opcode==0x7c && p[1]==0x48) {
        if (n<5 || p[4]!=n-5) return BTH_ERR_LENGTH;
        const uint8_t operation=p[3]&0x7f;
        const bool supported=(operation==0x41 || operation==0x42) && p[4]==0;
        if (!supported || (type!=0 && type!=2 && type!=4)) return echo(label,p,n,8,out,capacity,written);
        bth_status_t st=echo(label,p,n,type==0?9:12,out,capacity,written);
        if (st!=BTH_OK) return st;
        if (type==0 && !(p[3]&0x80)) {
            uint8_t volume=operation==0x41?(s->volume>122?127:(uint8_t)(s->volume+5)):
                (s->volume<5?0:(uint8_t)(s->volume-5));
            if (bth_avrcp_set_volume(s,volume)==BTH_OK) *actions|=BTH_AVRCP_VOLUME;
        }
        return BTH_OK;
    }
    if (opcode!=0 || p[1]!=0x48) return echo(label,p,n,8,out,capacity,written);
    if (n<6) return BTH_ERR_LENGTH;
    if (p[3]!=0 || p[4]!=0x19 || p[5]!=0x58) return echo(label,p,n,8,out,capacity,written);
    if (n<10) return BTH_ERR_LENGTH;
    if ((p[7]&3)!=0) return BTH_ERR_PARAM;
    const size_t length=((size_t)p[8]<<8)|p[9];
    if (length!=n-10) return BTH_ERR_LENGTH;
    const uint8_t *params=p+10; uint8_t error=1,answer[5]; size_t answer_length=1; uint8_t response=10;
    bool volume_change=false,register_volume=false;
    if (p[6]==0x10) {
        if (type!=1) return echo(label,p,n,8,out,capacity,written);
        if (length==1 && (params[0]==2 || params[0]==3)) {
            answer[0]=params[0]; answer[1]=1; response=12;
            if (params[0]==2) { answer[2]=0; answer[3]=0x19; answer[4]=0x58; answer_length=5; }
            else { answer[2]=13; answer_length=3; }
        }
    } else if (p[6]==0x50) {
        if (type!=0) return echo(label,p,n,8,out,capacity,written);
        if (length==1) {
            // Bit7 is RFA; only the seven volume bits carry a value.
            response=9; answer[0]=params[0]&0x7f; volume_change=s->volume!=answer[0];
        }
    } else if (p[6]==0x31) {
        if (type!=3) return echo(label,p,n,8,out,capacity,written);
        if (length==5 && params[0]==13) {
            response=15; answer[0]=13; answer[1]=s->volume; answer_length=2; register_volume=true;
        }
    } else error=0;
    if (response==10) answer[0]=error;
    bth_status_t st=vendor(label,response,p[6],answer,answer_length,out,capacity,written);
    if (st!=BTH_OK) return st;
    // AVRCP6.13.2/6.13.3: remote absolute-volume writes do not consume the
    // registered VolumeChanged notification. Local setters still trigger it.
    if (volume_change) { s->volume=answer[0]; *actions|=BTH_AVRCP_VOLUME; }
    if (register_volume) { s->notification_label=label; s->notification_active=true; s->notification_due=false; }
    return BTH_OK;
}
bth_status_t bth_avrcp_receive(bth_avrcp_t *s,const uint8_t *in,size_t length,bool secure,
    uint8_t *out,size_t capacity,size_t *written,uint32_t *actions) {
    if (written) *written=0;
    if (actions) *actions=0;
    if (!s || !s->initialized || !in || !out || !written || !actions) return BTH_ERR_PARAM;
    if (!length) return BTH_ERR_LENGTH;
    if (!secure || (in[0]&2)) return BTH_IGNORED;
    if (in[0]&1) return BTH_ERR_PARAM;
    uint8_t label=in[0]>>4,packet=(in[0]>>2)&3;
    bth_avrcp_t next=*s; const uint8_t *p=NULL; size_t n=0;
    if (packet<2) {
        const size_t header=packet==0?3:4;
        if (length<header) return BTH_ERR_LENGTH;
        const size_t pid=header-2;
        if (in[pid]!=0x11 || in[pid+1]!=0x0e) {
            if (capacity<3) return BTH_ERR_CAPACITY;
            out[0]=(uint8_t)((label<<4)|3); out[1]=in[pid]; out[2]=in[pid+1]; *written=3; return BTH_OK;
        }
        n=length-header; p=in+header;
        if (packet==1) {
            if (in[1]<2 || !n) return BTH_ERR_LENGTH;
            if (n>sizeof next.rx) return BTH_ERR_CAPACITY;
            memcpy(next.rx,p,n); next.rx_used=(uint16_t)n; next.rx_label=label;
            next.rx_remaining=(uint8_t)(in[1]-1); next.rx_active=true; *s=next; return BTH_MORE;
        }
        next.rx_active=false; next.rx_used=next.rx_remaining=0;
    } else {
        if (!next.rx_active || label!=next.rx_label || (packet==2 && next.rx_remaining<=1) ||
            (packet==3 && next.rx_remaining!=1)) { bth_avrcp_expire(s); return BTH_ERR_STATE; }
        if (length<2) { bth_avrcp_expire(s); return BTH_ERR_LENGTH; }
        n=length-1;
        if (n>sizeof next.rx-next.rx_used) { bth_avrcp_expire(s); return BTH_ERR_CAPACITY; }
        memcpy(next.rx+next.rx_used,in+1,n); next.rx_used+=(uint16_t)n; --next.rx_remaining;
        if (packet==2) { *s=next; return BTH_MORE; }
        p=next.rx; n=next.rx_used; next.rx_active=false; next.rx_used=0;
    }
    bth_status_t st=body(&next,label,p,n,out,capacity,written,actions);
    if (st==BTH_OK) *s=next;
    return st;
}
bth_status_t bth_avrcp_notification(bth_avrcp_t *s,uint8_t *out,size_t capacity,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || !out || !written) return BTH_ERR_PARAM;
    if (!s->notification_active || !s->notification_due) return BTH_MORE;
    const uint8_t params[]={13,s->volume};
    bth_status_t st=vendor(s->notification_label,13,0x31,params,sizeof params,out,capacity,written);
    if (st==BTH_OK) s->notification_active=s->notification_due=false;
    return st;
}
void bth_avrcp_expire(bth_avrcp_t *s) { if (s) { s->rx_active=false; s->rx_remaining=0; s->rx_used=0; } }
