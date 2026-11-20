#include "../../src/owned/core/bth_sco.h"
static unsigned checks,failed;
#define assert(x) do { ++checks; if(!(x)) ++failed; } while (0)
#include <stdio.h>
#include <string.h>
unsigned scoBytePacingSelftest(void){ checks=failed=0;
 bth_sco_t s;uint8_t rx[256]={0};bth_sco_config_t c={0};c.owner=1;c.acl_handle=1;c.codec=BTH_SCO_CVSD;c.route_confirmed=true;c.allow_rx_pacing=true;c.controller_tx_packets=1;c.controller_tx_bytes=64;c.rx_slots=4;c.rx_bytes=64;c.max_latency=10;c.packet_types=1;c.voice_setting=0x60;
 assert(!bth_sco_init(&s,&c,rx,sizeof rx-1));assert(!bth_sco_init(&s,&c,NULL,sizeof rx));
 assert(bth_sco_init(&s,&c,rx,sizeof rx));assert(!bth_sco_backend_ready(&s));
 // An actual incoming request is rejected while RX pacing is disabled.
 uint8_t request_parameters[10]={0};request_parameters[9]=2;bth_h4_frame_t request={0};request.type=BTH_H4_EVENT;request.event.code=4;request.event.params.data=request_parameters;request.event.params.len=10;
 assert(bth_sco_on_event(&s,&request,true,10)==BTH_OK&&s.reject);bth_sco_command_t cmd={0};assert(bth_sco_next_command(&s,&cmd)==BTH_OK&&cmd.opcode==0x042a);assert(bth_sco_audio_ready(&s,1,s.generation,true)==BTH_IGNORED);
 assert(bth_sco_init(&s,&c,rx,sizeof rx));
 // Forced connected fixture tests the old bypass directly; no RX should create TX credit.
 s.state=BTH_SCO_CONNECTED;s.active_handle=true;s.handle=2;s.generation=1;
 uint8_t data[60]={0},out[64];bth_sco_view_t v={0};v.handle=2;v.payload.data=data;v.payload.len=60;
 for(int i=0;i<4;i++)assert(bth_sco_receive(&s,1,&v)==BTH_OK);
 assert(s.rx_pacing_bytes==0);assert(bth_sco_receive(&s,1,&v)==BTH_ERR_CAPACITY&&s.rx_count==4);size_t n=1;bth_sco_token_t ticket={0};
 assert(bth_sco_prepare_tx(&s,1,1,data,60,out,sizeof out,&n,&ticket)==BTH_ERR_STATE&&n==0);
 s.rx_pacing_bytes=240;assert(bth_sco_prepare_tx(&s,1,1,data,60,out,sizeof out,&n,&ticket)==BTH_ERR_STATE&&n==0);
 s.tx_reserved=true;s.tx_ticket=(bth_sco_token_t){1,1,60};assert(bth_sco_commit_tx(&s,s.tx_ticket)==BTH_ERR_STATE&&s.tx_submitted==0);
 // Positive completion credit path remains bounded to one outstanding packet.
 c.controller_tx_flow=true;c.route_confirmed=false;assert(bth_sco_init(&s,&c,rx,sizeof rx)&&!bth_sco_backend_ready(&s));c.route_confirmed=true;c.controller_tx_packets=0;assert(bth_sco_init(&s,&c,rx,sizeof rx)&&!bth_sco_backend_ready(&s));c.controller_tx_packets=1;assert(bth_sco_init(&s,&c,rx,sizeof rx));assert(bth_sco_backend_ready(&s));s.state=BTH_SCO_CONNECTED;s.active_handle=true;s.handle=2;s.generation=1;s.tx_available=1;
 assert(bth_sco_prepare_tx(&s,1,1,data,60,out,sizeof out,&n,&ticket)==BTH_OK);assert(bth_sco_commit_tx(&s,ticket)==BTH_OK);assert(s.tx_available==0&&s.tx_outstanding==1);
 assert(bth_sco_prepare_tx(&s,1,1,data,60,out,sizeof out,&n,&ticket)==BTH_MORE);
 assert(bth_sco_completed(&s,2,1)==BTH_OK);assert(s.tx_available==1&&s.tx_outstanding==0);
 assert(bth_sco_completed(&s,2,1)==BTH_ERR_STATE);
 assert(bth_sco_prepare_tx(&s,2,1,data,60,out,sizeof out,&n,&ticket)==BTH_IGNORED);
 assert(bth_sco_prepare_tx(&s,1,2,data,60,out,sizeof out,&n,&ticket)==BTH_IGNORED);
 assert(bth_sco_prepare_tx(&s,1,1,data,60,out,sizeof out,&n,&ticket)==BTH_OK);bth_sco_token_t copy=ticket;
 assert(bth_sco_commit_tx(&s,ticket)==BTH_OK);assert(bth_sco_commit_tx(&s,copy)==BTH_IGNORED);
 puts("PASS disabled RX credit and one completion-paced outstanding packet; synthetic only");return failed;}
unsigned scoBytePacingChecks(void){return checks;
}
