#include "bth_control_tx.h"
#include "bth_l2cap_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"QOS_CORE line%d: %s\n",__LINE__,#x);abort();}}while(0)
static void fill(bth_control_tx_t*q,unsigned peer,uint16_t cid,unsigned n){bth_control_reservation_t r;CHECK(bth_control_tx_reserve(q,(uint8_t)peer,&r)==BTH_OK);for(unsigned j=0;j<n;++j)r.data[j]=(uint8_t)j;CHECK(bth_control_tx_commit(q,r.token,cid,n,672)==BTH_OK);}
static void completed(bth_hci_flow_t*f,uint16_t handle){uint8_t p[]={1,(uint8_t)handle,(uint8_t)(handle>>8),1,0};bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x13,.params={p,sizeof p}}};CHECK(bth_hci_flow_on_event(f,&e)==BTH_OK);}
int main(void){
 bth_control_tx_t q;bth_control_tx_init(&q);CHECK(bth_control_tx_attach(&q,0,0x25,7)&&bth_control_tx_attach(&q,1,0x26,8));uint32_t owners[]={7,8};
 bth_hci_flow_t flow;bth_hci_flow_init(&flow);CHECK(bth_hci_flow_set_buffers(&flow,1021,8,251,8)&&bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC)&&bth_hci_flow_attach(&flow,0x26,BTH_LINK_LE));
 fill(&q,1,4,515);const bth_control_tx_t original=q;const bth_hci_flow_t original_flow=flow;bth_control_tx_policy_t policy={0,false,27,false};bth_control_tx_info_t info;uint8_t out[1030];size_t n;
 CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_MORE&&!n&&info.paced);CHECK(!memcmp(&q,&original,sizeof q)&&!memcmp(&flow,&original_flow,sizeof flow)&&!q.peers[1].tx.active);
 policy.first_peer=2;CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_ERR_PARAM&&!n);CHECK(!memcmp(&q,&original,sizeof q)&&!memcmp(&flow,&original_flow,sizeof flow));policy.first_peer=0;policy.le_data_allowed=true;
 uint8_t assembly[600];bth_l2cap_rx_t rx;CHECK(bth_l2cap_rx_init(&rx,0x26,assembly,sizeof assembly));unsigned fragments=0,bytes=0;
 while(q.used){CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_OK);CHECK(info.peer==1&&info.le_data&&!info.urgent_bypass&&info.payload_bytes<=27);bth_h4_frame_t h;CHECK(bth_h4_decode(out,n,&h)==BTH_OK);CHECK(h.acl.pb==(fragments?1:0));bth_l2cap_pdu_t p;const bth_status_t st=bth_l2cap_rx_feed(&rx,&h.acl,&p);CHECK(st==BTH_MORE||st==BTH_OK);if(st==BTH_OK){CHECK(p.cid==4&&p.payload.len==515);for(unsigned j=0;j<515;++j)CHECK(p.payload.data[j]==(uint8_t)j);}bytes+=info.payload_bytes;++fragments;completed(&flow,0x26);}
 CHECK(fragments==20&&bytes==519&&flow.le.available==8);
 fill(&q,1,4,55);fill(&q,1,5,6);policy.le_data_allowed=false;unsigned completed_cids[2]={0},count=0;
 while(q.used){CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_OK&&info.urgent_bypass);bth_h4_frame_t h;CHECK(bth_h4_decode(out,n,&h)==BTH_OK);bth_l2cap_pdu_t p;const bth_status_t st=bth_l2cap_rx_feed(&rx,&h.acl,&p);CHECK(st==BTH_MORE||st==BTH_OK);if(st==BTH_OK){CHECK(count<2);completed_cids[count++]=p.cid;}completed(&flow,0x26);}
 CHECK(count==2&&completed_cids[0]==4&&completed_cids[1]==5);
 bth_control_tx_init(&q);CHECK(bth_control_tx_attach(&q,0,0x25,7)&&bth_control_tx_attach(&q,1,0x26,8));bth_hci_flow_init(&flow);CHECK(bth_hci_flow_set_buffers(&flow,1021,8,0,0)&&bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC)&&bth_hci_flow_attach(&flow,0x26,BTH_LINK_LE));fill(&q,1,4,80);for(unsigned j=0;j<7;++j)CHECK(bth_hci_flow_take_acl(&flow,0x26,27));policy.first_peer=1;policy.le_data_allowed=true;const bth_control_tx_t held=q;const bth_hci_flow_t held_flow=flow;CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_MORE&&!n);CHECK(!memcmp(&q,&held,sizeof q)&&!memcmp(&flow,&held_flow,sizeof flow));fill(&q,0,0x40,1);CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_OK&&info.peer==0&&flow.classic.available==0);completed(&flow,0x26);CHECK(flow.classic.available==1);completed(&flow,0x26);CHECK(flow.classic.available==2);CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_OK&&info.peer==1&&flow.classic.available==1);
 bth_control_tx_init(&q);CHECK(bth_control_tx_attach(&q,1,0x26,8));bth_hci_flow_init(&flow);CHECK(bth_hci_flow_set_buffers(&flow,1021,1,0,0)&&bth_hci_flow_attach(&flow,0x26,BTH_LINK_LE));fill(&q,1,4,1);CHECK(bth_control_tx_next_policy(&q,owners,&flow,&policy,&info,out,sizeof out,&n)==BTH_OK&&info.peer==1&&flow.classic.available==0);completed(&flow,0x26);CHECK(flow.classic.available==1);
 printf("QOS_CORE limited/paced-no-start/credits/owner/fifo/urgent/shared-reserve %u checks PASS\n",checks);return 0;
}
