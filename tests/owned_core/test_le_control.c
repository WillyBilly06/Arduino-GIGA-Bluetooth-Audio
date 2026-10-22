#include "bth_le_gap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); abort(); } ++checks; } while(0)
static bth_status_t dispatch(bth_le_gap_t *g,uint8_t *p,size_t n,uint32_t owner,uint16_t handle) {
    bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x3e,.params={p,n}}};
    return bth_le_gap_peer_event(g,owner,handle,&e);
}
static void wire(bth_cmdq_t *q,uint16_t opcode,const uint8_t *p,size_t n) {
    bth_hci_flow_t flow; bth_hci_flow_init(&flow); uint8_t out[32]; size_t written;
    CHECK(bth_cmdq_next(q,&flow,out,sizeof out,&written)==BTH_OK);
    CHECK(written==n+4 && bth_read_le16(out+1)==opcode && out[3]==n && !memcmp(out+4,p,n));
    CHECK(q->sent_count==1 && q->sent[0].owner==42);
    const uint8_t cc[]={1,(uint8_t)opcode,(uint8_t)(opcode>>8),0,0x23,1};
    bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x0e,.params={cc,sizeof cc}}}; uint32_t owner;
    CHECK(bth_cmdq_complete(q,&e,&owner)==BTH_OK && owner==42);
}
int main(void) {
    bth_cmdq_t q; bth_cmdq_init(&q); bth_le_gap_t g;
    const bth_le_gap_config_t config={.interval_min=0x80,.interval_max=0x100};
    CHECK(bth_le_gap_start(&g,&q,&config));
    uint8_t p[]={6,0x23,1,24,0,40,0,0,0,200,0};
    CHECK(dispatch(&g,p,sizeof p,0,0x123)==BTH_IGNORED && !q.used);
    CHECK(dispatch(&g,p,sizeof p,42,0x124)==BTH_IGNORED && !q.used);
    for (size_t n=1;n<sizeof p;++n) CHECK(dispatch(&g,p,n,42,0x123)==BTH_ERR_LENGTH && !q.used);
    CHECK(dispatch(&g,p,sizeof p,42,0x123)==BTH_OK);
    const uint8_t accept[]={0x23,1,24,0,40,0,0,0,200,0,0,0,0,0}; wire(&q,0x2020,accept,sizeof accept);
    // Supervision timeout must be strictly greater than twice the maximum interval.
    p[9]=10; CHECK(dispatch(&g,p,sizeof p,42,0x123)==BTH_OK);
    const uint8_t reject[]={0x23,1,0x3b}; wire(&q,0x2021,reject,sizeof reject);
    const uint16_t invalid[][4]={{5,40,0,200},{24,3201,0,200},{40,24,0,200},{24,40,500,3200},{24,40,0,9},{24,40,0,3201}};
    for (size_t i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        for (unsigned j=0;j<4;++j) {p[3+2*j]=(uint8_t)invalid[i][j];p[4+2*j]=(uint8_t)(invalid[i][j]>>8);}
        CHECK(dispatch(&g,p,sizeof p,42,0x123)==BTH_OK); wire(&q,0x2021,reject,sizeof reject);
    }
    // No false bond: the temporary public-only server answers LTK requests negatively.
    uint8_t ltk[13]={5,0x23,1}; CHECK(dispatch(&g,ltk,sizeof ltk,42,0x123)==BTH_OK);
    wire(&q,0x201b,ltk+1,2);
    CHECK(dispatch(&g,ltk,sizeof ltk-1,42,0x123)==BTH_ERR_LENGTH);
    p[0]=7; CHECK(dispatch(&g,p,sizeof p,42,0x123)==BTH_IGNORED);
    p[0]=6; p[3]=24;p[4]=0;p[5]=40;p[6]=0;p[7]=p[8]=0;p[9]=200;p[10]=0;
    for(unsigned i=0;i<8;++i) CHECK(bth_cmdq_push(&q,0x0c03,NULL,0,false)==BTH_OK);
    CHECK(dispatch(&g,p,sizeof p,42,0x123)==BTH_ERR_CAPACITY && q.used==8);
    printf("PASS LE peer control: %u checks\n",checks);return 0;
}
