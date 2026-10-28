#pragma once
#include "HfpAudio.h"
#include "../Types.h"
extern "C" {
#include "core/bth_sco.h"
#include "core/bth_sco_setup.h"
}
namespace giga_bredr { namespace detail {
struct VoiceAudioHostConfig {bool allow_route_configuration=false,allow_rx_pacing=false,wideband=true;uint8_t rx_slots=4,rx_bytes=64;};
// Every callback is a bounded mailbox/queue operation. prepare may return Busy
// until the separate audio worker acknowledges real output preparation. No
// codec, user microphone callback, allocator, I2S operation or blocking wait is
// permitted here. copy_tx copies the current <=60-byte unit remainder.
struct VoiceAudioHostHooks {
 Status (*prepare)(uint32_t,owned::VoiceCodec,void*)=nullptr;
 void (*ended)(uint32_t,void*)=nullptr;
 void (*request_tx)(uint32_t,unsigned,void*)=nullptr;
 bool (*copy_tx)(uint32_t,uint8_t*,size_t,size_t&,uint32_t&,void*)=nullptr;
 bool (*consume_tx)(uint32_t,uint32_t,size_t,void*)=nullptr;
 bool (*receive)(uint32_t,uint8_t,const uint8_t*,size_t,void*)=nullptr;
 Status (*health)(uint32_t,void*)=nullptr;
 void (*gains)(uint8_t speaker,uint8_t microphone,void*)=nullptr;
 void* user=nullptr;
};
struct VoiceAudioSubmission {bth_sco_token_t token{};uint32_t generation=0,unit_ticket=0;uint16_t bytes=0;uint8_t kind=0;};
class OwnedVoiceAudio {
public:
 Status begin(const VoiceAudioHostConfig&,const VoiceAudioHostHooks&,uint8_t* rx_storage,size_t);
 void end();void detach();
 void controllerInfo(uint16_t manufacturer,const uint8_t features[8],uint8_t tx_bytes,uint16_t tx_packets);
 void startSetup();bool setupDone()const{return setup_.done;}
 bth_status_t setupNext(uint32_t now,bth_sco_setup_command_t& c){return bth_sco_setup_next(&setup_,now,&c);}
 bth_status_t setupQueued(bth_sco_setup_ticket_t t,uint32_t now){return bth_sco_setup_queued(&setup_,t,now);}
 bth_status_t setupEvent(const bth_h4_frame_t& e,uint32_t now){return bth_sco_setup_on_event(&setup_,&e,now);}
 bool setupRequiresReset()const{return setup_.requires_reset;}
 uint8_t setupReason()const{return uint8_t(setup_.reason);}
 uint8_t controllerStatus()const{return core_.last_error?core_.last_error:setup_.last_status;}
 void gains(uint8_t speaker,uint8_t microphone){if(hooks_.gains)hooks_.gains(speaker,microphone,hooks_.user);}
 bool enabled()const{return configured_;}
 bool ready()const;bool wideband()const;
 Status attach(uint32_t owner,uint16_t acl_handle,const uint8_t peer_hci_address[6]);
 bth_status_t event(const bth_h4_frame_t&,bool secure,bool profile_ready,owned::VoiceCodec,uint32_t now_ms,uint32_t command_owner);
 bth_status_t receive(const bth_sco_view_t&,bool secure);
 bth_status_t validateCompleted(const bth_h4_frame_t&)const;
 bth_status_t completed(const bth_h4_frame_t&);
 void poll(bool authorized,uint32_t now_ms);
 bth_status_t nextCommand(bth_sco_command_t& c){return bth_sco_next_command(&core_,&c);}
 bth_status_t commandQueued(bth_sco_token_t t,uint32_t now){return bth_sco_command_queued(&core_,t,now);}
 bth_status_t prepareSubmission(uint8_t*,size_t,size_t&,VoiceAudioSubmission&);
 bth_status_t commitSubmission(const VoiceAudioSubmission&);
 bool active()const{return core_.initialized&&core_.state==BTH_SCO_CONNECTED;}
 bool ownsHandle(uint16_t h)const{return core_.initialized&&core_.active_handle&&core_.handle==h;}
 bool needsPeerRetirement()const{return core_.initialized&&core_.state==BTH_SCO_FAILED;}
 bool preempting()const{return core_.initialized&&core_.state!=BTH_SCO_ATTACHED&&core_.state!=BTH_SCO_CLOSED;}
 uint32_t generation()const{return core_.generation;}
 owned::VoiceCodec codec()const{return core_.config.codec==BTH_SCO_MSBC?owned::VoiceCodec::Msbc:owned::VoiceCodec::Cvsd;}
 const bth_sco_t& state()const{return core_;}
private:
 void observe();bth_sco_config_t policy(uint32_t,uint16_t,const uint8_t*)const;
 VoiceAudioHostConfig config_{};VoiceAudioHostHooks hooks_{};bth_sco_setup_t setup_{};bth_sco_t core_{};
 uint8_t* storage_=nullptr;size_t storage_bytes_=0;uint8_t features_[8]{},tx_bytes_=0;uint16_t manufacturer_=0,tx_packets_=0;
 uint32_t last_generation_=0,notified_=0,now_=0;uint16_t fragment_bytes_=0,completed_bytes_=0,committed_unit_bytes_=0;
 uint16_t submitted_lengths_[8]{};uint8_t submitted_read_=0,submitted_count_=0;uint8_t copy_[60]{};
 bool configured_=false,was_connected_=false;
 VoiceAudioSubmission pending_consume_{},prepared_tx_{};uint32_t consume_deadline_=0;
};
}}
