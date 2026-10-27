#include "bth_gap.h"
#include <string.h>
static const uint16_t opcodes[]={0x0c1a,0x0c01,0x2001,0x0c33,0x0c31,0x0c56,0x0c13,0x0c24,0x0c45,0x0c52};
static void event_bit(uint8_t *mask,unsigned event) { mask[(event-1)/8]|=(uint8_t)(1u<<((event-1)%8)); }
bool bth_gap_start(bth_gap_t *g,const bth_gap_config_t *c) {
    if (!g || !c || !c->name.data || !c->name.len || c->name.len>248 ||
        c->host_acl_length<1021 || !c->host_acl_packets || c->class_of_device>0xffffff ||
        (c->class_of_device&3) || c->uuid_count>BTH_GAP_UUID_LIMIT || (c->uuid_count && !c->service_uuids)) return false;
    memset(g,0,sizeof(*g)); g->config=*c; g->initialized=true; return true;
}
static size_t parameters(const bth_gap_t *g,uint8_t *p) {
    memset(p,0,248);
    const bth_gap_config_t *c=&g->config;
    switch (g->stage) {
    case BTH_GAP_SCAN_OFF: return 1;
    case BTH_GAP_EVENT_MASK: {
        static const uint8_t events[]={3,4,5,6,7,8,9,0x0b,0x0e,0x0f,0x10,0x12,0x13,0x14,0x16,0x17,0x18,0x1a,0x1b,0x1d,
            0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x3b,0x3c,0x3d,0x3e};
        for (unsigned i=0;i<sizeof(events);++i) event_bit(p,events[i]);
        return 8;
    }
    case BTH_GAP_LE_MASK: {
        static const uint8_t events[]={1,3,4,5,6,7,10};
        for (unsigned i=0;i<sizeof(events);++i) event_bit(p,events[i]);
        if(c->le_ecc_events){event_bit(p,8);event_bit(p,9);}
        return 8;
    }
    case BTH_GAP_HOST_BUFFERS:
        p[0]=(uint8_t)c->host_acl_length; p[1]=(uint8_t)(c->host_acl_length>>8);
        p[2]=BTH_GAP_SYNC_BYTES;
        p[3]=(uint8_t)c->host_acl_packets; p[4]=(uint8_t)(c->host_acl_packets>>8);
        p[5]=BTH_GAP_SYNC_PACKETS; return 7;
    case BTH_GAP_RX_FLOW: case BTH_GAP_SSP: p[0]=1; return 1;
    case BTH_GAP_NAME: memcpy(p,c->name.data,c->name.len); return 248;
    case BTH_GAP_CLASS:
        p[0]=(uint8_t)c->class_of_device; p[1]=(uint8_t)(c->class_of_device>>8); p[2]=(uint8_t)(c->class_of_device>>16); return 3;
    case BTH_GAP_INQUIRY_MODE: p[0]=2; return 1;
    case BTH_GAP_EIR: {
        size_t pos=1; // p[0] FEC not required, then 240 bytes of EIR data.
        if (c->uuid_count) {
            p[pos++]=(uint8_t)(1+2*c->uuid_count); p[pos++]=3;
            for (unsigned i=0;i<c->uuid_count;++i) {
                p[pos++]=(uint8_t)c->service_uuids[i]; p[pos++]=(uint8_t)(c->service_uuids[i]>>8);
            }
        }
        size_t n=c->name.len;
        if (n>241-pos-2) {
            n=241-pos-2;
            // Avoid cutting a UTF-8 codepoint in the shortened local name.
            while (n && (c->name.data[n]&0xc0)==0x80) --n;
        }
        p[pos++]=(uint8_t)(n+1); p[pos++]=n==c->name.len?9:8;
        memcpy(p+pos,c->name.data,n); return 241;
    }
    default: return 0;
    }
}
static bth_status_t send(bth_gap_t *g,bth_hci_flow_t *flow,uint16_t opcode,const uint8_t *p,size_t len,
    uint8_t *out,size_t cap,size_t *written) {
    if (!flow->command_credits) return BTH_MORE;
    bth_status_t st=bth_hci_command_encode(out,cap,opcode,p,len,written);
    if (st!=BTH_OK) return st;
    if (!bth_hci_flow_take_command(flow)) { *written=0; return BTH_ERR_STATE; }
    g->pending=true; g->pending_opcode=opcode; return BTH_OK;
}
bth_status_t bth_gap_next_command(bth_gap_t *g,bth_hci_flow_t *flow,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!g || !g->initialized || !flow || !out || !written) return BTH_ERR_PARAM;
    if (g->stage==BTH_GAP_FAILED) return BTH_ERR_STATE;
    if (g->pending) return BTH_MORE;
    if (g->stage==BTH_GAP_READY) return BTH_IGNORED;
    for (unsigned i=0;i<BTH_HCI_LINK_LIMIT;++i) if (flow->links[i].active) return BTH_ERR_STATE;
    uint8_t p[248]; size_t len=parameters(g,p);
    return send(g,flow,opcodes[g->stage],p,len,out,cap,written);
}
static bth_status_t fail(bth_gap_t *g,uint8_t error,bth_status_t status) {
    g->stage=BTH_GAP_FAILED; g->error=error; g->pending=false; return status;
}
bth_status_t bth_gap_on_event(bth_gap_t *g,const bth_h4_frame_t *event) {
    if (!g || !g->initialized || !event || (event->type==BTH_H4_EVENT && event->event.params.len && !event->event.params.data)) return BTH_ERR_PARAM;
    if (g->stage==BTH_GAP_FAILED) return BTH_ERR_STATE;
    if (event->type!=BTH_H4_EVENT) return BTH_IGNORED;
    bth_span_t p=event->event.params;
    if (event->event.code==0x10) return fail(g,p.len==1?p.data[0]:0xff,p.len==1?BTH_ERR_STATE:BTH_ERR_LENGTH);
    if (!g->pending) return BTH_IGNORED;
    if (event->event.code==0x0f) {
        if (p.len!=4) return fail(g,0xff,BTH_ERR_LENGTH);
        if (bth_read_le16(p.data+2)!=g->pending_opcode) return BTH_IGNORED;
        return p.data[0]?fail(g,p.data[0],BTH_ERR_STATE):BTH_MORE;
    }
    if (event->event.code!=0x0e) return BTH_IGNORED;
    if (p.len<3) return fail(g,0xff,BTH_ERR_LENGTH);
    if (bth_read_le16(p.data+1)!=g->pending_opcode) return BTH_IGNORED;
    if (p.len!=4) return fail(g,0xff,BTH_ERR_LENGTH);
    if (p.data[3]) return fail(g,p.data[3],BTH_ERR_STATE);
    g->pending=false;
    if (g->scan_update) { g->scan=g->desired_scan; g->scan_update=false; }
    else g->stage=(bth_gap_stage_t)(g->stage+1);
    return BTH_OK;
}
bth_status_t bth_gap_set_scan(bth_gap_t *g,bth_hci_flow_t *flow,bool ready,uint8_t scan,
    uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!g || !g->initialized || !flow || !out || !written || scan>3) return BTH_ERR_PARAM;
    if (g->stage!=BTH_GAP_READY || (scan && !ready)) return BTH_ERR_STATE;
    if (g->pending) return BTH_MORE;
    bth_status_t st=send(g,flow,0x0c1a,&scan,1,out,cap,written);
    if (st==BTH_OK) { g->scan_update=true; g->desired_scan=scan; }
    return st;
}
bth_status_t bth_gap_expire(bth_gap_t *g) {
    if (!g || !g->initialized) return BTH_ERR_PARAM;
    if (!g->pending) return BTH_IGNORED;
    return fail(g,0xff,BTH_ERR_STATE);
}
