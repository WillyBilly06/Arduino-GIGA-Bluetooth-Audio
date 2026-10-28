#include "VoiceProfiles.h"
#include <cstring>
namespace giga_bredr { namespace detail {
Status VoiceProfiles::begin(const VoiceControlConfig& c,bth_control_tx_t& q,uint8_t peer){
 if(active_)return Status::Busy;
 if((!c.hands_free&&!c.headset)||!q.initialized||peer>=BTH_CONTROL_PEERS||c.speaker_gain>15||c.microphone_gain>15||
    (c.hands_free&&(!c.hands_free_channel||c.hands_free_channel>30))||(c.headset&&(!c.headset_channel||c.headset_channel>30))||
    (c.hands_free&&c.headset&&c.hands_free_channel==c.headset_channel))return Status::InvalidArgument;
 config_=c;replies_=&q;peer_=peer;owner_=channel_generation_=profile_serial_=retired_owner_=0;retired_local_=retired_remote_=0;retirement_authorized_=false;wideband_=false;stats_={};clearChannel();active_=true;return Status::Ok;
}
void VoiceProfiles::clearChannel(){
 bth_rfcomm_close(&mux_);hf_.end();hs_.end();for(auto& d:dlcs_)d=Dlc();
 local_=remote_=mtu_=0;transmit_read_=transmit_count_=next_dlc_=0;close_=false;
 for(auto& t:transmit_)t.length=0;
}
void VoiceProfiles::reset(uint32_t owner){clearChannel();retired_owner_=0;retired_local_=retired_remote_=0;retirement_authorized_=false;owner_=active_?owner:0;}
void VoiceProfiles::end(){reset(0);replies_=nullptr;active_=false;}
bool VoiceProfiles::ownerValid()const{return active_&&owner_&&replies_&&replies_->initialized&&replies_->peers[peer_].attached&&replies_->peers[peer_].owner==owner_;}
bool VoiceProfiles::channelOpened(uint32_t owner,uint16_t local,uint16_t remote,uint16_t mtu){
 if(!ownerValid()||owner!=owner_||local_||local<0x40||remote<0x40||mtu<FrameBytes||channel_generation_==UINT32_MAX||(retiringLocal()&&!retirement_authorized_))return false;
 const uint32_t generation=channel_generation_+1;
 if(!bth_rfcomm_init(&mux_,generation,false,config_.hands_free?config_.hands_free_channel:config_.headset_channel,InformationBytes,ReceiveWindow))return false;
 if(config_.hands_free&&config_.headset&&bth_rfcomm_add_listener(&mux_,generation,config_.headset_channel)!=BTH_OK){bth_rfcomm_close(&mux_);return false;}
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
void VoiceProfiles::requestClose(){close_=true;}
VoiceProfiles::Dlc* VoiceProfiles::dlc(uint8_t id){for(auto& d:dlcs_)if(id&&d.dlci==id)return &d;return nullptr;}
const bth_rfcomm_dlc_t* VoiceProfiles::coreDlc(uint8_t id)const{for(const auto& d:mux_.dlc)if(d.state&&d.dlci==id)return &d;return nullptr;}
uint8_t* VoiceProfiles::output(){return transmit_count_<TransmitSlots?transmit_[(transmit_read_+transmit_count_)%TransmitSlots].bytes:nullptr;}
bool VoiceProfiles::enqueue(size_t n){if(!n)return true;if(!output()||n>FrameBytes){requestClose();return false;}transmit_[(transmit_read_+transmit_count_)%TransmitSlots].length=uint16_t(n);++transmit_count_;return true;}
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
  const uint8_t scn=event.dlci>>1;unsigned index=scn==config_.hands_free_channel&&config_.hands_free?0:1;
  if((index==0&&!config_.hands_free)||(index==1&&(!config_.headset||scn!=config_.headset_channel))){requestClose();return true;}
  auto& d=dlcs_[index];d=Dlc();d.dlci=event.dlci;d.generation=++profile_serial_;
  const bool ok=index==0?hf_.begin(d.generation,wideband_,config_.speaker_gain,config_.microphone_gain):hs_.begin(d.generation,config_.speaker_gain,config_.microphone_gain);
  if(!ok)requestClose();
 }else if(event.type==BTH_RFCOMM_DLC_CLOSED){
  for(unsigned i=0;i<2;++i)if(dlcs_[i].dlci==event.dlci){dlcs_[i]=Dlc();if(i==0)hf_.end();else hs_.end();}
 }else if(event.type==BTH_RFCOMM_MUX_CLOSED){hf_.end();hs_.end();requestClose();}
 else if(event.type==BTH_RFCOMM_DATA){
  auto* d=dlc(event.dlci);if(!d||d->count>=ReceiveWindow||event.payload.len>InformationBytes){requestClose();return true;}
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
 bth_control_reservation_t slot{};if(bth_control_tx_reserve(replies_,peer_,&slot)!=BTH_OK)return;
 const auto& item=transmit_[transmit_read_];std::memcpy(slot.data,item.bytes,item.length);
 if(bth_control_tx_commit(replies_,slot.token,remote_,item.length,mtu_)!=BTH_OK){bth_control_tx_cancel(replies_,slot.token);requestClose();return;}
 transmit_[transmit_read_].length=0;transmit_read_=(transmit_read_+1)%TransmitSlots;--transmit_count_;++stats_.transmitted_frames;
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
  if(hf_.status().state==owned::HfpState::Failed||hs_.status().failed)requestClose();
 }
 // Already committed copied output can unblock retained input. Optional AT
 // work may wait behind that input; the durable FIFO must still make progress.
 pump();if(!allow_transmit||close_||!output())return;
 size_t n=0;auto result=bth_rfcomm_next(&mux_,channel_generation_,now,output(),FrameBytes,&n);
 if(result<0){requestClose();return;}if(n){enqueue(n);return;}
 for(unsigned k=0;k<2;++k){const unsigned i=(next_dlc_+k)%2;const uint8_t count=transmit_count_;generate(dlcs_[i],now);if(transmit_count_!=count){next_dlc_=uint8_t(i^1);break;}}
}
}}
