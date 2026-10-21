#include "bth_le_gap.h"
#include <string.h>
static bool advertising_data(bth_span_t data) {
    if (data.len>31 || (data.len && !data.data)) return false;
    for (size_t i=0;i<data.len;) {
        const uint8_t bytes=data.data[i];
        if (!bytes) { for (;i<data.len;++i) if (data.data[i]) return false; return true; }
        if ((size_t)bytes+1>data.len-i) return false;
        i+=(size_t)bytes+1;
    }
    return true;
}
static bth_status_t fail(bth_le_gap_t *g,uint8_t reason,bth_status_t status) {
    g->stage=BTH_LE_FAILED; g->error=reason; g->pending=false; return status;
}
bool bth_le_gap_start(bth_le_gap_t *g,bth_cmdq_t *commands,const bth_le_gap_config_t *config) {
    if (!g) return false;
    memset(g,0,sizeof(*g));
    if (!commands || !commands->initialized || !config || config->interval_min<0x20 || config->interval_max>0x4000 ||
        config->interval_min>config->interval_max || !advertising_data(config->advertisement) || !advertising_data(config->scan_response)) return false;
    g->commands=commands; g->config=*config; g->initialized=true; return true;
}
bth_status_t bth_le_gap_next(bth_le_gap_t *g,bool want_advertising,uint32_t now_ms) {
    if (!g || !g->initialized) return BTH_ERR_PARAM;
    if (g->stage==BTH_LE_FAILED) return BTH_ERR_STATE;
    if (g->pending) return BTH_MORE;
    if (g->connected) want_advertising=false;
    if (g->stage==BTH_LE_READY && g->advertising==want_advertising) return BTH_IGNORED;
    uint16_t opcode=0x200a; uint8_t p[32]={0}; size_t length=1;
    switch (g->stage) {
    case BTH_LE_STOP: break;
    case BTH_LE_PARAMETERS:
        opcode=0x2006; length=15;
        p[0]=(uint8_t)g->config.interval_min; p[1]=(uint8_t)(g->config.interval_min>>8);
        p[2]=(uint8_t)g->config.interval_max; p[3]=(uint8_t)(g->config.interval_max>>8); p[13]=7; break;
    case BTH_LE_DATA: case BTH_LE_SCAN_DATA: {
        const bth_span_t data=g->stage==BTH_LE_DATA?g->config.advertisement:g->config.scan_response;
        opcode=g->stage==BTH_LE_DATA?0x2008:0x2009; length=32; p[0]=(uint8_t)data.len;
        if (data.len) memcpy(p+1,data.data,data.len);
        break;
    }
    case BTH_LE_DATA_LENGTH: opcode=0x2024; length=4; p[0]=0xfb; p[2]=0x48; p[3]=8; break;
    case BTH_LE_READY: p[0]=want_advertising?1:0; break;
    default: return BTH_ERR_STATE;
    }
    bth_status_t st=bth_cmdq_push(g->commands,opcode,p,length,false);
    if (st==BTH_ERR_CAPACITY) return BTH_MORE;
    if (st!=BTH_OK) return st;
    g->pending=true; g->pending_opcode=opcode; g->requested_enable=p[0]!=0;
    g->deadline=now_ms+5000; return BTH_OK;
}
bth_status_t bth_le_gap_on_event(bth_le_gap_t *g,const bth_h4_frame_t *event) {
    if (!g || !g->initialized || !event) return BTH_ERR_PARAM;
    if (g->stage==BTH_LE_FAILED) return BTH_ERR_STATE;
    if (event->type!=BTH_H4_EVENT) return BTH_IGNORED;
    bth_span_t p=event->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    if (event->event.code==0x10) return fail(g,p.len==1?p.data[0]:0xff,BTH_ERR_STATE);
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
    if (p.data[3] && !(g->pending_opcode==0x200a && g->connected && p.data[3]==0x0c)) return fail(g,p.data[3],BTH_ERR_STATE);
    g->pending=false;
    if (g->stage==BTH_LE_READY) g->advertising=g->requested_enable && !g->connected;
    else {
        if (g->stage==BTH_LE_STOP) g->advertising=false;
        g->stage=(bth_le_gap_stage_t)(g->stage+1);
        if (g->stage==BTH_LE_DATA_LENGTH && !g->config.data_length_extension) g->stage=BTH_LE_READY;
    }
    return BTH_OK;
}
void bth_le_gap_connection(bth_le_gap_t *g,bool connected) { if (g && g->initialized) { g->connected=connected; g->advertising=false; } }
bth_status_t bth_le_gap_peer_event(bth_le_gap_t *g,uint32_t owner,uint16_t handle,const bth_h4_frame_t *event) {
    if (!g || !g->initialized || !event || handle>0x0eff) return BTH_ERR_PARAM;
    if (!owner || event->type!=BTH_H4_EVENT || event->event.code!=0x3e) return BTH_IGNORED;
    const bth_span_t p=event->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    if (!p.len || (p.data[0]!=5 && p.data[0]!=6)) return BTH_IGNORED;
    if (p.len!=(p.data[0]==5?13u:11u)) return BTH_ERR_LENGTH;
    if (bth_read_le16(p.data+1)!=handle) return BTH_IGNORED;
    if (p.data[0]==5) return bth_cmdq_push_owned(g->commands,0x201b,p.data+1,2,true,owner);
    const uint16_t minimum=bth_read_le16(p.data+3),maximum=bth_read_le16(p.data+5);
    const uint16_t latency=bth_read_le16(p.data+7),timeout=bth_read_le16(p.data+9);
    // Interval units are1.25ms; timeout units are10ms. Strict inequality is
    // required: supervision timeout >2*(latency+1)*maximum interval.
    const bool valid=minimum>=6 && maximum<=3200 && minimum<=maximum && latency<=499 &&
        timeout>=10 && timeout<=3200 && (uint32_t)timeout*4>((uint32_t)latency+1)*maximum;
    uint8_t reply[14]={0}; memcpy(reply,p.data+1,valid?10:2);
    if (!valid) reply[2]=0x3b;
    return bth_cmdq_push_owned(g->commands,valid?0x2020:0x2021,reply,valid?14:3,true,owner);
}
bth_status_t bth_le_gap_expire(bth_le_gap_t *g,uint32_t now_ms) {
    if (!g || !g->initialized) return BTH_ERR_PARAM;
    if (g->stage==BTH_LE_FAILED) return BTH_ERR_STATE;
    if (!g->pending) return BTH_IGNORED;
    if ((int32_t)(now_ms-g->deadline)<0) return BTH_MORE;
    return fail(g,0xff,BTH_ERR_STATE);
}
