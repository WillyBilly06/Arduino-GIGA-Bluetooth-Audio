#include "bth_sco_setup.h"
#include <string.h>
enum {READ_COMMANDS=0,READ_ROUTE,WRITE_ROUTE,VERIFY_ROUTE,READ_FLOW,WRITE_FLOW,VERIFY_FLOW,DONE};
static uint16_t opcode(uint8_t step){switch(step){case READ_COMMANDS:return 0x1002;case READ_ROUTE:case VERIFY_ROUTE:return 0xfc1d;case WRITE_ROUTE:return 0xfc1c;case READ_FLOW:case VERIFY_FLOW:return 0x0c2e;case WRITE_FLOW:return 0x0c2f;default:return 0;}}
static void finish(bth_sco_setup_t* s,bth_sco_setup_reason_t reason){s->done=true;s->step=DONE;s->pending=false;s->reason=reason;}
static void no_flow(bth_sco_setup_t* s){s->controller_flow=false;s->rx_paced=s->config.allow_rx_pacing;s->available=s->rx_paced&&s->config.tx_bytes;finish(s,s->available?BTH_SCO_SETUP_NONE:BTH_SCO_SETUP_FLOW_UNAVAILABLE);}
bool bth_sco_setup_init(bth_sco_setup_t* s,const bth_sco_setup_config_t* c){
 if(!s||!c)return false;memset(s,0,sizeof *s);s->config=*c;s->initialized=true;
 if(c->manufacturer!=15)finish(s,BTH_SCO_SETUP_CONTROLLER);else if(!c->tx_bytes)finish(s,BTH_SCO_SETUP_BUFFERS);return true;
}
bth_status_t bth_sco_setup_expire(bth_sco_setup_t* s,uint32_t now){
 if(!s||!s->initialized)return BTH_ERR_PARAM;if(!s->pending||(int32_t)(now-s->deadline)<0)return BTH_MORE;
 s->requires_reset=true;finish(s,BTH_SCO_SETUP_TIMEOUT);return BTH_OK;
}
bth_status_t bth_sco_setup_next(bth_sco_setup_t* s,uint32_t now,bth_sco_setup_command_t* command){
 if(!s||!s->initialized||!command)return BTH_ERR_PARAM;memset(command,0,sizeof *command);bth_sco_setup_expire(s,now);if(s->done||s->pending)return BTH_MORE;
 if(s->serial==UINT32_MAX){finish(s,BTH_SCO_SETUP_MALFORMED);return BTH_ERR_STATE;}
 const uint16_t op=opcode(s->step);if(!op)return BTH_ERR_STATE;
 if(!s->ticket.serial||s->ticket.step!=s->step)s->ticket=(bth_sco_setup_ticket_t){++s->serial,op,s->step};
 command->opcode=op;command->ticket=s->ticket;
 if(s->step==WRITE_ROUTE){if(!s->config.allow_route_configuration||!s->have_route){finish(s,BTH_SCO_SETUP_ROUTE_REQUIRED);return BTH_ERR_STATE;}memcpy(command->parameters,s->baseline_route,5);command->parameters[0]=1;command->length=5;}
 else if(s->step==WRITE_FLOW){if(!s->flow_write_supported){no_flow(s);return BTH_MORE;}command->parameters[0]=1;command->length=1;}
 return BTH_OK;
}
bth_status_t bth_sco_setup_queued(bth_sco_setup_t* s,bth_sco_setup_ticket_t ticket,uint32_t now){
 if(!s||!s->initialized)return BTH_ERR_PARAM;
 if(s->done||s->pending||!ticket.serial||ticket.serial!=s->ticket.serial||ticket.step!=s->step||ticket.opcode!=opcode(s->step))return BTH_IGNORED;
 s->pending=true;s->deadline=now+5000;return BTH_OK;
}
static void result(bth_sco_setup_t* s,const uint8_t* p,size_t n){
 if(!n){s->requires_reset=s->step==WRITE_ROUTE||s->step==VERIFY_ROUTE;finish(s,BTH_SCO_SETUP_MALFORMED);return;}
 s->last_status=p[0];
 if(p[0]){
  if(s->step==READ_COMMANDS){s->flow_write_supported=false;s->step=READ_ROUTE;return;}
  if(s->step==READ_ROUTE){finish(s,BTH_SCO_SETUP_ROUTE_QUERY);return;}
  if(s->step==WRITE_ROUTE){s->requires_reset=true;finish(s,BTH_SCO_SETUP_ROUTE_WRITE);return;}
  if(s->step==VERIFY_ROUTE){s->requires_reset=true;finish(s,BTH_SCO_SETUP_ROUTE_MISMATCH);return;}
  if(s->step==VERIFY_FLOW){finish(s,BTH_SCO_SETUP_FLOW_UNAVAILABLE);return;} // Unknown after accepted enable is not a known-off mode.
  if(s->step==READ_FLOW||s->step==WRITE_FLOW){no_flow(s);return;}
 }
 switch(s->step){
 case READ_COMMANDS:
  if(n!=65){finish(s,BTH_SCO_SETUP_MALFORMED);break;}memcpy(s->commands,p+1,64);s->flow_write_supported=(s->commands[10]&0x10)!=0;s->step=READ_ROUTE;break;
 case READ_ROUTE:
  if(n!=6){finish(s,BTH_SCO_SETUP_MALFORMED);break;}memcpy(s->baseline_route,p+1,5);s->have_route=true;
  if(p[1]==1)s->step=READ_FLOW;else if(s->config.allow_route_configuration)s->step=WRITE_ROUTE;else finish(s,BTH_SCO_SETUP_ROUTE_REQUIRED);break;
 case WRITE_ROUTE:
  if(n!=1){s->requires_reset=true;finish(s,BTH_SCO_SETUP_MALFORMED);break;}s->route_written=true;s->step=VERIFY_ROUTE;break;
 case VERIFY_ROUTE:
  if(n!=6||p[1]!=1||memcmp(p+2,s->baseline_route+1,4)){s->requires_reset=true;finish(s,BTH_SCO_SETUP_ROUTE_MISMATCH);break;}s->step=READ_FLOW;break;
 case READ_FLOW:case VERIFY_FLOW:
  if(n!=2||p[1]>1){finish(s,BTH_SCO_SETUP_MALFORMED);break;}
  if(p[1]&&s->config.tx_packets){s->controller_flow=true;s->available=true;finish(s,BTH_SCO_SETUP_NONE);}
  else if(p[1])finish(s,BTH_SCO_SETUP_BUFFERS);
  else if(s->step==READ_FLOW&&s->flow_write_supported)s->step=WRITE_FLOW;else no_flow(s);break;
 case WRITE_FLOW:
  if(n!=1){finish(s,BTH_SCO_SETUP_MALFORMED);break;}s->step=VERIFY_FLOW;break;
 default:finish(s,BTH_SCO_SETUP_MALFORMED);break;
 }
}
bth_status_t bth_sco_setup_on_event(bth_sco_setup_t* s,const bth_h4_frame_t* event,uint32_t now){
 if(!s||!s->initialized||!event)return BTH_ERR_PARAM;bth_sco_setup_expire(s,now);
 if(s->done||!s->pending||event->type!=BTH_H4_EVENT)return BTH_IGNORED;
 const bth_span_t p=event->event.params;
 if(event->event.code==0x0e){if(p.len<3||bth_read_le16(p.data+1)!=s->ticket.opcode)return BTH_IGNORED;s->pending=false;result(s,p.data+3,p.len-3);return BTH_OK;}
 if(event->event.code==0x0f){if(p.len!=4||bth_read_le16(p.data+2)!=s->ticket.opcode)return BTH_IGNORED;if(!p.data[0])return BTH_IGNORED;s->pending=false;result(s,p.data,1);return BTH_OK;}
 return BTH_IGNORED;
}
