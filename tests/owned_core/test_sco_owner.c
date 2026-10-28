#include "../../src/owned/core/bth_sco.h"
#include <string.h>
static unsigned checks,failed;
#define CHECK(x) do{++checks;if(!(x))++failed;}while(0)
static bth_sco_t s;
static uint8_t memory[256];
static const uint8_t request[]={4,4,10,1,2,3,4,5,6,0,0,0,2};
static const uint8_t complete[]={4,0x2c,17,0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2};
static bth_sco_config_t configuration(void){
 bth_sco_config_t c;memset(&c,0,sizeof c);c.owner=7;c.acl_handle=0x25;for(unsigned i=0;i<6;++i)c.peer_hci_address[i]=(uint8_t)(i+1);
 c.codec=BTH_SCO_CVSD;c.route_confirmed=true;c.controller_tx_flow=true;c.controller_tx_packets=2;c.controller_tx_bytes=64;
 c.host_rx_flow=true;c.rx_slots=4;c.rx_bytes=64;c.max_latency=12;c.voice_setting=0x60;c.packet_types=0x03c8;c.retransmission_effort=2;return c;
}
static bth_status_t event(const uint8_t* p,size_t n,bool secure,uint32_t now){bth_h4_frame_t e;CHECK(bth_h4_decode(p,n,&e)==BTH_OK);return bth_sco_on_event(&s,&e,secure,now);}
static void start(void){
 bth_sco_config_t c=configuration();CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,100)==BTH_OK);CHECK(s.state==BTH_SCO_REQUESTED);
 bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_MORE);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);
 CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x0429&&command.length==21);CHECK(!memcmp(command.parameters,c.peer_hci_address,6));CHECK(bth_read_le16(command.parameters+16)==0x60);
 CHECK(bth_sco_command_queued(&s,command.ticket,101)==BTH_OK);
 const uint8_t status[]={4,15,4,0,1,0x29,4};CHECK(event(status,sizeof status,true,102)==BTH_OK);CHECK(event(complete,sizeof complete,true,103)==BTH_OK);CHECK(s.state==BTH_SCO_CONNECTED&&s.handle==0x33&&s.tx_available==2);
}
static void gates(void){
 bth_sco_config_t c=configuration();c.route_confirmed=false;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,10)==BTH_OK);bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x042a&&command.length==7&&command.parameters[6]==0x0d);
 c=configuration();CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,false,10)==BTH_OK);CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x042a);
 CHECK(bth_sco_audio_ready(&s,8,s.generation,true)==BTH_IGNORED);
 c=configuration();c.codec=BTH_SCO_MSBC;c.voice_setting=0x43;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(bth_sco_backend_ready(&s));
}
static void flow_and_storage(void){
 start();uint8_t payload[]={1,2,3,4},wire[80];size_t n=0;bth_sco_token_t token;
 CHECK(bth_sco_prepare_tx(&s,7,s.generation,payload,4,wire,sizeof wire,&n,&token)==BTH_OK&&n==8&&wire[0]==3&&wire[1]==0x33&&wire[2]==0&&wire[3]==4);CHECK(s.tx_available==2);
 CHECK(bth_sco_commit_tx(&s,token)==BTH_OK&&s.tx_available==1&&s.tx_outstanding==1);CHECK(bth_sco_commit_tx(&s,token)==BTH_IGNORED);
 CHECK(bth_sco_completed(&s,0x33,2)==BTH_ERR_STATE&&s.tx_available==1);CHECK(bth_sco_completed(&s,0x25,1)==BTH_IGNORED);CHECK(bth_sco_completed(&s,0x33,1)==BTH_OK&&s.tx_available==2);
 bth_sco_view_t packet={0x33,0,{payload,sizeof payload}};
 for(unsigned i=0;i<4;++i)CHECK(bth_sco_receive(&s,7,&packet)==BTH_OK);
 CHECK(bth_sco_receive(&s,7,&packet)==BTH_ERR_CAPACITY&&s.rx_count==4);
 bth_sco_rx_view_t view;CHECK(bth_sco_peek_rx(&s,7,s.generation,&view)==BTH_OK&&view.payload.len==4&&view.payload.data[0]==1);CHECK(!s.pending_completed);
 CHECK(bth_sco_consume_rx(&s,view.ticket)==BTH_OK&&s.pending_completed==1);CHECK(bth_sco_consume_rx(&s,view.ticket)==BTH_IGNORED);
 CHECK(bth_sco_prepare_completed(&s,wire,sizeof wire,&n,&token)==BTH_OK&&n==9&&wire[0]==1&&bth_read_le16(wire+1)==0x0c35&&bth_read_le16(wire+7)==1);CHECK(s.pending_completed==1);
 CHECK(bth_sco_commit_completed(&s,token)==BTH_OK&&!s.pending_completed);CHECK(bth_sco_commit_completed(&s,token)==BTH_IGNORED);
 const uint32_t generation=s.generation;const uint8_t close[]={4,5,4,0,0x33,0,0x13};CHECK(event(close,sizeof close,true,200)==BTH_OK&&s.state==BTH_SCO_ATTACHED&&!s.rx_count);
 CHECK(bth_sco_prepare_tx(&s,7,generation,payload,4,wire,sizeof wire,&n,&token)==BTH_IGNORED);
}
static void timeout_and_peer(void){
 bth_sco_config_t c=configuration();CHECK(bth_sco_init(&s,&c,memory,sizeof memory));uint8_t wrong[sizeof request];memcpy(wrong,request,sizeof wrong);wrong[3]=9;CHECK(event(wrong,sizeof wrong,true,10)==BTH_IGNORED&&s.state==BTH_SCO_ATTACHED);
 CHECK(event(request,sizeof request,true,0xfffffff0u)==BTH_OK);CHECK(bth_sco_expire(&s,0x20u)==BTH_MORE);CHECK(bth_sco_expire(&s,6000)==BTH_OK&&s.state==BTH_SCO_FAILED);
 CHECK(event(complete,sizeof complete,true,6010)==BTH_IGNORED&&s.state==BTH_SCO_FAILED);
}
static void late_completion_and_revoke(void){
 bth_sco_config_t c=configuration();bth_sco_command_t command;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));
 CHECK(event(request,sizeof request,true,100)==BTH_OK);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,101)==BTH_OK);
 const uint8_t status[]={4,15,4,0,1,0x29,4};CHECK(event(status,sizeof status,true,102)==BTH_OK);
 // No periodic poll occurred: receive must apply expiry before the late event.
 CHECK(event(complete,sizeof complete,true,6000)==BTH_IGNORED&&s.state==BTH_SCO_FAILED);
 CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,100)==BTH_OK);
 const uint8_t revoked[]={4,8,4,0,0x25,0,0};event(revoked,sizeof revoked,false,101);
 CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_IGNORED);CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x042a);
}
extern bth_status_t bth_sco_request_close(bth_sco_t*,uint32_t,uint32_t,uint8_t,uint32_t);
static void local_close(void){start();bth_sco_command_t command;CHECK(bth_sco_request_close(&s,8,s.generation,0x13,200)==BTH_IGNORED);CHECK(bth_sco_request_close(&s,7,s.generation,0x13,200)==BTH_OK);
 CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x0406&&command.length==3&&bth_read_le16(command.parameters)==0x33&&command.parameters[2]==0x13);
 CHECK(bth_sco_command_queued(&s,command.ticket,201)==BTH_OK);const uint8_t status[]={4,15,4,0,1,6,4};CHECK(event(status,sizeof status,true,202)==BTH_OK&&s.state==BTH_SCO_DISCONNECTING&&s.handle==0x33);
 const uint8_t close[]={4,5,4,0,0x33,0,0x13};CHECK(event(close,sizeof close,true,203)==BTH_OK&&s.state==BTH_SCO_ATTACHED);
}
static void cleanup_unusable_completion(void){
 for(unsigned wrong=0;wrong<2;++wrong){bth_sco_config_t c=configuration();bth_sco_command_t command;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,100)==BTH_OK);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,101)==BTH_OK);const uint8_t status[]={4,15,4,0,1,0x29,4};CHECK(event(status,sizeof status,true,102)==BTH_OK);
  uint8_t result[sizeof complete];memcpy(result,complete,sizeof result);if(wrong)result[19]=3;
  CHECK(event(result,sizeof result,wrong!=0,103)==BTH_OK&&s.state==BTH_SCO_DISCONNECTING&&s.handle==0x33);CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x0406);
 }
}
static void close_releases_only_consumed_storage(void){start();uint8_t payload[]={1,2};bth_sco_view_t packet={0x33,0,{payload,2}};CHECK(bth_sco_receive(&s,7,&packet)==BTH_OK);CHECK(bth_sco_request_close(&s,7,s.generation,0x13,200)==BTH_OK&&s.pending_completed==1&&!s.rx_count);
 bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_MORE);uint8_t out[16];size_t n=0;bth_sco_token_t token;CHECK(bth_sco_prepare_completed(&s,out,sizeof out,&n,&token)==BTH_OK);CHECK(bth_sco_commit_completed(&s,token)==BTH_OK);CHECK(bth_sco_next_command(&s,&command)==BTH_OK&&command.opcode==0x0406);
}
extern bth_status_t bth_sco_reconfigure(bth_sco_t*,const bth_sco_config_t*,uint8_t*,size_t);
static void reconfiguration_retains_epochs(void){start();const uint32_t generation=s.generation;const uint8_t close[]={4,5,4,0,0x33,0,0x13};CHECK(event(close,sizeof close,true,200)==BTH_OK);bth_sco_config_t c=configuration();c.codec=BTH_SCO_MSBC;c.voice_setting=0x43;CHECK(bth_sco_reconfigure(&s,&c,memory,sizeof memory)==BTH_OK&&s.generation==generation);CHECK(event(request,sizeof request,true,300)==BTH_OK&&s.generation>generation);CHECK(bth_sco_audio_ready(&s,7,generation,true)==BTH_IGNORED);}
static void reject_waits_for_actual_completion(void){
 bth_sco_config_t c=configuration();c.route_confirmed=false;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,100)==BTH_OK);bth_sco_command_t command;CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,101)==BTH_OK);
 const uint8_t status[]={4,15,4,0,1,0x2a,4};CHECK(event(status,sizeof status,true,102)==BTH_OK&&s.state==BTH_SCO_WAIT_COMPLETE);CHECK(event(request,sizeof request,true,103)==BTH_IGNORED);
 uint8_t result[sizeof complete];memcpy(result,complete,sizeof result);result[3]=0x0d;CHECK(event(result,sizeof result,true,104)==BTH_OK&&s.state==BTH_SCO_ATTACHED);
}
static void late_queue_cannot_revive_request(void){bth_sco_config_t c=configuration();bth_sco_command_t command;CHECK(bth_sco_init(&s,&c,memory,sizeof memory));CHECK(event(request,sizeof request,true,100)==BTH_OK);CHECK(bth_sco_audio_ready(&s,7,s.generation,true)==BTH_OK);CHECK(bth_sco_next_command(&s,&command)==BTH_OK);CHECK(bth_sco_command_queued(&s,command.ticket,6000)==BTH_IGNORED&&s.state==BTH_SCO_FAILED);}
unsigned scoOwnerSelftest(void){checks=failed=0;gates();flow_and_storage();timeout_and_peer();late_completion_and_revoke();local_close();cleanup_unusable_completion();close_releases_only_consumed_storage();reconfiguration_retains_epochs();reject_waits_for_actual_completion();late_queue_cannot_revive_request();return failed;}
unsigned scoOwnerChecks(void){return checks;}
