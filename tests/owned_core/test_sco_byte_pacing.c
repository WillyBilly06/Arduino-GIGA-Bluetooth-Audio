#include "../../src/owned/core/bth_sco.h"
#include <string.h>
static unsigned checks,failed;static bth_sco_t s;static uint8_t arena[256],payload[64],wire[80];
#define CHECK(x) do{++checks;if(!(x))++failed;}while(0)
static bth_status_t event(const uint8_t* p,size_t n,uint32_t now){bth_h4_frame_t e;CHECK(bth_h4_decode(p,n,&e)==BTH_OK);return bth_sco_on_event(&s,&e,true,now);}
static const uint8_t request[]={4,4,10,1,2,3,4,5,6,0,0,0,2},status[]={4,15,4,0,1,0x29,4},complete[]={4,0x2c,17,0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2};
static void connect(bool ncp){
 bth_sco_config_t c;memset(&c,0,sizeof c);c.owner=7;c.acl_handle=0x25;for(unsigned i=0;i<6;++i)c.peer_hci_address[i]=(uint8_t)(i+1);c.codec=BTH_SCO_CVSD;c.route_confirmed=true;c.controller_tx_flow=ncp;c.controller_tx_packets=2;c.controller_tx_bytes=64;c.allow_rx_pacing=true;c.host_rx_flow=true;c.rx_slots=4;c.rx_bytes=64;c.max_latency=12;c.voice_setting=0x60;c.packet_types=0x3c8;c.retransmission_effort=2;
 CHECK(bth_sco_init(&s,&c,arena,sizeof arena));CHECK(event(request,sizeof request,100)==BTH_OK);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,101)==BTH_OK);CHECK(event(status,sizeof status,102)==BTH_OK);CHECK(event(complete,sizeof complete,103)==BTH_OK);
}
static bth_status_t receive(size_t n,uint8_t state){bth_sco_view_t p={0x33,state,{payload,n}};return bth_sco_receive(&s,7,&p);}
static bth_status_t prepare(size_t n,bth_sco_token_t* ticket){size_t written=0;return bth_sco_prepare_tx(&s,7,s.generation,payload,n,wire,sizeof wire,&written,ticket);}
static void consume_rx(void){bth_sco_rx_view_t view;while(bth_sco_peek_rx(&s,7,s.generation,&view)==BTH_OK)CHECK(bth_sco_consume_rx(&s,view.ticket)==BTH_OK);}
static void spend(size_t n){bth_sco_token_t ticket;CHECK(prepare(n,&ticket)==BTH_OK&&ticket.count==n);CHECK(bth_sco_commit_tx(&s,ticket)==BTH_OK);}
static void residual_and_ticket(void){connect(false);CHECK(receive(60,0)==BTH_OK);bth_sco_token_t ticket;CHECK(prepare(30,&ticket)==BTH_OK);bth_sco_token_t wrong=ticket;wrong.count=28;CHECK(bth_sco_commit_tx(&s,wrong)==BTH_IGNORED);CHECK(bth_sco_commit_tx(&s,ticket)==BTH_OK);CHECK(bth_sco_commit_tx(&s,ticket)==BTH_IGNORED);spend(20);spend(10);CHECK(prepare(2,&ticket)==BTH_MORE);CHECK(!s.tx_available&&!s.tx_outstanding);consume_rx();CHECK(s.pending_completed==1);}
static void arbitrary_rx_residuals(void){connect(false);CHECK(receive(48,0)==BTH_OK);bth_sco_token_t ticket;CHECK(prepare(60,&ticket)==BTH_MORE);consume_rx();CHECK(receive(48,0)==BTH_OK);spend(60);consume_rx();CHECK(receive(64,0)==BTH_OK);spend(60);spend(40);CHECK(prepare(2,&ticket)==BTH_MORE);CHECK(!s.tx_available&&!s.tx_outstanding);}
static void zero_error_and_burst(void){connect(false);bth_sco_token_t ticket;CHECK(receive(0,2)==BTH_OK);consume_rx();CHECK(prepare(30,&ticket)==BTH_MORE);CHECK(receive(60,2)==BTH_OK);consume_rx();spend(30);spend(30);CHECK(receive(3,1)==BTH_ERR_LENGTH);CHECK(prepare(2,&ticket)==BTH_MORE);
 CHECK(receive(64,0)==BTH_OK);CHECK(receive(64,0)==BTH_OK);consume_rx();spend(60);spend(60);spend(8);CHECK(prepare(2,&ticket)==BTH_MORE);
}
static void explicit_overflow(void){connect(false);for(unsigned i=0;i<4;++i){CHECK(receive(64,0)==BTH_OK);consume_rx();}CHECK(receive(64,0)==BTH_ERR_CAPACITY);CHECK(s.rx_pacing_overflows==1&&s.rx_pacing_rejected_bytes==64);for(unsigned i=0;i<4;++i)spend(60);spend(16);bth_sco_token_t ticket;CHECK(prepare(2,&ticket)==BTH_MORE);}
static void generation_and_ncp_isolation(void){connect(false);CHECK(receive(60,0)==BTH_OK);bth_sco_token_t old;CHECK(prepare(30,&old)==BTH_OK);const uint32_t generation=s.generation;CHECK(bth_sco_request_close(&s,7,s.generation,0x13,200)==BTH_OK);CHECK(bth_sco_commit_tx(&s,old)==BTH_IGNORED);const uint8_t closed[]={4,5,4,0,0x33,0,0x13};CHECK(event(closed,sizeof closed,201)==BTH_OK);CHECK(event(request,sizeof request,300)==BTH_OK);CHECK(s.generation>generation);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,301)==BTH_OK);CHECK(event(status,sizeof status,302)==BTH_OK);CHECK(event(complete,sizeof complete,303)==BTH_OK);CHECK(bth_sco_commit_tx(&s,old)==BTH_IGNORED);CHECK(prepare(30,&old)==BTH_MORE);
 connect(true);CHECK(s.tx_available==2);CHECK(receive(60,0)==BTH_OK);spend(30);spend(30);CHECK(!s.tx_available&&s.tx_outstanding==2);CHECK(prepare(30,&old)==BTH_MORE);CHECK(bth_sco_completed(&s,0x33,2)==BTH_OK&&s.tx_available==2);CHECK(!s.rx_pacing_bytes&&!s.rx_pacing_overflows);
}
unsigned scoBytePacingSelftest(void){checks=failed=0;residual_and_ticket();arbitrary_rx_residuals();zero_error_and_burst();explicit_overflow();generation_and_ncp_isolation();return failed;}
unsigned scoBytePacingChecks(void){return checks;}
