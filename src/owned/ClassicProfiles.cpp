#include "ClassicProfiles.h"
#include "../platform/RadioProtocol.h"
#include <string.h>
#include <stdio.h>
namespace giga_bredr { namespace detail {
namespace {
constexpr uint8_t CustomStatus=0x80,CustomMetadata=0x81,CustomAbort=0x82;
// The imported CT uses pending kinds 1..3. These adapter-owned kinds share its
// label allocator and expiry/retirement table, so late replies cannot match a
// newer metadata/status transaction after a timeout.
constexpr uint8_t WorkStatus=1,WorkMetadata=2;
constexpr uint8_t UnsupportedConfiguration=0x29,BadState=0x31;
const uint8_t sbc[]={0,0,0x3f,0xff,2,250};
const uint8_t aac[]={0,2,0xc0,0x01,0x8c,0x80,0,0};
const uint8_t he[]={0,2,0x08,0x01,0x8c,0x80,0,0};
const uint8_t aptx[]={0,255,0x4f,0,0,0,1,0,0x32};
const uint8_t hd[]={0,255,0xd7,0,0,0,0x24,0,0x32,0,0,0,0};
const uint8_t ldac[]={0,255,0x2d,1,0,0,0xaa,0,0x3c,1};
const uint8_t lhdc[]={0,255,0x3a,5,0,0,0x35,0x4c,0x35,6,0x11,0,0};
const uint8_t android_opus[]={0,255,0xe0,0,0,0,1,0,0x92};
const uint8_t pipewire_opus[]={0,255,0xf1,5,0,0,5,0x10,2,1,3,0,0,0,0x1c,0,0,0,0,0,0,0,0,0,0,0};
struct Definition {Codec codec;const uint8_t* caps;size_t length;};
const Definition definitions[]={
 {Codec::Sbc,sbc,sizeof sbc},{Codec::Aac,aac,sizeof aac},{Codec::AacHe,he,sizeof he},
 {Codec::Aptx,aptx,sizeof aptx},{Codec::AptxHd,hd,sizeof hd},{Codec::Ldac,ldac,sizeof ldac},
 {Codec::LhdcV5,lhdc,sizeof lhdc},{Codec::Opus,android_opus,sizeof android_opus},{Codec::Opus,pipewire_opus,sizeof pipewire_opus}
};
uint8_t validate(bth_span_t selected,bth_span_t capabilities) {
 if(!selected.data||!capabilities.data||selected.len!=capabilities.len||selected.len<2||selected.data[0]!=0) return UnsupportedConfiguration;
 const Definition* definition=nullptr;
 for(const auto& d:definitions) if(d.length==capabilities.len&&!memcmp(d.caps,capabilities.data,d.length)){definition=&d;break;}
 if(!definition||selected.data[1]!=capabilities.data[1]) return UnsupportedConfiguration;
 if(definition->codec==Codec::Sbc&&bth_avdtp_sbc_validate(selected,capabilities)) return UnsupportedConfiguration;
 CodecConfig config;
 if(parseRadioCodec(selected.data[1],selected.data+2,selected.len-2,config)!=Status::Ok||
    !radioEndpointAccepts(config,definition->codec,capabilities.data[1],capabilities.data+2,capabilities.len-2)) return UnsupportedConfiguration;
 return 0;
}
bool due(uint32_t now,uint32_t deadline){return deadline&&int32_t(now-deadline)>=0;}
size_t minimum(size_t a,size_t b){return a<b?a:b;}
uint16_t be16(const uint8_t* p){return uint16_t((uint16_t(p[0])<<8)|p[1]);}
uint32_t be32(const uint8_t* p){return (uint32_t(be16(p))<<16)|be16(p+2);}
void vendor(uint8_t* out,uint8_t label,uint8_t type,uint8_t pdu,size_t size){
 const uint8_t header[]={0,0x11,0x0e,type,0x48,0,0,0x19,0x58,pdu,0,uint8_t(size>>8),uint8_t(size)};
 memcpy(out,header,sizeof header);out[0]=uint8_t(label<<4);
}
}
Status ClassicProfiles::begin(uint32_t mask,const RadioCallbacks& cb,bth_control_tx_t& q,uint8_t peer) {
 if(!(mask&codecBit(Codec::Sbc))||(mask&~AllCodecs)||!cb.configure||!q.initialized||peer>=BTH_CONTROL_PEERS) return Status::InvalidArgument;
 if(active_) stop(true);
 *this=ClassicProfiles();callbacks_=cb;replies_=&q;peer_=peer;mask_=mask;active_=true;
 for(unsigned i=0;i<sizeof definitions/sizeof definitions[0];++i)if(mask&codecBit(definitions[i].codec)) {
  const auto& d=definitions[i];endpoints_[endpoint_count_++]={uint8_t(i+1),{d.caps,d.length},validate,false};
 }
 size_t count=0;const auto* records=bth_sdp_speaker_records(&count);
 if(count!=3) {active_=false;return Status::Unsupported;}
 records_[record_count_++]=records[0];
 if(cb.volume)records_[record_count_++]=records[1];
 records_[record_count_++]=records[2];reset(0);return Status::Ok;
}
void ClassicProfiles::reset(uint32_t generation) {
 if(!active_)return;
 stop(true);generation_=generation;signal_=media_=avrcp_=0;close_media_=close_avrcp_=false;
 for(auto& c:channels_)c=Channel();
 bth_avdtp_init(&avdtp_,endpoints_,endpoint_count_,avdtp_assembly_,sizeof avdtp_assembly_);
 bth_sdp_init(&sdp_,records_,record_count_,generation);
 bth_avrcp_init(&target_,control_status_.volume);bth_avrcp_ct_init(&controller_);
 control_status_.connected=false;control_status_.playback_status=0xff;control_status_.song_length_ms=control_status_.song_position_ms=0xffffffffu;
 avdtp_deadline_=target_deadline_=controller_deadline_=custom_deadline_=metadata_deadline_=0;
 work_=0;capabilities_sent_=metadata_active_=metadata_continue_=metadata_abort_=custom_active_=false;
 metadata_used_=custom_used_=0;metadata_fragments_=custom_remaining_=0;
}
bool ClassicProfiles::validOwner()const {
 return active_&&generation_&&replies_&&replies_->initialized&&replies_->peers[peer_].attached&&replies_->peers[peer_].owner==generation_;
}
ClassicProfiles::Channel* ClassicProfiles::channel(uint16_t cid){for(auto& c:channels_)if(c.local==cid&&cid)return &c;return nullptr;}
bool ClassicProfiles::channelOpened(uint16_t psm,uint16_t local,uint16_t remote,uint16_t mtu){
 if(!validOwner()||local<0x40||remote<0x40||mtu<48||channel(local))return false;
 if(psm!=1&&psm!=0x19&&psm!=0x17)return false;
 if(psm==0x19&&!signal_&&abortTransportPending())return false;
 if(psm==0x17&&avrcp_)return false;
 if(psm==0x19&&signal_&&!wantsMediaChannel())return false;
 Channel* empty=nullptr;for(auto& c:channels_)if(!c.local){empty=&c;break;}if(!empty)return false;
 *empty={psm,local,remote,mtu,{}};
 if(psm==0x19){if(!signal_)signal_=local;else {media_=local;bth_avdtp_media_connected(&avdtp_);}}
 if(psm==0x17){
  avrcp_=local;control_status_.connected=true;bth_avrcp_ct_init(&controller_);bth_avrcp_init(&target_,control_status_.volume);
  work_=WorkStatus|(callbacks_.metadata?WorkMetadata:0);capabilities_sent_=false;
  metadata_active_=metadata_continue_=metadata_abort_=custom_active_=false;metadata_used_=custom_used_=0;
 }
 return true;
}
void ClassicProfiles::channelClosed(uint16_t cid){
 auto* c=channel(cid);if(!c)return;
 if(cid==signal_){signal_=0;stop(true);if(avdtp_.state!=BTH_AVDTP_ABORTING)bth_avdtp_transport_closed(&avdtp_);if(media_)close_media_=true;}
 if(cid==media_){media_=0;stop(true);if(avdtp_.state!=BTH_AVDTP_ABORTING)bth_avdtp_transport_closed(&avdtp_);close_media_=false;}
 if(cid==avrcp_){avrcp_=0;control_status_.connected=false;work_=0;bth_avrcp_ct_init(&controller_);bth_avrcp_init(&target_,control_status_.volume);metadata_active_=metadata_continue_=metadata_abort_=custom_active_=false;close_avrcp_=false;}
 *c=Channel();
}
void ClassicProfiles::stop(bool invalidate){
 playing_=false;if(invalidate)valid_=false;
 if(callbacks_.stream)callbacks_.stream(false,callbacks_.user);
}
void ClassicProfiles::diagnose(const char* text){if(callbacks_.diagnostic)callbacks_.diagnostic(text,callbacks_.user);}
bool ClassicProfiles::finish(const Channel& c,bth_control_reservation_t& r,bth_status_t status,size_t n){
 if(status>=0&&n){
  if(bth_control_tx_commit(replies_,r.token,c.remote,n,c.mtu)!=BTH_OK){stop(true);diagnose("Owned Classic reply commit failed");return true;}
 }else bth_control_tx_cancel(replies_,r.token);
 if(status<0)++control_status_.malformed_events;
 return true;
}
bool ClassicProfiles::signaling(Channel& c,const uint8_t* p,size_t n,bool secure,uint32_t now,bth_control_reservation_t& r){
 bth_avdtp_t next=avdtp_;uint32_t actions=0;size_t written=0;
 bth_status_t status=bth_avdtp_receive(&next,p,n,secure,r.data,minimum(r.capacity,c.mtu),&written,&actions);
 if(status>=0){
  if(actions&BTH_AVDTP_CONFIGURED_ACTION){
   CodecConfig proposed;Status prepared=parseRadioCodec(next.codec[1],next.codec+2,next.codec_len-2,proposed);
   if(prepared==Status::Ok)prepared=callbacks_.configure(proposed,callbacks_.user);
   if(prepared!=Status::Ok){
    stop(true);r.data[0]=uint8_t((r.data[0]&0xfc)|3);r.data[2]=7;r.data[3]=UnsupportedConfiguration;written=4;
    diagnose("A2DP configuration prepare rejected");
    avdtp_.rx_active=false;avdtp_.rx_used=0;avdtp_.rx_remaining=0;
    return finish(c,r,status,written);
   }
   accepted_=proposed;valid_=true;playing_=false;
  }
  if(actions&BTH_AVDTP_START_AUDIO){
   Status prepared=valid_?callbacks_.configure(accepted_,callbacks_.user):Status::Unsupported;
   if(prepared!=Status::Ok){
    stop(false);r.data[0]=uint8_t((r.data[0]&0xfc)|3);r.data[2]=uint8_t(next.local_seid<<2);r.data[3]=BadState;written=4;
    avdtp_.rx_active=false;avdtp_.rx_used=0;avdtp_.rx_remaining=0;
    diagnose("A2DP START prepare rejected");return finish(c,r,status,written);
   }
  }
  avdtp_=next;
  if(actions&(BTH_AVDTP_CLOSE_TRANSPORT|BTH_AVDTP_ABORT_AUDIO)){stop(true);if(media_)close_media_=true;}
  else if(actions&BTH_AVDTP_SUSPEND_AUDIO)stop(false);
  if(avdtp_.rx_active)avdtp_deadline_=now+2000;
  // Transport visibility of ACCEPT precedes publication of the playing state.
  bool result=finish(c,r,status,written);
  if((actions&BTH_AVDTP_START_AUDIO)&&valid_){playing_=true;if(callbacks_.stream)callbacks_.stream(true,callbacks_.user);}
  return result;
 }
 // Kernels retire malformed fragment assembly even when no stream transaction
 // can commit. Keep that retirement while preserving accepted stream state.
 avdtp_.rx_active=next.rx_active;avdtp_.rx_used=next.rx_used;avdtp_.rx_remaining=next.rx_remaining;avdtp_.rx_packet_size=next.rx_packet_size;
 return finish(c,r,status,written);
}
bool ClassicProfiles::receive(uint16_t cid,const uint8_t* p,size_t n,bool secure,uint32_t now){
 if(!validOwner())return true;
 auto* c=channel(cid);if(!c)return true;
 if((!p&&n)||!n){++control_status_.malformed_events;return true;}
 if(cid==media_){
  if(!secure||!valid_||!playing_||avdtp_.state!=BTH_AVDTP_STREAMING){++dropped_;return true;}
  ++received_;bytes_+=uint32_t(n);if(callbacks_.media)callbacks_.media(p,n,callbacks_.user);return true;
 }
 if(cid==avrcp_&&(p[0]&2)){controllerResponse(p,n,secure,now);return true;}
 bth_control_reservation_t r;bth_status_t reserved=bth_control_tx_reserve(replies_,peer_,&r);
 if(reserved!=BTH_OK)return reserved!=BTH_ERR_CAPACITY;
 if(cid==signal_)return signaling(*c,p,n,secure,now,r);
 size_t written=0;bth_status_t status=BTH_IGNORED;
 if(c->psm==1)status=bth_sdp_process_compact(&sdp_,&c->cursor,p,n,r.data,minimum(r.capacity,c->mtu),&written);
 else if(cid==avrcp_&&callbacks_.volume&&!close_avrcp_){
  bth_avrcp_t next=target_;uint32_t actions=0;
  status=bth_avrcp_receive(&next,p,n,secure,r.data,minimum(r.capacity,c->mtu),&written,&actions);
  if(status>=0){
   if(actions&BTH_AVRCP_VOLUME){
    const uint8_t packet=(p[0]>>2)&3;const uint8_t* body=packet==0&&n>=8?p+3:packet==3&&target_.rx_active?next.rx:nullptr;
    if(body&&body[2]==0x7c&&!(body[3]&0x80)){
     uint8_t v=target_.volume;next.volume=(body[3]&0x7f)==0x41?uint8_t(v>119?127:v+8):uint8_t(v<8?0:v-8);
    }
   }
   target_=next;if(target_.rx_active)target_deadline_=now+2000;
   if(actions&BTH_AVRCP_VOLUME){control_status_.volume=target_.volume;callbacks_.volume(target_.volume,callbacks_.user);}
  } else {
   // Keep the kernel's malformed-fragment retirement even when no command or
   // volume change commits. A later END must not revive that abandoned START.
   target_.rx_active=next.rx_active;target_.rx_used=next.rx_used;target_.rx_remaining=next.rx_remaining;
   // A complete but invalid command body is consumed too. Preserve assembly
   // only for short-output retry; input-overflow CAPACITY already expires it.
   if(status!=BTH_ERR_CAPACITY)bth_avrcp_expire(&target_);
  }
 }
 return finish(*c,r,status,written);
}
bool ClassicProfiles::wantsMediaChannel()const{return validOwner()&&signal_&&!media_&&valid_&&avdtp_.state==BTH_AVDTP_OPENING;}
bool ClassicProfiles::takeCloseMedia(){bool value=close_media_;close_media_=false;return value;}
bool ClassicProfiles::takeCloseAvrcp(){bool value=close_avrcp_;close_avrcp_=false;return value;}
uint16_t ClassicProfiles::signalCid()const{return signal_;}
uint16_t ClassicProfiles::mediaCid()const{return media_;}
uint16_t ClassicProfiles::avrcpCid()const{return avrcp_;}
bool ClassicProfiles::configured()const{return valid_;}
bool ClassicProfiles::streaming()const{return playing_;}
bool ClassicProfiles::abortTransportPending()const{return validOwner()&&avdtp_.state==BTH_AVDTP_ABORTING;}
void ClassicProfiles::abortTransportRetired(){if(abortTransportPending()&&!media_)bth_avdtp_transport_closed(&avdtp_);}
const CodecConfig& ClassicProfiles::acceptedConfiguration()const{return accepted_;}
AvrcpStatus ClassicProfiles::avrcpStatus()const{return control_status_;}
uint32_t ClassicProfiles::receivedPackets()const{return received_;}
uint32_t ClassicProfiles::receivedBytes()const{return bytes_;}
uint32_t ClassicProfiles::droppedPackets()const{return dropped_;}
Status ClassicProfiles::setVolume(uint8_t v){
 if(v>127)return Status::InvalidArgument;
 if(!active_||!callbacks_.volume)return Status::Busy;
 if(v!=control_status_.volume){control_status_.volume=v;bth_avrcp_set_volume(&target_,v);callbacks_.volume(v,callbacks_.user);}return Status::Ok;
}
bool ClassicProfiles::customRequest(uint8_t kind,uint8_t pdu,uint32_t now,bth_control_reservation_t& r,size_t cap,size_t& n){
 const size_t params=pdu==0x20?25:(pdu==0x40||pdu==0x41)?1:0;
 if(cap<13+params)return false;
 for(const auto& pending:controller_.pending)if(pending.kind==kind)return false;
 int label=-1;for(unsigned i=0;i<16;++i){unsigned candidate=(controller_.next_label+i)&15;if(!controller_.pending[candidate].kind&&!(controller_.retired_labels&(1u<<candidate))){label=int(candidate);break;}}
 if(label<0)return false;
 vendor(r.data,uint8_t(label),pdu==0x40||pdu==0x41?0:1,pdu,params);
 if(pdu==0x20){memset(r.data+13,0,8);r.data[21]=4;const uint8_t attrs[]={1,2,3,7};for(unsigned i=0;i<4;++i){memset(r.data+22+4*i,0,3);r.data[25+4*i]=attrs[i];}}
 else if(params)r.data[13]=0x20;
 auto& pending=controller_.pending[label];pending={};pending.kind=kind;pending.deadline=now+3000;controller_.next_label=uint8_t((label+1)&15);n=13+params;return true;
}
void ClassicProfiles::poll(bool secure,uint32_t now,bool allow_transmit){
 if(!validOwner())return;
 if(!secure&&playing_)stop(false);
 if(avdtp_.rx_active&&due(now,avdtp_deadline_)){uint32_t actions;bth_avdtp_expire(&avdtp_,BTH_AVDTP_EXPIRE_ASSEMBLY,&actions);}
 if(target_.rx_active&&due(now,target_deadline_))bth_avrcp_expire(&target_);
 if(controller_.rx_active&&due(now,controller_deadline_))bth_avrcp_ct_expire_assembly(&controller_);
 if(custom_active_&&due(now,custom_deadline_)){custom_active_=false;custom_used_=0;++control_status_.malformed_events;}
 uint16_t metadata_labels=0;
 for(unsigned i=0;i<16;++i)if(controller_.pending[i].kind==CustomMetadata)metadata_labels|=uint16_t(1u<<i);
 uint16_t expired=bth_avrcp_ct_expire(&controller_,now);
 if(expired&metadata_labels){metadata_abort_=metadata_active_;metadata_active_=metadata_continue_=false;metadata_used_=0;}
 if(metadata_active_&&due(now,metadata_deadline_)){
  for(unsigned i=0;i<16;++i)if(controller_.pending[i].kind==CustomMetadata){controller_.pending[i]={};controller_.retired_labels|=uint16_t(1u<<i);}
  metadata_abort_=true;metadata_active_=metadata_continue_=custom_active_=false;metadata_used_=custom_used_=0;
 }
 if(controller_.protocol_rejected||controller_.retired_labels==0xffff){close_avrcp_=true;return;}
 auto* c=channel(avrcp_);if(!allow_transmit||!secure||!c||close_avrcp_)return;
 bth_control_reservation_t r;if(bth_control_tx_reserve(replies_,peer_,&r)!=BTH_OK)return;
 size_t n=0;const size_t cap=minimum(r.capacity,c->mtu);bth_status_t status=BTH_MORE;
 if(callbacks_.volume&&target_.notification_due)status=bth_avrcp_notification(&target_,r.data,cap,&n);
 else if(metadata_abort_){if(customRequest(CustomAbort,0x41,now,r,cap,n)){metadata_abort_=false;status=BTH_OK;}}
 else if(metadata_continue_){if(customRequest(CustomMetadata,0x40,now,r,cap,n)){metadata_continue_=false;status=BTH_OK;}}
 else if(!capabilities_sent_){status=bth_avrcp_ct_capabilities(&controller_,true,now,r.data,cap,&n);if(status==BTH_OK)capabilities_sent_=true;}
 else if(work_&WorkStatus){if(customRequest(CustomStatus,0x30,now,r,cap,n)){work_&=uint8_t(~WorkStatus);status=BTH_OK;}}
 else if(work_&WorkMetadata){if(customRequest(CustomMetadata,0x20,now,r,cap,n)){work_&=uint8_t(~WorkMetadata);metadata_active_=false;metadata_used_=0;metadata_fragments_=0;status=BTH_OK;}}
 else if(controller_.capabilities_known){
  const uint8_t events[]={1,2};for(uint8_t event:events)if((controller_.supported_events&(1u<<event))&&!((controller_.disabled_events|controller_.registered_events)&(1u<<event))){status=bth_avrcp_ct_register(&controller_,true,event,0,now,r.data,cap,&n);break;}
 }
 finish(*c,r,status,n);
}
bool ClassicProfiles::emitMetadata(const uint8_t* p,size_t n){
 if(!n)return false;
 size_t pos=1;
 for(unsigned i=0;i<p[0];++i){if(n-pos<8)return false;size_t length=be16(p+pos+6);if(length>n-pos-8)return false;pos+=8+length;}
 if(pos!=n)return false;
 pos=1;for(unsigned i=0;i<p[0];++i){uint32_t attribute=be32(p+pos);uint16_t charset=be16(p+pos+4);size_t length=be16(p+pos+6);const uint8_t* text=p+pos+8;
  if(callbacks_.metadata&&attribute>=1&&attribute<=7&&charset==106){
   size_t copied=minimum(length,127);if(copied<length&&(text[copied]&0xc0)==0x80)while(copied&&(text[copied]&0xc0)==0x80)--copied;
   char value[128];memcpy(value,text,copied);value[copied]=0;callbacks_.metadata(uint8_t(attribute),value,copied,callbacks_.user);
  }pos+=8+length;
 }return true;
}
void ClassicProfiles::customResponse(uint8_t label,const uint8_t* body,size_t length,uint32_t now){
 auto& pending=controller_.pending[label];
 if(length<10||body[1]!=0x48||body[2]||body[3]||body[4]!=0x19||body[5]!=0x58||body[7]>3||be16(body+8)!=length-10){++control_status_.malformed_events;return;}
 const uint8_t type=body[0]&15,pdu=body[6],packet=body[7];const uint8_t* data=body+10;size_t n=length-10;
 const uint8_t expected=pending.kind==CustomStatus?0x30:pending.kind==CustomAbort?0x41:0x20;
 if(pdu!=expected){++control_status_.malformed_events;return;}
 if(type==8||type==10){if(type==10&&n!=1){++control_status_.malformed_events;return;}pending={};metadata_active_=metadata_continue_=metadata_abort_=false;metadata_used_=0;return;}
 if(type!=12){++control_status_.malformed_events;return;}
 if(pending.kind==CustomStatus){
  if(pdu!=0x30||packet||n!=9||(data[8]>4&&data[8]!=255)){++control_status_.malformed_events;return;}
  control_status_.song_length_ms=be32(data);control_status_.song_position_ms=be32(data+4);control_status_.playback_status=data[8];pending={};return;
 }
 if(pending.kind==CustomAbort){if(pdu!=0x41||packet||n){++control_status_.malformed_events;return;}pending={};return;}
 if(pdu!=0x20){++control_status_.malformed_events;return;}
 if(packet==0){if(metadata_active_||!emitMetadata(data,n)){++control_status_.malformed_events;return;}pending={};return;}
 if((packet==1&&metadata_active_)||(packet>=2&&!metadata_active_)){++control_status_.malformed_events;return;}
 if(packet==1){metadata_active_=true;metadata_used_=0;metadata_fragments_=0;}
 pending={};
 if(++metadata_fragments_>8||n>sizeof metadata_bytes_-metadata_used_){metadata_active_=false;metadata_continue_=false;metadata_abort_=true;metadata_used_=0;++control_status_.malformed_events;return;}
 memcpy(metadata_bytes_+metadata_used_,data,n);metadata_used_+=n;
 metadata_deadline_=now+3000;
 if(packet==3){if(!emitMetadata(metadata_bytes_,metadata_used_))++control_status_.malformed_events;metadata_active_=false;metadata_used_=0;}
 else metadata_continue_=true;
}
void ClassicProfiles::controllerResponse(const uint8_t* p,size_t n,bool secure,uint32_t now){
 if(!secure||close_avrcp_)return;
 uint8_t label=p[0]>>4,packet=(p[0]>>2)&3;
 if(controller_.pending[label].kind<CustomStatus){
  bth_avrcp_ct_event_t event;auto status=bth_avrcp_ct_receive(&controller_,p,n,true,&event);
  if(status<0){++control_status_.malformed_events;return;}
  if(controller_.rx_active)controller_deadline_=now+2000;
  if((event.actions&BTH_AVRCP_CT_NOTIFICATION)&&!(event.actions&BTH_AVRCP_CT_REJECTED)){
   if(event.event==1){control_status_.playback_status=event.play_status;if(!event.interim)work_|=WorkStatus;}
   if(event.event==2&&!event.interim)work_|=WorkStatus|(callbacks_.metadata?WorkMetadata:0);
  }return;
 }
 const size_t header=packet==1?4:packet==0?3:1;
 if(n<header||(p[0]&1)||(packet<2&&(p[header-2]!=0x11||p[header-1]!=0x0e))){++control_status_.malformed_events;return;}
 if(!packet){customResponse(label,p+3,n-3,now);return;}
 const uint8_t* body=p+header;size_t count=n-header;
 if(packet==1){if(p[1]<2||custom_active_||!count){++control_status_.malformed_events;return;}custom_active_=true;custom_label_=label;custom_remaining_=uint8_t(p[1]-1);custom_used_=0;}
 else if(!custom_active_||custom_label_!=label||(packet==2&&custom_remaining_<=1)||(packet==3&&custom_remaining_!=1)){custom_active_=false;custom_used_=0;++control_status_.malformed_events;return;}
 else --custom_remaining_;
 if(!count||count>sizeof custom_bytes_-custom_used_){custom_active_=false;custom_used_=0;++control_status_.malformed_events;return;}
 memcpy(custom_bytes_+custom_used_,body,count);custom_used_+=count;custom_deadline_=now+2000;
 if(packet==3){custom_active_=false;customResponse(label,custom_bytes_,custom_used_,now);custom_used_=0;}
}
}}
