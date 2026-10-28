#include "../../src/owned/core/bth_sco_setup.h"
#include <string.h>
static unsigned checks,failed;
#define CHECK(x) do{++checks;if(!(x))++failed;}while(0)
static bth_sco_setup_t s;static uint32_t now;
static void response(uint16_t opcode,const uint8_t* data,size_t n){uint8_t wire[260];wire[0]=4;wire[1]=14;wire[2]=(uint8_t)(n+3);wire[3]=1;wire[4]=(uint8_t)opcode;wire[5]=(uint8_t)(opcode>>8);memcpy(wire+6,data,n);bth_h4_frame_t event;CHECK(bth_h4_decode(wire,n+6,&event)==BTH_OK);CHECK(bth_sco_setup_on_event(&s,&event,++now)==BTH_OK);}
static bth_sco_setup_command_t next(uint16_t opcode){bth_sco_setup_command_t command;CHECK(bth_sco_setup_next(&s,now,&command)==BTH_OK&&command.opcode==opcode);CHECK(bth_sco_setup_queued(&s,command.ticket,++now)==BTH_OK);return command;}
static bth_sco_setup_config_t config(void){bth_sco_setup_config_t c;memset(&c,0,sizeof c);c.manufacturer=15;c.tx_bytes=64;c.tx_packets=2;return c;}
static void discovery(void){uint8_t commands[65]={0};commands[11]=0x10;next(0x1002);response(0x1002,commands,sizeof commands);}
static void known_hci_route_and_flow(void){bth_sco_setup_config_t c=config();CHECK(bth_sco_setup_init(&s,&c));now=100;discovery();next(0xfc1d);const uint8_t route[]={0,1,2,3,0,1};response(0xfc1d,route,sizeof route);next(0x0c2e);const uint8_t off[]={0,0};response(0x0c2e,off,2);const bth_sco_setup_command_t write=next(0x0c2f);CHECK(write.length==1&&write.parameters[0]==1);const uint8_t ok[]={0};response(0x0c2f,ok,1);next(0x0c2e);const uint8_t on[]={0,1};response(0x0c2e,on,2);CHECK(s.done&&s.available&&s.controller_flow&&!s.rx_paced&&!s.route_written);}
static void route_configuration_gate(void){bth_sco_setup_config_t c=config();CHECK(bth_sco_setup_init(&s,&c));now=100;discovery();next(0xfc1d);const uint8_t pcm[]={0,0,2,3,0,1};response(0xfc1d,pcm,6);CHECK(s.done&&!s.available&&s.reason==BTH_SCO_SETUP_ROUTE_REQUIRED&&!s.route_written);
 c.allow_route_configuration=true;CHECK(bth_sco_setup_init(&s,&c));discovery();next(0xfc1d);response(0xfc1d,pcm,6);bth_sco_setup_command_t write=next(0xfc1c);const uint8_t wanted[]={1,2,3,0,1};CHECK(write.length==5&&!memcmp(write.parameters,wanted,5));const uint8_t ok[]={0};response(0xfc1c,ok,1);next(0xfc1d);const uint8_t mismatch[]={0,1,9,3,0,1};response(0xfc1d,mismatch,6);CHECK(s.done&&!s.available&&s.requires_reset&&s.reason==BTH_SCO_SETUP_ROUTE_MISMATCH);
}
static void unsupported_and_timeout(void){bth_sco_setup_config_t c=config();c.allow_rx_pacing=true;CHECK(bth_sco_setup_init(&s,&c));now=100;uint8_t commands[65]={0};next(0x1002);response(0x1002,commands,65);next(0xfc1d);const uint8_t hci[]={0,1,0,0,0,0};response(0xfc1d,hci,6);next(0x0c2e);const uint8_t unsupported[]={1};response(0x0c2e,unsupported,1);CHECK(s.done&&s.available&&s.rx_paced&&!s.controller_flow);
 CHECK(bth_sco_setup_init(&s,&c));now=100;bth_sco_setup_command_t command=next(0x1002);CHECK(bth_sco_setup_queued(&s,command.ticket,now)==BTH_IGNORED);CHECK(bth_sco_setup_expire(&s,now+5001)==BTH_OK&&s.done&&!s.available&&s.requires_reset);
}
static void unknown_after_enable_is_not_flow_off(void){bth_sco_setup_config_t c=config();c.allow_rx_pacing=true;CHECK(bth_sco_setup_init(&s,&c));now=100;discovery();next(0xfc1d);const uint8_t route[]={0,1,0,0,0,0};response(0xfc1d,route,6);next(0x0c2e);const uint8_t off[]={0,0};response(0x0c2e,off,2);next(0x0c2f);const uint8_t ok[]={0};response(0x0c2f,ok,1);next(0x0c2e);const uint8_t unavailable[]={1};response(0x0c2e,unavailable,1);CHECK(s.done&&!s.available&&!s.rx_paced);}
unsigned scoSetupSelftest(void){checks=failed=0;known_hci_route_and_flow();route_configuration_gate();unsupported_and_timeout();unknown_after_enable_is_not_flow_off();return failed;}
unsigned scoSetupChecks(void){return checks;}
