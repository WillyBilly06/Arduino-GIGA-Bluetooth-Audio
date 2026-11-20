#include "VoiceProfiles.h"
#include <cstring>
namespace giga_bredr { namespace detail {
Status VoiceProfiles::begin(const VoiceControlConfig& c,bth_control_tx_t& q,uint8_t peer){
 if(active_)return Status::Busy;
 const uint8_t serial=c.serial?c.serial->channel():0;
 const uint8_t opp=c.obex&&c.obex->oppEnabled()?c.obex->oppChannel():0;
 if((!c.hands_free&&!c.headset&&!c.serial&&!c.obex)||(c.obex&&(!c.obex->enabled()||(opp&&(opp>30||(c.hands_free&&opp==c.hands_free_channel)||(c.headset&&opp==c.headset_channel)||(serial&&opp==serial)))))||!q.initialized||peer>=BTH_CONTROL_PEERS||c.speaker_gain>15||c.microphone_gain>15||
    (c.hands_free&&(!c.hands_free_channel||c.hands_free_channel>30))||(c.headset&&(!c.headset_channel||c.headset_channel>30))||
    (c.hands_free&&c.headset&&c.hands_free_channel==c.headset_channel)||
    (c.serial&&(!c.serial->enabled()||!serial||serial>30||c.serial->creditWindow(InformationBytes)<2||(c.hands_free&&serial==c.hands_free_channel)||(c.headset&&serial==c.headset_channel))))return Status::InvalidArgument;
 config_=c;serial_=c.serial;obex_=c.obex;started_=false;bulk_interval_=0;replies_=&q;peer_=peer;owner_=channel_generation_=profile_serial_=retired_owner_=0;retired_local_=retired_remote_=0;retirement_authorized_=false;wideband_=false;stats_={};clearChannel();active_=true;return Status::Ok;
}
void VoiceProfiles::clearChannel(){
 bth_rfcomm_close(&mux_);hf_.end();hs_.end();if(serial_)serial_->close();if(obex_)obex_->muxClosed();started_=false;for(auto& d:dlcs_)d=Dlc();
 local_=remote_=mtu_=0;transmit_read_=transmit_count_=next_dlc_=0;close_=false;bulk_at_=0;bulk_sent_=false;
 for(auto& t:transmit_){t.length=0;t.bulk=false;}
}
void VoiceProfiles::reset(uint32_t owner){clearChannel();retired_owner_=0;retired_local_=retired_remote_=0;retirement_authorized_=false;owner_=active_?owner:0;if(obex_)obex_->reset(owner_);}
void VoiceProfiles::end(){reset(0);replies_=nullptr;serial_=nullptr;config_.serial=nullptr;obex_=nullptr;config_.obex=nullptr;active_=false;}
void VoiceProfiles::dropVoice(){if((!serial_&&!obex_)||local_){end();return;}hf_.end();hs_.end();config_.hands_free=config_.headset=false;}
bth_status_t VoiceProfiles::commandHsp(uint32_t expected,owned::HspCommand command){
 return !config_.headset||!ownerValid()||!local_||close_?BTH_IGNORED:hs_.command(expected,command);
}
bool VoiceProfiles::ownerValid()const{return active_&&owner_&&replies_&&replies_->initialized&&replies_->peers[peer_].attached&&replies_->peers[peer_].owner==owner_;}
bool VoiceProfiles::channelOpened(uint32_t owner,uint16_t local,uint16_t remote,uint16_t mtu,bool initiator){
 if(!ownerValid()||owner!=owner_||local_||local<0x40||remote<0x40||mtu<FrameBytes||channel_generation_==UINT32_MAX||(retiringLocal()&&!retirement_authorized_))return false;
 const uint32_t generation=channel_generation_+1;
 uint8_t channels[4];unsigned count=0;
 if(config_.hands_free)channels[count++]=config_.hands_free_channel;if(config_.headset)channels[count++]=config_.headset_channel;if(serial_)channels[count++]=serial_->channel();if(obex_&&obex_->oppEnabled())channels[count++]=obex_->oppChannel();
 // A PBAP-only owner listens on no server channel (0).
 if((!count&&!obex_)||!bth_rfcomm_init(&mux_,generation,initiator,count?channels[0]:0,InformationBytes,ReceiveWindow))return false;
 for(unsigned i=1;i<count;++i)if(bth_rfcomm_add_listener(&mux_,generation,channels[i])!=BTH_OK){bth_rfcomm_close(&mux_);return false;}
 // The SPP ring reserves window*N1 bytes; voice DLCs keep their two frames.
 if(serial_&&bth_rfcomm_set_listener_window(&mux_,generation,serial_->channel(),serial_->creditWindow(InformationBytes))!=BTH_OK){bth_rfcomm_close(&mux_);return false;}
 if(obex_&&obex_->oppEnabled()&&bth_rfcomm_set_listener_window(&mux_,generation,obex_->oppChannel(),ObexPort::Window)!=BTH_OK){bth_rfcomm_close(&mux_);return false;}
 started_=false;
 channel_generation_=generation;local_=local;remote_=remote;mtu_=mtu;retired_owner_=0;retired_local_=retired_remote_=0;retirement_authorized_=false;return true;
}
bool VoiceProfiles::channelMtuUpdated(uint32_t owner,uint16_t local,uint16_t remote,uint16_t mtu){
 if(!ownerValid()||owner!=owner_||local!=local_||remote!=remote_||mtu<FrameBytes)return false;mtu_=mtu;return true;
}
void VoiceProfiles::channelClosed(uint16_t cid){if(cid&&cid==local_){retired_owner_=owner_;retired_local_=local_;retired_remote_=remote_;retirement_authorized_=false;clearChannel();}}
bool VoiceProfiles::authorizeRetirement(uint32_t owner,uint16_t local,uint16_t remote){
 if(!ownerValid()||owner!=owner_||local_||retired_owner_!=owner||!local||!remote||retired_local_!=local||retired_remote_!=remote)return false;
 bool pending=true;if(bth_control_tx_channel_pending(replies_,peer_,owner,remote,&pending)!=BTH_OK||pending)return false;
 retirement_authorized_=true;return true;
}
void VoiceProfiles::requestClose(){close_=true;if(serial_)serial_->close();if(obex_)obex_->muxClosed();} // No SPP admission on a retiring mux.
VoiceProfiles::Dlc* VoiceProfiles::dlc(uint8_t id){for(auto& d:dlcs_)if(id&&d.dlci==id)return &d;return nullptr;}
const bth_rfcomm_dlc_t* VoiceProfiles::coreDlc(uint8_t id)const{for(const auto& d:mux_.dlc)if(d.state&&d.dlci==id)return &d;return nullptr;}
uint8_t* VoiceProfiles::output(){return transmit_count_<TransmitSlots?transmit_[(transmit_read_+transmit_count_)%TransmitSlots].bytes:nullptr;}
bool VoiceProfiles::enqueue(size_t n,bool bulk){if(!n)return true;if(!output()||n>FrameBytes){requestClose();return false;}auto& slot=transmit_[(transmit_read_+transmit_count_)%TransmitSlots];slot.length=uint16_t(n);slot.bulk=bulk;++transmit_count_;return true;}
bool VoiceProfiles::receive(uint16_t cid,const uint8_t* p,size_t n,bool secure,uint32_t now){
 if(!ownerValid()||cid!=local_||!local_)return true;
 if(!secure||close_){requestClose();return true;}
 if(!p||n>FrameBytes){requestClose();return true;}
 bth_rfcomm_frame_t frame{};if(bth_rfcomm_decode(p,n,mux_.mode==2,&frame)!=BTH_OK){requestClose();return true;}
 auto* entry=dlc(frame.dlci);
 if(frame.control==BTH_RFCOMM_UIH&&frame.dlci&&frame.payload.len&&entry&&entry->count==ReceiveWindow){++stats_.backpressure;return false;}
 uint8_t* target=output();if(!target){++stats_.backpressure;return false;}
 size_t written=0;bth_rfcomm_event_t event{};auto result=bth_rfcomm_receive(&mux_,channel_generation_,now,p,n,target,FrameBytes,&written,&event);
 if(result==BTH_ERR_CAPACITY){requestClose();return true;} // Larger response cannot fit the admitted L2CAP service.
 if(result<0||!mux_.initialized){requestClose();return true;}
 if(!enqueue(written))return true;
 if(event.type==BTH_RFCOMM_DLC_OPENED){
  if(profile_serial_==UINT32_MAX){requestClose();return true;}
  if(obex_&&obex_->claims(event.dlci,mux_.initiator)){const auto* core=coreDlc(event.dlci);obex_->opened(event.dlci,core&&core->mtu<=InformationBytes?core->mtu:0,core&&core->credit,++profile_serial_);return true;}
  // Only incoming DLCs map to listeners; an outgoing DLC belongs to OBEX alone.
  if((event.dlci&1)!=(mux_.initiator?1:0)){requestClose();return true;}
  const uint8_t scn=event.dlci>>1;
  const unsigned index=config_.hands_free&&scn==config_.hands_free_channel?0:config_.headset&&scn==config_.headset_channel?1:serial_&&scn==serial_->channel()?SerialIndex:3;
  if(index==3){requestClose();return true;}
  auto& d=dlcs_[index];d=Dlc();d.dlci=event.dlci;d.generation=++profile_serial_;
  bool ok=true;
  if(index==SerialIndex){const auto* core=coreDlc(event.dlci);ok=core&&core->mtu&&core->mtu<=InformationBytes;if(ok)serial_->open(d.generation,core->mtu,core->credit);}
  else ok=index==0?hf_.begin(d.generation,wideband_,config_.speaker_gain,config_.microphone_gain):hs_.begin(d.generation,config_.speaker_gain,config_.microphone_gain);
  if(!ok)requestClose();
 }else if(event.type==BTH_RFCOMM_DLC_CLOSED){
  if(obex_)obex_->closed(event.dlci);
  for(unsigned i=0;i<3;++i)if(dlcs_[i].dlci&&dlcs_[i].dlci==event.dlci){dlcs_[i]=Dlc();if(i==0)hf_.end();else if(i==1)hs_.end();else if(serial_)serial_->close();}
 }else if(event.type==BTH_RFCOMM_MUX_CLOSED){hf_.end();hs_.end();if(serial_)serial_->close();requestClose();}
 else if(event.type==BTH_RFCOMM_DATA){
  if(obex_&&obex_->owns(event.dlci)){
   // The OBEX packet assembler holds everything the peer may send before our
   // next response, so the credit returns at once (no retained L2CAP input).
   obex_->receive(event.dlci,event.payload.data,event.payload.len);
   const auto* core=coreDlc(event.dlci);if(core&&core->credit&&bth_rfcomm_release(&mux_,channel_generation_,event.dlci,1)!=BTH_OK)requestClose();
   return true;
  }
  auto* d=dlc(event.dlci);
  // Credits reserve ring space; a legacy peer that ignores FC=1 loses only
  // its own overflow (counted), never retained L2CAP input that fences media.
  if(d&&d==&dlcs_[SerialIndex]){if(event.payload.len>InformationBytes){requestClose();return true;}serial_->rxPush(event.payload.data,event.payload.len);return true;}
  if(!d||d->count>=ReceiveWindow||event.payload.len>InformationBytes){requestClose();return true;}
  auto& input=d->receive[(d->read+d->count)%ReceiveWindow];std::memcpy(input.bytes,event.payload.data,event.payload.len);input.length=uint16_t(event.payload.len);++d->count;++stats_.received_frames;
 }
 return true;
}
bool VoiceProfiles::consume(Dlc& d,uint32_t now){
 if(!d.count)return true;auto& input=d.receive[d.read];
 auto result=&d==&dlcs_[0]?hf_.receive(d.generation,input.bytes,input.length,now):hs_.receive(d.generation,input.bytes,input.length,now);
 if(result<0){requestClose();return false;}
 const auto* core=coreDlc(d.dlci);if(core&&core->credit&&bth_rfcomm_release(&mux_,channel_generation_,d.dlci,1)!=BTH_OK){requestClose();return false;}
 input.length=0;d.read=(d.read+1)%ReceiveWindow;--d.count;++stats_.consumed_frames;return true;
}
void VoiceProfiles::pump(){
 if(!transmit_count_||!ownerValid()||!local_)return;
 if(transmit_[transmit_read_].bulk&&bth_control_tx_available(replies_,peer_)<BulkReserve){
  // Credits, MSC, replies and voice rows may pass the waiting data frame. At
  // most one SPP data frame is ever queued, so per-DLC data order is unchanged.
  if(transmit_count_<2)return;
  const unsigned next=(transmit_read_+1)%TransmitSlots;const Tx waiting=transmit_[transmit_read_];
  transmit_[transmit_read_]=transmit_[next];transmit_[next]=waiting;
 }
 const auto& item=transmit_[transmit_read_];
 bth_control_reservation_t slot{};if(bth_control_tx_reserve(replies_,peer_,&slot)!=BTH_OK)return;
 std::memcpy(slot.data,item.bytes,item.length);
 if(bth_control_tx_commit(replies_,slot.token,remote_,item.length,mtu_)!=BTH_OK){bth_control_tx_cancel(replies_,slot.token);requestClose();return;}
 const bool bulk=item.bulk;transmit_[transmit_read_].length=0;transmit_[transmit_read_].bulk=false;transmit_read_=(transmit_read_+1)%TransmitSlots;--transmit_count_;if(!bulk)++stats_.transmitted_frames;
}
void VoiceProfiles::serviceSerial(){
 const auto& d=dlcs_[SerialIndex];if(!serial_||!d.dlci||!bth_rfcomm_ready(&mux_,channel_generation_,d.dlci))return;
 const auto* core=coreDlc(d.dlci);if(!core)return;serial_->observeCredits(core->tx_credits);if(!core->credit||!core->mtu)return;
 // Grant only credits whose N1-byte frames already fit in free ring space.
 // Batch returns until at most half the window is still outstanding.
 const unsigned outstanding=unsigned(core->rx_credits)+core->grant_pending,window=core->window;
 const size_t room=serial_->rxFree()/core->mtu;const unsigned limit=unsigned(room<window?room:window);
 if(limit<=outstanding||outstanding>window/2)return;
 if(bth_rfcomm_release(&mux_,channel_generation_,d.dlci,uint8_t(limit-outstanding))!=BTH_OK)requestClose();
}
void VoiceProfiles::generateSerial(uint32_t now){
 const auto& d=dlcs_[SerialIndex];
 if(!serial_||!d.dlci||!bth_rfcomm_ready(&mux_,channel_generation_,d.dlci)||!output())return;
 const auto* core=coreDlc(d.dlci);if(!core||!core->mtu)return;
 if(!core->credit){
  // RFCOMM1.0 peer: MSC FC with hysteresis instead of credits.
  const bool off=core->local_flow_off?serial_->rxFree()<4u*core->mtu:serial_->rxFree()<2u*core->mtu;
  if(off!=core->local_flow_off){size_t n=0;const auto result=bth_rfcomm_set_flow(&mux_,channel_generation_,d.dlci,off,output(),FrameBytes,&n);if(result==BTH_OK)enqueue(n);else if(result<0)requestClose();return;}
 }
 // One data frame per host turn, only behind no other RFCOMM output.
 if(transmit_count_||!serial_->txPending())return;
 if((core->credit&&!core->tx_credits)||(!core->credit&&(mux_.peer_flow_off||core->peer_flow_off)))return;
 if(bulk_interval_&&bulk_sent_&&uint32_t(now-bulk_at_)<bulk_interval_)return;
 uint8_t chunk[InformationBytes];const size_t take=serial_->txPeek(chunk,core->mtu<InformationBytes?core->mtu:InformationBytes);size_t n=0;
 const auto result=bth_rfcomm_send(&mux_,channel_generation_,d.dlci,chunk,take,output(),FrameBytes,&n);
 if(result==BTH_OK){if(enqueue(n,true)){serial_->txConsume(take);bulk_at_=now;bulk_sent_=true;}}
 else if(result<0)requestClose();
}
bool VoiceProfiles::dlcActive()const{
 for(const auto& d:dlcs_)if(d.dlci)return true;
 return obex_&&(obex_->closeDlci(ObexPort::Opp)||obex_->closeDlci(ObexPort::Pbap));
}
// At most one mux-level OBEX frame per host turn: start a mux this side
// initiated, close a finished/failed OBEX DLC, open the PBAP client DLC, or
// retire an initiated mux nothing uses. True when it queued output or closed.
bool VoiceProfiles::obexControl(uint32_t now){
 size_t n=0;
 if(mux_.initiator&&mux_.mux==BTH_RFCOMM_CLOSED){
  if(started_)return false;
  const auto result=bth_rfcomm_start(&mux_,channel_generation_,now,output(),FrameBytes,&n);
  if(result==BTH_OK){started_=true;enqueue(n);return true;}
  if(result<0){obex_->transportRefused();requestClose();return true;}
  return false;
 }
 if(mux_.mux!=BTH_RFCOMM_OPEN)return false;
 for(unsigned l=0;l<ObexPort::LinkCount;++l){
  if(!obex_->wantsClose(l))continue;
  const uint8_t d=obex_->closeDlci(l);if(!coreDlc(d)){obex_->closed(d);continue;} // Refused or already gone.
  const auto result=bth_rfcomm_disconnect(&mux_,channel_generation_,d,now,output(),FrameBytes,&n);
  if(result==BTH_OK){obex_->closeSent(l);enqueue(n);return true;}
  if(result==BTH_ERR_STATE)continue; // Still negotiating: closed once it opens.
  if(result<0){requestClose();return true;}
 }
 uint8_t scn=0;
 if(obex_->wantsDlc(scn)){
  const uint8_t d=uint8_t(scn*2+(mux_.initiator?0:1));
  if(coreDlc(d))return false; // A previous DLC on that channel is still retiring.
  const auto result=bth_rfcomm_open_window(&mux_,channel_generation_,scn,true,ObexPort::Window,now,output(),FrameBytes,&n);
  if(result==BTH_OK){obex_->dlcRequested(d);enqueue(n);return true;}
  if(result==BTH_MORE||result==BTH_IGNORED)return false; // Another DLC is still fixing the mux mode.
  obex_->transportRefused();return false;
 }
 if(mux_.initiator&&obex_->transportIdle()&&!dlcActive()){requestClose();return true;}
 return false;
}
void VoiceProfiles::generateObex(unsigned l,uint32_t now){
 const uint8_t d=obex_?obex_->dlci(l):0;
 if(!d||transmit_count_||!obex_->txPending(l)||!output()||!bth_rfcomm_ready(&mux_,channel_generation_,d))return;
 const auto* core=coreDlc(d);if(!core||!core->mtu)return;
 if((core->credit&&!core->tx_credits)||(!core->credit&&(mux_.peer_flow_off||core->peer_flow_off)))return;
 if(bulk_interval_&&bulk_sent_&&uint32_t(now-bulk_at_)<bulk_interval_)return;
 uint8_t chunk[InformationBytes];const size_t take=obex_->txPeek(l,chunk,core->mtu<InformationBytes?core->mtu:InformationBytes);size_t n=0;
 const auto result=bth_rfcomm_send(&mux_,channel_generation_,d,chunk,take,output(),FrameBytes,&n);
 if(result==BTH_OK){if(enqueue(n,true)){obex_->txConsume(l,take);bulk_at_=now;bulk_sent_=true;}}
 else if(result<0)requestClose();
}
void VoiceProfiles::generate(Dlc& d,uint32_t now){
 if(!d.dlci||!bth_rfcomm_ready(&mux_,channel_generation_,d.dlci)||!output())return;
 const auto* core=coreDlc(d.dlci);if(!core||(core->credit&&!core->tx_credits)||(!core->credit&&(mux_.peer_flow_off||core->peer_flow_off)))return;
 if(!d.row_size){size_t n=0;auto result=&d==&dlcs_[0]?hf_.next(d.generation,now,d.row,sizeof d.row,n):hs_.next(d.generation,now,d.row,sizeof d.row,n);
  if(result<0){requestClose();return;}if(result!=BTH_OK||!n)return;d.row_size=uint8_t(n);d.row_cursor=0;
 }
 const size_t remain=d.row_size-d.row_cursor;const size_t take=remain<core->mtu?remain:core->mtu;size_t n=0;
 const auto result=bth_rfcomm_send(&mux_,channel_generation_,d.dlci,reinterpret_cast<const uint8_t*>(d.row+d.row_cursor),take,output(),FrameBytes,&n);
 if(result==BTH_OK&&enqueue(n)){d.row_cursor=uint8_t(d.row_cursor+take);if(d.row_cursor==d.row_size)d.row_cursor=d.row_size=0;}
 else if(result<0)requestClose();
}
void VoiceProfiles::poll(bool secure,uint32_t now,bool allow_transmit){
 if(!ownerValid()||!local_)return;if(!secure){requestClose();return;}
 if(!close_){
  if(bth_rfcomm_expire(&mux_,channel_generation_,now)==BTH_OK)requestClose();
  if(hf_.status().generation&&hf_.expire(hf_.status().generation,now)==BTH_OK)requestClose();
  if(hs_.status().generation&&hs_.expire(hs_.status().generation,now)==BTH_OK)requestClose();
  for(auto& d:dlcs_)if(!close_&&d.count)consume(d,now);
  if(!close_)serviceSerial();
  if(!close_&&obex_)obex_->service(now);
  if(hf_.status().state==owned::HfpState::Failed||hs_.status().failed)requestClose();
 }
 // Already committed copied output can unblock retained input. Optional AT
 // work may wait behind that input; the durable FIFO must still make progress.
 pump();if(!allow_transmit||close_||!output())return;
 size_t n=0;auto result=bth_rfcomm_next(&mux_,channel_generation_,now,output(),FrameBytes,&n);
 if(result<0){requestClose();return;}if(n){enqueue(n);return;}
 if(obex_&&obexControl(now))return;
 for(unsigned k=0;k<Slots;++k){const unsigned i=(next_dlc_+k)%Slots;const uint8_t count=transmit_count_;if(i==SerialIndex)generateSerial(now);else if(i>SerialIndex)generateObex(i-SerialIndex-1,now);else generate(dlcs_[i],now);if(transmit_count_!=count){next_dlc_=uint8_t((i+1)%Slots);break;}}
}
}}
