#include "OwnedVoiceAudio.h"
#include "../CodecBuildConfig.h"
#include <cstring>
namespace giga_bredr { namespace detail {
Status OwnedVoiceAudio::begin(const VoiceAudioHostConfig& c,const VoiceAudioHostHooks& h,uint8_t* storage,size_t bytes){
 if(configured_)return Status::Busy;
 if(!h.prepare||!h.ended||!h.request_tx||!h.copy_tx||!h.consume_tx||!h.receive||!storage||!c.rx_slots||c.rx_slots>8||c.rx_bytes<60||size_t(c.rx_slots)*c.rx_bytes>bytes)return Status::InvalidArgument;
 config_=c;hooks_=h;storage_=storage;storage_bytes_=bytes;last_generation_=notified_=0;core_={};setup_={};configured_=true;return Status::Ok;
}
void OwnedVoiceAudio::end(){detach();configured_=false;hooks_={};storage_=nullptr;storage_bytes_=0;}
void OwnedVoiceAudio::detach(){if(notified_&&hooks_.ended)hooks_.ended(notified_,hooks_.user);notified_=0;last_generation_=core_.generation>last_generation_?core_.generation:last_generation_;core_={};submitted_read_=submitted_count_=0;fragment_bytes_=completed_bytes_=committed_unit_bytes_=0;was_connected_=false;pending_consume_=prepared_tx_={};consume_deadline_=0;}
void OwnedVoiceAudio::controllerInfo(uint16_t manufacturer,const uint8_t features[8],uint8_t tx_bytes,uint16_t packets){manufacturer_=manufacturer;std::memcpy(features_,features,8);tx_bytes_=tx_bytes;tx_packets_=packets;}
void OwnedVoiceAudio::startSetup(){bth_sco_setup_config_t c{};c.manufacturer=manufacturer_;c.tx_bytes=tx_bytes_;c.tx_packets=tx_packets_;c.allow_route_configuration=config_.allow_route_configuration;c.allow_rx_pacing=config_.allow_rx_pacing;bth_sco_setup_init(&setup_,&c);}
bool OwnedVoiceAudio::ready()const{return configured_&&setup_.done&&setup_.available&&(features_[2]&1)&&((features_[1]&8)||(features_[3]&0x80));}
bool OwnedVoiceAudio::wideband()const{return ready()&&config_.wideband&&GBR_ENABLE_SBC&&(features_[2]&8)&&(features_[3]&0x80);}
bth_sco_config_t OwnedVoiceAudio::policy(uint32_t owner,uint16_t acl,const uint8_t* peer)const{
 bth_sco_config_t c{};c.owner=owner;c.acl_handle=acl;std::memcpy(c.peer_hci_address,peer,6);c.codec=BTH_SCO_CVSD;c.route_confirmed=ready();c.controller_tx_flow=setup_.controller_flow;c.controller_tx_packets=tx_packets_;c.controller_tx_bytes=tx_bytes_;c.allow_rx_pacing=setup_.rx_paced;
 c.host_rx_flow=false; // Existing GAP setup remains ACL-only until separately admitted.
 c.rx_slots=config_.rx_slots;c.rx_bytes=config_.rx_bytes;c.max_latency=0xffff;c.voice_setting=0x60;c.packet_types=0x03c8;c.retransmission_effort=0xff;return c;
}
Status OwnedVoiceAudio::attach(uint32_t owner,uint16_t acl,const uint8_t peer[6]){
 if(!configured_||!owner||!peer)return Status::InvalidArgument;detach();const auto c=policy(owner,acl,peer);if(!bth_sco_init(&core_,&c,storage_,storage_bytes_))return Status::InvalidArgument;core_.generation=last_generation_;return Status::Ok;
}
void OwnedVoiceAudio::observe(){
 if(core_.generation>last_generation_)last_generation_=core_.generation;
 if(core_.state==BTH_SCO_CONNECTED&&!was_connected_){
  const unsigned factor=core_.config.codec==BTH_SCO_CVSD?2:1;unsigned fragment=unsigned(core_.air_tx_bytes)*factor;
  if(fragment>tx_bytes_)fragment=tx_bytes_;if(fragment>60)fragment=60;if(core_.config.codec==BTH_SCO_CVSD)fragment&=~1u;
  if(!fragment||((60u+fragment-1)/fragment)>8){bth_sco_request_close(&core_,core_.config.owner,core_.generation,0x13,now_);return;}
  fragment_bytes_=uint16_t(fragment);completed_bytes_=committed_unit_bytes_=0;submitted_read_=submitted_count_=0;prepared_tx_={};was_connected_=true;hooks_.request_tx(core_.generation,1,hooks_.user);
 }
 if(core_.state!=BTH_SCO_CONNECTED){was_connected_=false;prepared_tx_={};}
 if(core_.state==BTH_SCO_ATTACHED||core_.state==BTH_SCO_FAILED){
  pending_consume_={};consume_deadline_=0;submitted_read_=submitted_count_=0;
  if(notified_){hooks_.ended(notified_,hooks_.user);notified_=0;}
 }
}
bth_status_t OwnedVoiceAudio::event(const bth_h4_frame_t& e,bool secure,bool profile_ready,owned::VoiceCodec desired,uint32_t now,uint32_t command_owner){
 now_=now;if(!core_.initialized)return BTH_IGNORED;
 if((e.event.code==0x0e||e.event.code==0x0f)&&command_owner!=core_.config.owner)return BTH_IGNORED;
 const auto p=e.event.params;
 if(e.event.code==0x2d&&core_.active_handle){
  if(p.len!=9)return BTH_ERR_LENGTH;if(bth_read_le16(p.data+1)!=core_.handle)return BTH_IGNORED;
  // This first HCI voice backend does not renegotiate live packet geometry.
  // Retire the actual SCO handle instead of running the old byte cadence.
  return p.data[0]?BTH_IGNORED:bth_sco_request_close(&core_,core_.config.owner,core_.generation,0x13,now);
 }
 if(e.event.code==4&&p.len==10&&(p.data[9]==0||p.data[9]==2)&&core_.state==BTH_SCO_ATTACHED&&!std::memcmp(p.data,core_.config.peer_hci_address,6)){
  auto c=core_.config;c.codec=desired==owned::VoiceCodec::Msbc?BTH_SCO_MSBC:BTH_SCO_CVSD;c.voice_setting=c.codec==BTH_SCO_MSBC?0x43:0x60;c.route_confirmed=ready()&&(c.codec!=BTH_SCO_MSBC||wideband());
  if(p.data[9]==0){c.packet_types=(features_[1]&0x20)?0x03c5:0x03c1;if(!(features_[1]&8))c.route_confirmed=false;}
  else {c.packet_types=0x03c8;if(!(features_[3]&0x80))c.route_confirmed=false;}
  if(bth_sco_reconfigure(&core_,&c,storage_,storage_bytes_)!=BTH_OK)return BTH_ERR_STATE;
 }
 const auto result=bth_sco_on_event(&core_,&e,secure&&profile_ready,now);observe();return result;
}
void OwnedVoiceAudio::poll(bool authorized,uint32_t now){
 now_=now;if(!core_.initialized)return;bth_sco_expire(&core_,now);
 if(pending_consume_.kind){
  if(hooks_.consume_tx(pending_consume_.generation,pending_consume_.unit_ticket,pending_consume_.bytes,hooks_.user)){pending_consume_={};consume_deadline_=0;}
  else if(int32_t(now-consume_deadline_)>=0)bth_sco_request_close(&core_,core_.config.owner,core_.generation,0x13,now);
 }
 if(core_.state==BTH_SCO_CONNECTED&&hooks_.health){const auto health=hooks_.health(core_.generation,hooks_.user);if(health!=Status::Ok&&health!=Status::Busy)bth_sco_request_close(&core_,core_.config.owner,core_.generation,0x13,now);}
 if(!authorized&&(core_.state==BTH_SCO_REQUESTED||core_.state==BTH_SCO_CONNECTED))bth_sco_request_close(&core_,core_.config.owner,core_.generation,5,now);
 if(core_.state==BTH_SCO_REQUESTED&&!core_.reject&&!core_.ready){
  notified_=core_.generation;const auto status=hooks_.prepare(core_.generation,codec(),hooks_.user);if(status==Status::Ok)bth_sco_audio_ready(&core_,core_.config.owner,core_.generation,true);else if(status!=Status::Busy)bth_sco_audio_ready(&core_,core_.config.owner,core_.generation,false);
 }
 bth_sco_rx_view_t packet{};if(bth_sco_peek_rx(&core_,core_.config.owner,core_.generation,&packet)==BTH_OK&&hooks_.receive(core_.generation,packet.packet_status,packet.payload.data,packet.payload.len,hooks_.user))bth_sco_consume_rx(&core_,packet.ticket);
 observe();
}
bth_status_t OwnedVoiceAudio::receive(const bth_sco_view_t& p,bool secure){
 if(!core_.initialized)return BTH_IGNORED;if(!secure){bth_sco_request_close(&core_,core_.config.owner,core_.generation,5,now_);return BTH_IGNORED;}
 const auto result=bth_sco_receive(&core_,core_.config.owner,&p);
 if(result<0){bth_sco_request_close(&core_,core_.config.owner,core_.generation,0x13,now_);return result;}
 // RX contributes only bounded host-byte entitlement in the core. Worker
 // demand follows committed complete TX units, never rounded RX packet counts.
 return result;
}
bth_status_t OwnedVoiceAudio::validateCompleted(const bth_h4_frame_t& e)const{
 if(!core_.initialized||!core_.active_handle||e.type!=BTH_H4_EVENT||e.event.code!=0x13||!core_.config.controller_tx_flow)return BTH_IGNORED;
 const auto p=e.event.params;if(p.len<1||p.len!=1u+4u*p.data[0])return BTH_ERR_LENGTH;unsigned found=0,count=0;
 for(unsigned i=0;i<p.data[0];++i)if(bth_read_le16(p.data+1+4*i)==core_.handle){++found;count=bth_read_le16(p.data+3+4*i);}
 if(!found)return BTH_IGNORED;if(found!=1||count>submitted_count_||count>core_.tx_outstanding)return BTH_ERR_STATE;return BTH_OK;
}
bth_status_t OwnedVoiceAudio::completed(const bth_h4_frame_t& e){
 const auto valid=validateCompleted(e);if(valid!=BTH_OK)return valid;
 const auto p=e.event.params;unsigned count=0;for(unsigned i=0;i<p.data[0];++i)if(bth_read_le16(p.data+1+4*i)==core_.handle)count=bth_read_le16(p.data+3+4*i);
 const auto result=bth_sco_completed(&core_,core_.handle,uint16_t(count));if(result!=BTH_OK)return result;
 unsigned bytes=0;for(unsigned i=0;i<count;++i){bytes+=submitted_lengths_[submitted_read_];submitted_read_=uint8_t((submitted_read_+1)%8);--submitted_count_;}
 completed_bytes_=uint16_t(completed_bytes_+bytes);const unsigned units=completed_bytes_/60;completed_bytes_%=60;if(units&&active())hooks_.request_tx(core_.generation,units,hooks_.user);return BTH_OK;
}
bth_status_t OwnedVoiceAudio::prepareSubmission(uint8_t* out,size_t capacity,size_t& written,VoiceAudioSubmission& submission){
 written=0;submission={};if(!core_.initialized)return BTH_IGNORED;bth_sco_token_t ticket{};
 const auto completed=bth_sco_prepare_completed(&core_,out,capacity,&written,&ticket);if(completed==BTH_OK){submission.token=ticket;submission.generation=core_.generation;submission.kind=1;return BTH_OK;}if(completed<0)return completed;
 if(!active()||!fragment_bytes_||pending_consume_.kind||prepared_tx_.kind)return BTH_MORE;const unsigned lead=(60u+fragment_bytes_-1)/fragment_bytes_;if(core_.config.controller_tx_flow&&submitted_count_>=lead)return BTH_MORE;
 size_t available=0;uint32_t unit=0;if(!hooks_.copy_tx(core_.generation,copy_,sizeof copy_,available,unit,hooks_.user))return BTH_MORE;if(!available||available>60||!unit)return BTH_ERR_STATE;
 const size_t take=available<fragment_bytes_?available:fragment_bytes_;const auto result=bth_sco_prepare_tx(&core_,core_.config.owner,core_.generation,copy_,take,out,capacity,&written,&ticket);
 if(result==BTH_OK){submission.token=ticket;submission.generation=core_.generation;submission.unit_ticket=unit;submission.bytes=uint16_t(take);submission.kind=2;prepared_tx_=submission;}return result;
}
bth_status_t OwnedVoiceAudio::commitSubmission(const VoiceAudioSubmission& submission){
 if(submission.generation!=core_.generation)return BTH_IGNORED;if(submission.kind==1)return bth_sco_commit_completed(&core_,submission.token);if(submission.kind!=2)return BTH_ERR_PARAM;
 if(!prepared_tx_.kind||submission.token.generation!=prepared_tx_.token.generation||submission.token.serial!=prepared_tx_.token.serial||submission.token.count!=prepared_tx_.token.count)return BTH_IGNORED;
 if(submission.bytes!=prepared_tx_.bytes||submission.bytes!=submission.token.count||submission.unit_ticket!=prepared_tx_.unit_ticket)return BTH_ERR_PARAM;
 const auto status=bth_sco_commit_tx(&core_,submission.token);if(status!=BTH_OK)return status;
 prepared_tx_={};
 if(core_.config.controller_tx_flow){if(submitted_count_==8)return BTH_ERR_STATE;submitted_lengths_[(submitted_read_+submitted_count_)%8]=submission.bytes;++submitted_count_;}
 // UART copy is already admitted. Worker queue retirement can briefly contend
 // with publication; retain its exact receipt and retry without resending H4.
 if(!hooks_.consume_tx(core_.generation,submission.unit_ticket,submission.bytes,hooks_.user)){pending_consume_=submission;consume_deadline_=now_+5000;}
 if(!core_.config.controller_tx_flow){
  const unsigned progress=unsigned(committed_unit_bytes_)+submission.bytes;
  committed_unit_bytes_=uint16_t(progress%BTH_SCO_HOST_UNIT_BYTES);
  const unsigned units=progress/BTH_SCO_HOST_UNIT_BYTES;if(units&&active())hooks_.request_tx(core_.generation,units,hooks_.user);
 }
 return BTH_OK;
}
}}
