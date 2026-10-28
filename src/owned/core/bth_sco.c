#include "bth_sco.h"
#include <string.h>
static bool same(bth_sco_token_t a,bth_sco_token_t b){return a.serial&&a.serial==b.serial&&a.generation==b.generation&&a.count==b.count;}
static bool connected(const bth_sco_t* s){return s&&s->initialized&&s->state==BTH_SCO_CONNECTED;}
static bool link_present(const bth_sco_t* s){return s&&s->initialized&&s->active_handle&&(s->state==BTH_SCO_CONNECTED||s->state==BTH_SCO_DISCONNECTING);}
static void put16(uint8_t* p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static bool token(bth_sco_t* s,uint16_t count,bth_sco_token_t* out){
 if(s->serial==UINT32_MAX){s->state=BTH_SCO_FAILED;return false;}*out=(bth_sco_token_t){s->generation,++s->serial,count};return true;
}
static void clear_audio(bth_sco_t* s){
 s->handle=s->tx_available=s->tx_outstanding=s->pending_completed=s->rx_pacing_bytes=0;s->rx_read=s->rx_count=0;
 s->ready=s->reject=s->command_queued=s->tx_reserved=s->completed_reserved=s->active_handle=s->close_on_complete=false;s->pending_opcode=0;
 memset(s->receive,0,sizeof s->receive);s->command_ticket=(bth_sco_token_t){0};s->tx_ticket=(bth_sco_token_t){0};s->completed_ticket=(bth_sco_token_t){0};
}
bool bth_sco_init(bth_sco_t* s,const bth_sco_config_t* c,uint8_t* storage,size_t bytes){
 if(!s||!c||!c->owner||c->acl_handle>=0x0f00||(c->codec!=BTH_SCO_CVSD&&c->codec!=BTH_SCO_MSBC)||c->rx_slots>BTH_SCO_RX_LIMIT||(c->packet_types&0xfc00)||(c->retransmission_effort>2&&c->retransmission_effort!=0xff)||
    ((size_t)c->rx_slots*c->rx_bytes>bytes)||((c->rx_slots&&c->rx_bytes)&&!storage))return false;
 memset(s,0,sizeof *s);s->config=*c;s->receive_storage=storage;s->receive_bytes=bytes;s->initialized=true;s->state=BTH_SCO_ATTACHED;return true;
}
bth_status_t bth_sco_reconfigure(bth_sco_t* s,const bth_sco_config_t* c,uint8_t* storage,size_t bytes){
 if(!s||!s->initialized||!c)return BTH_ERR_PARAM;
 if(s->state!=BTH_SCO_ATTACHED||s->active_handle||c->owner!=s->config.owner||c->acl_handle!=s->config.acl_handle||memcmp(c->peer_hci_address,s->config.peer_hci_address,6))return BTH_ERR_STATE;
 bth_sco_t next;if(!bth_sco_init(&next,c,storage,bytes))return BTH_ERR_PARAM;
 next.generation=s->generation;next.serial=s->serial;next.rx_packets=s->rx_packets;next.rx_status_errors=s->rx_status_errors;next.rx_overflows=s->rx_overflows;next.tx_submitted=s->tx_submitted;next.rx_pacing_overflows=s->rx_pacing_overflows;next.rx_pacing_rejected_bytes=s->rx_pacing_rejected_bytes;next.last_error=s->last_error;next.local_error=s->local_error;*s=next;return BTH_OK;
}
bool bth_sco_backend_ready(const bth_sco_t* s){
 if(!s||!s->initialized)return false;const bth_sco_config_t* c=&s->config;
 return c->route_confirmed&&c->rx_slots&&c->rx_bytes&&c->controller_tx_bytes&&
  ((c->controller_tx_flow&&c->controller_tx_packets)||(!c->controller_tx_flow&&c->allow_rx_pacing))&&
  // CVSD uses linear16 PCM; host-encoded mSBC is an8-bit transparent stream.
  // Broadcom PCM/I2S wideband settings are a different backend, not this HCI path.
  c->voice_setting==(c->codec==BTH_SCO_CVSD?0x0060:0x0043)&&c->max_latency>=4&&c->packet_types;
}
bth_status_t bth_sco_on_event(bth_sco_t* s,const bth_h4_frame_t* event,bool secure,uint32_t now){
 if(!s||!s->initialized||!event)return BTH_ERR_PARAM;if(event->type!=BTH_H4_EVENT)return BTH_IGNORED;
 const bth_span_t p=event->event.params;
 // Expire before considering replies; an event cannot bypass a missed poll.
 bth_sco_expire(s,now);
 if(!secure){
  if(s->state==BTH_SCO_REQUESTED){s->reject=true;s->ready=false;s->pending_opcode=0x042a;}
  else if(s->state==BTH_SCO_WAIT_STATUS||s->state==BTH_SCO_WAIT_COMPLETE){s->close_on_complete=true;s->close_reason=5;}
  else if(s->state==BTH_SCO_CONNECTED)bth_sco_request_close(s,s->config.owner,s->generation,5,now);
 }
 if(event->event.code==4){
  if(p.len!=10)return BTH_ERR_LENGTH;if(p.data[9]!=0&&p.data[9]!=2)return BTH_IGNORED;
  if(memcmp(p.data,s->config.peer_hci_address,6)||s->state!=BTH_SCO_ATTACHED)return BTH_IGNORED;
  if(s->generation==UINT32_MAX){s->state=BTH_SCO_FAILED;return BTH_ERR_STATE;}++s->generation;
  s->reject=!secure||!bth_sco_backend_ready(s);s->ready=false;s->local_error=0;s->pending_opcode=s->reject?0x042a:0x0429;s->state=BTH_SCO_REQUESTED;s->deadline=now+5000;
  return token(s,0,&s->command_ticket)?BTH_OK:BTH_ERR_STATE;
 }
 if(event->event.code==0x0f){
  if(p.len!=4)return BTH_ERR_LENGTH;const uint16_t opcode=bth_read_le16(p.data+2);
  if(s->state==BTH_SCO_DISCONNECTING&&s->command_queued&&s->pending_opcode==0x0406&&opcode==0x0406){
   s->last_error=p.data[0];s->command_queued=false;s->pending_opcode=0;if(s->last_error)s->state=BTH_SCO_FAILED;return BTH_OK;
  }
  if(s->state!=BTH_SCO_WAIT_STATUS||!s->command_queued||opcode!=s->pending_opcode)return BTH_IGNORED;
  s->last_error=p.data[0];s->command_queued=false;
  if(s->last_error){clear_audio(s);s->state=BTH_SCO_ATTACHED;return BTH_OK;}
  s->state=BTH_SCO_WAIT_COMPLETE;return BTH_OK;
 }
 if(event->event.code==0x2c){
  if(p.len!=17)return BTH_ERR_LENGTH;if(s->state!=BTH_SCO_WAIT_COMPLETE||memcmp(p.data+3,s->config.peer_hci_address,6))return BTH_IGNORED;
  s->last_error=p.data[0];if(s->last_error){clear_audio(s);s->state=BTH_SCO_ATTACHED;return BTH_OK;}
  const uint16_t handle=bth_read_le16(p.data+1);const uint8_t air=s->config.codec==BTH_SCO_CVSD?2:3;
  if(handle>=0x0f00||handle==s->config.acl_handle||(p.data[9]!=0&&p.data[9]!=2)){s->local_error=1;s->state=BTH_SCO_FAILED;return BTH_ERR_STATE;}
  s->handle=handle;s->interval_slots=p.data[10];s->air_mode=p.data[16];s->air_rx_bytes=bth_read_le16(p.data+12);s->air_tx_bytes=bth_read_le16(p.data+14);
  s->tx_available=s->config.controller_tx_flow?s->config.controller_tx_packets:0;s->active_handle=true;s->state=BTH_SCO_CONNECTED;
  // This handle was created by our matching ACCEPT. If authorization or the
  // PCM format no longer permits data, close that actual handle without ever
  // exposing a CONNECTED audio state to the worker.
  if(!secure||p.data[16]!=air){s->local_error=!secure?3:2;return bth_sco_request_close(s,s->config.owner,s->generation,!secure?5:0x13,now);}
  if(s->reject||s->close_on_complete)return bth_sco_request_close(s,s->config.owner,s->generation,s->close_reason?s->close_reason:0x13,now);return BTH_OK;
 }
 if(event->event.code==5){
  if(p.len!=4)return BTH_ERR_LENGTH;
  if(!s->active_handle||bth_read_le16(p.data+1)!=s->handle)return BTH_IGNORED;
  if(p.data[0]){s->last_error=p.data[0];return BTH_IGNORED;}
  const bool failed=s->state==BTH_SCO_FAILED;clear_audio(s);s->state=failed?BTH_SCO_FAILED:BTH_SCO_ATTACHED;return BTH_OK;
 }
 return BTH_IGNORED;
}
bth_status_t bth_sco_audio_ready(bth_sco_t* s,uint32_t owner,uint32_t generation,bool ready){
 if(!s||!s->initialized)return BTH_ERR_PARAM;if(owner!=s->config.owner||generation!=s->generation||s->state!=BTH_SCO_REQUESTED||s->reject)return BTH_IGNORED;
 s->ready=ready;if(!ready){s->reject=true;s->pending_opcode=0x042a;}return BTH_OK;
}
bth_status_t bth_sco_next_command(const bth_sco_t* s,bth_sco_command_t* out){
 if(!s||!s->initialized||!out)return BTH_ERR_PARAM;memset(out,0,sizeof *out);
 if(s->state==BTH_SCO_DISCONNECTING){
  if(s->command_queued||s->pending_opcode!=0x0406||s->pending_completed||s->completed_reserved)return BTH_MORE;
  out->ticket=s->command_ticket;out->opcode=0x0406;out->length=3;put16(out->parameters,s->handle);out->parameters[2]=s->close_reason;return BTH_OK;
 }
 if(s->state!=BTH_SCO_REQUESTED||s->command_queued||(!s->ready&&!s->reject))return BTH_MORE;
 out->ticket=s->command_ticket;out->opcode=s->pending_opcode;memcpy(out->parameters,s->config.peer_hci_address,6);
 if(s->reject){out->length=7;out->parameters[6]=0x0d;return BTH_OK;}
 out->length=21;put32(out->parameters+6,8000);put32(out->parameters+10,8000);put16(out->parameters+14,s->config.max_latency);put16(out->parameters+16,s->config.voice_setting);out->parameters[18]=s->config.retransmission_effort;put16(out->parameters+19,s->config.packet_types);return BTH_OK;
}
bth_status_t bth_sco_command_queued(bth_sco_t* s,bth_sco_token_t ticket,uint32_t now){
 if(!s||!s->initialized)return BTH_ERR_PARAM;bth_sco_expire(s,now);if(!same(ticket,s->command_ticket)||s->command_queued)return BTH_IGNORED;
 if(s->state==BTH_SCO_DISCONNECTING&&s->pending_opcode==0x0406){if(s->pending_completed||s->completed_reserved)return BTH_MORE;s->command_queued=true;return BTH_OK;}
 if(s->state!=BTH_SCO_REQUESTED)return BTH_IGNORED;
 if(!s->reject&&!s->ready)return BTH_MORE;s->command_queued=true;s->state=BTH_SCO_WAIT_STATUS;return BTH_OK;
}
bth_status_t bth_sco_expire(bth_sco_t* s,uint32_t now){
 if(!s||!s->initialized)return BTH_ERR_PARAM;
 if((s->state!=BTH_SCO_REQUESTED&&s->state!=BTH_SCO_WAIT_STATUS&&s->state!=BTH_SCO_WAIT_COMPLETE&&s->state!=BTH_SCO_DISCONNECTING)||((int32_t)(now-s->deadline)<0))return BTH_MORE;
 s->state=BTH_SCO_FAILED;return BTH_OK; // Unknown command completion is terminal on this ACL generation.
}
bth_status_t bth_sco_completed(bth_sco_t* s,uint16_t handle,uint16_t count){
 if(!link_present(s)||handle!=s->handle||!s->config.controller_tx_flow)return BTH_IGNORED;
 if(count>s->tx_outstanding||count>s->config.controller_tx_packets-s->tx_available)return BTH_ERR_STATE;
 s->tx_outstanding=(uint16_t)(s->tx_outstanding-count);s->tx_available=(uint16_t)(s->tx_available+count);return BTH_OK;
}
bth_status_t bth_sco_receive(bth_sco_t* s,uint32_t owner,const bth_sco_view_t* packet){
 if(!s||!s->initialized||!packet)return BTH_ERR_PARAM;if(!link_present(s)||owner!=s->config.owner||packet->handle!=s->handle)return BTH_IGNORED;
 if(packet->packet_status>3||packet->payload.len>s->config.rx_bytes||(packet->payload.len&&!packet->payload.data)||(s->config.codec==BTH_SCO_CVSD&&!packet->packet_status&&(packet->payload.len&1)))return BTH_ERR_LENGTH;
 const bool pacing=!s->config.controller_tx_flow&&s->config.allow_rx_pacing;
 // This selected host pipeline consumes complete S16 samples. A malformed
 // odd error payload is not permission to invent half-sample TX progress.
 if(pacing&&s->config.codec==BTH_SCO_CVSD&&(packet->payload.len&1))return BTH_ERR_LENGTH;
 if(s->state==BTH_SCO_DISCONNECTING){if(s->config.host_rx_flow){if(s->pending_completed==UINT16_MAX){s->state=BTH_SCO_FAILED;return BTH_ERR_STATE;}++s->pending_completed;}return BTH_OK;}
 if(s->rx_count==s->config.rx_slots){++s->rx_overflows;return BTH_ERR_CAPACITY;}
 if(pacing){
  const size_t limit=(size_t)s->config.rx_slots*s->config.rx_bytes+BTH_SCO_HOST_UNIT_BYTES-1;
  if(packet->payload.len>limit-s->rx_pacing_bytes){
   if(s->rx_pacing_overflows!=UINT32_MAX)++s->rx_pacing_overflows;
   const uint32_t rejected=(uint32_t)packet->payload.len;
   s->rx_pacing_rejected_bytes=rejected>UINT32_MAX-s->rx_pacing_rejected_bytes?UINT32_MAX:s->rx_pacing_rejected_bytes+rejected;
   return BTH_ERR_CAPACITY; // Outer owner retires voice; no accepted grant is lost.
  }
 }
 const unsigned slot=(s->rx_read+s->rx_count)%s->config.rx_slots;const size_t n=packet->payload.len;bth_sco_token_t ticket;if(!token(s,0,&ticket))return BTH_ERR_STATE;
 if(n)memcpy(s->receive_storage+slot*s->config.rx_bytes,packet->payload.data,n);s->receive[slot]=(bth_sco_rx_slot_t){(uint16_t)n,packet->packet_status,ticket};++s->rx_count;++s->rx_packets;if(packet->packet_status)++s->rx_status_errors;
 if(pacing)s->rx_pacing_bytes=(uint16_t)(s->rx_pacing_bytes+n);return BTH_OK;
}
bth_status_t bth_sco_peek_rx(const bth_sco_t* s,uint32_t owner,uint32_t generation,bth_sco_rx_view_t* out){
 if(out)memset(out,0,sizeof *out);if(!s||!s->initialized||!out)return BTH_ERR_PARAM;if(!connected(s)||owner!=s->config.owner||generation!=s->generation)return BTH_IGNORED;if(!s->rx_count)return BTH_MORE;
 const bth_sco_rx_slot_t* slot=&s->receive[s->rx_read];out->payload=(bth_span_t){s->receive_storage+s->rx_read*s->config.rx_bytes,slot->length};out->packet_status=slot->status;out->ticket=slot->ticket;return BTH_OK;
}
bth_status_t bth_sco_consume_rx(bth_sco_t* s,bth_sco_token_t ticket){
 if(!connected(s)||!s->rx_count||!same(ticket,s->receive[s->rx_read].ticket))return BTH_IGNORED;
 memset(&s->receive[s->rx_read],0,sizeof s->receive[0]);s->rx_read=(uint8_t)((s->rx_read+1)%s->config.rx_slots);--s->rx_count;if(s->config.host_rx_flow)++s->pending_completed;return BTH_OK;
}
bth_status_t bth_sco_prepare_tx(bth_sco_t* s,uint32_t owner,uint32_t generation,const uint8_t* data,size_t n,uint8_t* out,size_t capacity,size_t* written,bth_sco_token_t* ticket){
 if(written)*written=0;if(ticket)*ticket=(bth_sco_token_t){0};if(!s||!s->initialized||!data||!n||!out||!written||!ticket)return BTH_ERR_PARAM;
 if(!connected(s)||owner!=s->config.owner||generation!=s->generation)return BTH_IGNORED;
 if(n>s->config.controller_tx_bytes||(s->config.codec==BTH_SCO_CVSD&&(n&1)))return BTH_ERR_LENGTH;if(capacity<n+4)return BTH_ERR_CAPACITY;
 if(s->tx_reserved||(s->config.controller_tx_flow?!s->tx_available:n>s->rx_pacing_bytes))return BTH_MORE;
 if(!token(s,(uint16_t)n,&s->tx_ticket))return BTH_ERR_STATE;out[0]=3;put16(out+1,s->handle);out[3]=(uint8_t)n;memcpy(out+4,data,n);*written=n+4;*ticket=s->tx_ticket;s->tx_reserved=true;return BTH_OK;
}
bth_status_t bth_sco_commit_tx(bth_sco_t* s,bth_sco_token_t ticket){
 if(!connected(s)||!s->tx_reserved||!same(ticket,s->tx_ticket))return BTH_IGNORED;
 if(s->config.controller_tx_flow){if(!s->tx_available)return BTH_ERR_STATE;--s->tx_available;++s->tx_outstanding;}
 else {if(ticket.count>s->rx_pacing_bytes)return BTH_ERR_STATE;s->rx_pacing_bytes=(uint16_t)(s->rx_pacing_bytes-ticket.count);}
 s->tx_reserved=false;++s->tx_submitted;return BTH_OK;
}
bth_status_t bth_sco_prepare_completed(bth_sco_t* s,uint8_t* out,size_t capacity,size_t* written,bth_sco_token_t* ticket){
 if(written)*written=0;if(ticket)*ticket=(bth_sco_token_t){0};if(!s||!s->initialized||!out||!written||!ticket)return BTH_ERR_PARAM;
 if(!link_present(s)||!s->config.host_rx_flow||!s->pending_completed||s->completed_reserved)return BTH_MORE;if(capacity<9)return BTH_ERR_CAPACITY;
 if(!token(s,s->pending_completed,&s->completed_ticket))return BTH_ERR_STATE;out[0]=1;put16(out+1,0x0c35);out[3]=5;out[4]=1;put16(out+5,s->handle);put16(out+7,s->pending_completed);*written=9;*ticket=s->completed_ticket;s->completed_reserved=true;return BTH_OK;
}
bth_status_t bth_sco_commit_completed(bth_sco_t* s,bth_sco_token_t ticket){
 if(!link_present(s)||!s->completed_reserved||!same(ticket,s->completed_ticket))return BTH_IGNORED;if(ticket.count>s->pending_completed)return BTH_ERR_STATE;
 s->pending_completed=(uint16_t)(s->pending_completed-ticket.count);s->completed_reserved=false;return BTH_OK;
}
bth_status_t bth_sco_request_close(bth_sco_t* s,uint32_t owner,uint32_t generation,uint8_t reason,uint32_t now){
 if(!s||!s->initialized)return BTH_ERR_PARAM;if(owner!=s->config.owner||generation!=s->generation)return BTH_IGNORED;
 if(s->state==BTH_SCO_REQUESTED){s->reject=true;s->ready=false;s->pending_opcode=0x042a;return BTH_OK;}
 if(s->state==BTH_SCO_WAIT_STATUS||s->state==BTH_SCO_WAIT_COMPLETE){s->close_on_complete=true;s->close_reason=reason?reason:0x13;return BTH_OK;}
 if(s->state==BTH_SCO_DISCONNECTING)return BTH_OK;if(!connected(s))return BTH_IGNORED;
 while(s->rx_count){const bth_sco_token_t old=s->receive[s->rx_read].ticket;if(bth_sco_consume_rx(s,old)!=BTH_OK){s->state=BTH_SCO_FAILED;return BTH_ERR_STATE;}}
 s->tx_reserved=false;s->rx_pacing_bytes=0;s->state=BTH_SCO_DISCONNECTING;s->pending_opcode=0x0406;s->command_queued=false;s->close_reason=reason?reason:0x13;s->deadline=now+5000;
 return token(s,0,&s->command_ticket)?BTH_OK:BTH_ERR_STATE;
}
