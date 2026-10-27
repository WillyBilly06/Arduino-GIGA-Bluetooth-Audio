#include "bth_capabilities.h"
#include <string.h>
static const uint16_t opcodes[]={0x1002,0x1004,0x0c56,0x0c6d,0x1004,0x1004,0x0c7a,0x1004,0x2003,0x201c};
void bth_capabilities_start(bth_capabilities_t *c,uint8_t version) {
    bth_capabilities_start_with_policy(c,version,true);
}
void bth_capabilities_start_with_policy(bth_capabilities_t *c,uint8_t version,bool classic_sc_allowed) {
    if (!c) return;
    memset(c,0,sizeof(*c)); c->initialized=true; c->hci_version=version;
    c->classic_sc_allowed=classic_sc_allowed;
}
static bth_status_t fail(bth_capabilities_t *c,uint8_t error,bth_status_t st) {
    c->stage=BTH_CAP_FAILED; c->error=error; c->pending=false; return st;
}
static uint8_t page(bth_cap_stage_t stage) {
    return stage==BTH_CAP_CLASSIC0?0:stage==BTH_CAP_CLASSIC2?2:1;
}
bth_status_t bth_capabilities_next(bth_capabilities_t *c,bth_hci_flow_t *flow,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!c || !c->initialized || !flow || !out || !written) return BTH_ERR_PARAM;
    if (c->stage>=BTH_CAP_FAILED) return BTH_ERR_STATE;
    if (c->pending) return BTH_MORE;
    if (c->stage==BTH_CAP_READY) return BTH_IGNORED;
    for (unsigned i=0;i<BTH_HCI_LINK_LIMIT;++i) if (flow->links[i].active) return BTH_ERR_STATE;
    if (!flow->command_credits) return BTH_MORE;
    uint8_t params[2]={0}; size_t length=0; uint16_t opcode=opcodes[c->stage];
    if (opcode==0x1004) { params[0]=page(c->stage); length=1; }
    else if (opcode==0x0c56 || opcode==0x0c7a) { params[0]=1; length=1; }
    else if (opcode==0x0c6d) { params[0]=1; params[1]=c->simultaneous_host?1:0; length=2; }
    bth_status_t st=bth_hci_command_encode(out,cap,opcode,params,length,written);
    if (st!=BTH_OK) return st;
    if (!bth_hci_flow_take_command(flow)) { *written=0; return BTH_ERR_STATE; }
    c->pending=true; c->pending_opcode=opcode; return BTH_OK;
}
bth_status_t bth_capabilities_on_event(bth_capabilities_t *c,const bth_h4_frame_t *event) {
    if (!c || !c->initialized || !event) return BTH_ERR_PARAM;
    if (c->stage>=BTH_CAP_FAILED) return BTH_ERR_STATE;
    if (event->type!=BTH_H4_EVENT) return BTH_IGNORED;
    bth_span_t p=event->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    if (event->event.code==0x10) return fail(c,p.len==1?p.data[0]:0xff,p.len==1?BTH_ERR_STATE:BTH_ERR_LENGTH);
    if (!c->pending) return BTH_IGNORED;
    if (event->event.code==0x0f) {
        if (p.len!=4) return fail(c,0xff,BTH_ERR_LENGTH);
        if (bth_read_le16(p.data+2)!=c->pending_opcode) return BTH_IGNORED;
        return p.data[0]?fail(c,p.data[0],BTH_ERR_STATE):BTH_MORE;
    }
    if (event->event.code!=0x0e) return BTH_IGNORED;
    if (p.len<3) return fail(c,0xff,BTH_ERR_LENGTH);
    if (bth_read_le16(p.data+1)!=c->pending_opcode) return BTH_IGNORED;
    if (p.len<4) return fail(c,0xff,BTH_ERR_LENGTH);
    if (p.data[3]) return fail(c,p.data[3],BTH_ERR_STATE);
    const uint8_t *data=p.data+4;
    size_t expected=c->stage==BTH_CAP_COMMANDS?68:c->pending_opcode==0x1004?14:
                    c->stage==BTH_CAP_LE_FEATURES || c->stage==BTH_CAP_LE_STATES?12:4;
    if (p.len!=expected) return fail(c,0xff,BTH_ERR_LENGTH);
    bth_cap_stage_t next=(bth_cap_stage_t)(c->stage+1);
    if (c->pending_opcode==0x1004) {
        const unsigned requested=page(c->stage);
        if (data[0]!=requested) return BTH_IGNORED;
        if (data[1]<requested) return fail(c,0xff,BTH_ERR_PARAM);
        c->max_classic_page=data[1];
        memcpy(c->classic[requested],data+2,8);
    }
    switch (c->stage) {
    case BTH_CAP_COMMANDS:
        memcpy(c->commands,data,64);
        // Core Supported Commands: ExtFeatures117, WriteSSP142,
        // WriteLEHost198, LEFeatures202, LEStates227.
        if (!(data[14]&0x40) || !(data[17]&0x40) || !(data[24]&0x40) || !(data[25]&4) || !(data[28]&8))
            return fail(c,0xff,BTH_UNSUPPORTED);
        break;
    case BTH_CAP_CLASSIC0:
        if (c->max_classic_page<1 || !(c->classic[0][4]&0x40) || (c->classic[0][4]&0x20) || !(c->classic[0][6]&8))
            return fail(c,0xff,BTH_UNSUPPORTED);
        // The simultaneous-host parameter became unused at HCI5.3 (version12).
        c->simultaneous_host=c->hci_version<12 && (c->classic[0][6]&2)!=0;
        break;
    case BTH_CAP_SSP: c->ssp_enabled=true; break;
    case BTH_CAP_LE_HOST: c->le_enabled=true; break;
    case BTH_CAP_CLASSIC1:
        if ((c->classic[1][0]&3)!=3) return fail(c,0xff,BTH_ERR_STATE);
        if (c->max_classic_page<2) next=BTH_CAP_VERIFY_HOST;
        break;
    case BTH_CAP_CLASSIC2:
        if (!c->classic_sc_allowed || !(c->classic[2][1]&1)) next=BTH_CAP_VERIFY_HOST;
        else if (!(c->commands[32]&8)) return fail(c,0xff,BTH_UNSUPPORTED);
        break;
    case BTH_CAP_SC_HOST: c->sc_enabled=true; break;
    case BTH_CAP_VERIFY_HOST: {
        uint8_t required=(uint8_t)(3|(c->simultaneous_host?4:0)|(c->sc_enabled?8:0));
        if ((c->classic[1][0]&required)!=required) return fail(c,0xff,BTH_ERR_STATE);
        if (!c->classic_sc_allowed && (c->classic[1][0]&8)) return fail(c,0xff,BTH_ERR_STATE);
        break;
    }
    case BTH_CAP_LE_FEATURES: memcpy(c->le_features,data,8); break;
    case BTH_CAP_LE_STATES: memcpy(c->le_states,data,8); break;
    default: return fail(c,0xff,BTH_ERR_STATE);
    }
    c->pending=false; c->stage=next; return BTH_OK;
}
bth_status_t bth_capabilities_expire(bth_capabilities_t *c) {
    if (!c || !c->initialized) return BTH_ERR_PARAM;
    if (!c->pending) return BTH_IGNORED;
    return fail(c,0xff,BTH_ERR_STATE);
}
