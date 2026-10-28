#pragma once
#include "../Types.h"
#include "HfpControl.h"
#include "HspControl.h"
extern "C" {
#include "core/bth_rfcomm.h"
#include "core/bth_control_tx.h"
}
namespace giga_bredr { namespace detail {
// Experimental control owner, not an audio-backend capability or SDP grant.
// Its caller reserves this object and retains it through OwnedHost::end().
struct VoiceControlConfig {
 bool hands_free=false,headset=false;
 uint8_t hands_free_channel=1,headset_channel=2,speaker_gain=15,microphone_gain=15;
};
struct VoiceControlStats {
 uint32_t received_frames=0,consumed_frames=0,transmitted_frames=0,backpressure=0;
};
class VoiceProfiles {
public:
 static constexpr unsigned InformationBytes=127,FrameBytes=136,ReceiveWindow=2,TransmitSlots=2;
 Status begin(const VoiceControlConfig&,bth_control_tx_t&,uint8_t peer_index=0);
 void reset(uint32_t peer_owner);
 void end();
 bool enabled()const{return active_;}
 bool channelOpened(uint32_t peer_owner,uint16_t local,uint16_t remote,uint16_t remote_mtu);
 bool channelMtuUpdated(uint32_t peer_owner,uint16_t local,uint16_t remote,uint16_t remote_mtu);
 void channelClosed(uint16_t local);
 bool receive(uint16_t local,const uint8_t*,size_t,bool secure,uint32_t now_ms);
 void poll(bool secure,uint32_t now_ms,bool allow_transmit=true);
 uint16_t cid()const{return local_;}uint16_t remoteCid()const{return remote_;}
 uint16_t retiringLocal()const{return owner_&&retired_owner_==owner_?retired_local_:0;}
 uint16_t retiringRemote()const{return owner_&&retired_owner_==owner_?retired_remote_:0;}
 // Actor must also fence its ordered adapter mailbox before this call. The
 // scoped queue is rechecked here; the tombstone survives parser retries.
 bool authorizeRetirement(uint32_t peer_owner,uint16_t local,uint16_t remote);
 bool closeRequested()const{return close_;}
 bool pendingOutput()const{return transmit_count_!=0;}
 VoiceControlStats stats()const{return stats_;}
 owned::HfpStatus hfpStatus()const{return hf_.status();}
 owned::HspStatus hspStatus()const{return hs_.status();}
 // Only before an RFCOMM mux exists; never change advertised BRSF/BAC midway.
 bool setWidebandBackend(bool ready){if(local_)return false;wideband_=ready;return true;}
 bth_status_t commandHfp(uint32_t expected,owned::HfpCommand command,const char* argument=nullptr){return close_?BTH_IGNORED:hf_.command(expected,command,argument);}
private:
 struct Rx {uint8_t bytes[InformationBytes];uint16_t length=0;};
 struct Dlc {Rx receive[ReceiveWindow];uint8_t dlci=0,read=0,count=0;uint32_t generation=0;char row[48]{};uint8_t row_size=0,row_cursor=0;};
 struct Tx {uint8_t bytes[FrameBytes];uint16_t length=0;};
 bool ownerValid()const;
 Dlc* dlc(uint8_t);const bth_rfcomm_dlc_t* coreDlc(uint8_t)const;
 void clearChannel();void requestClose();void pump();
 bool enqueue(size_t);uint8_t* output();
 bool consume(Dlc&,uint32_t);void generate(Dlc&,uint32_t);
 VoiceControlConfig config_{};VoiceControlStats stats_{};bth_control_tx_t* replies_=nullptr;
 bth_rfcomm_t mux_{};owned::HfpControl hf_{};owned::HspControl hs_{};Dlc dlcs_[2];Tx transmit_[TransmitSlots];
 uint32_t owner_=0,channel_generation_=0,profile_serial_=0;uint16_t local_=0,remote_=0,mtu_=0;
 uint32_t retired_owner_=0;uint16_t retired_local_=0,retired_remote_=0;bool retirement_authorized_=false;
 uint8_t peer_=0,transmit_read_=0,transmit_count_=0,next_dlc_=0;bool active_=false,close_=false,wideband_=false;
};
}}
