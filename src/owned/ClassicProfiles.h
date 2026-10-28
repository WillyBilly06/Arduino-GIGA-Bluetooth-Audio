#pragma once
#include "../platform/GigaRadio.h"
#include "../platform/GigaAvrcp.h"
extern "C" {
#include "core/bth_control_tx.h"
#include "core/bth_avdtp.h"
#include "core/bth_avrcp.h"
#include "core/bth_avrcp_ct.h"
#include "core/bth_sdp.h"
}
namespace giga_bredr { namespace detail {
// The actor serializes every call and owns queue attachment and L2CAP teardown.
class ClassicProfiles {
public:
 Status begin(uint32_t codec_mask,const RadioCallbacks& callbacks,bth_control_tx_t& replies,uint8_t peer_index=0);
 Status installVoiceRecords(const bth_sdp_record_t*,size_t);
 void reset(uint32_t generation);
 // Internal actor gate. False START retains the exact SDU without preparing
 // output or accepting START. Stop notifications request normal asynchronously.
 void setStreamPolicy(bool(*policy)(bool,void*),void* user){stream_policy_=policy;stream_policy_user_=user;}
 bool channelOpened(uint16_t psm,uint16_t local_cid,uint16_t remote_cid,uint16_t remote_mtu);
 // Refresh from the actor's canonical accepted OPEN channel, without reopening
 // a profile or changing its generation, role, security, or protocol state.
 bool channelMtuUpdated(uint32_t generation,uint16_t psm,uint16_t local_cid,uint16_t remote_cid,uint16_t remote_mtu);
 void channelClosed(uint16_t local_cid);
 // FALSE when reply capacity or the asynchronous START policy is pending: retain this exact SDU.
 // Application callbacks run synchronously and must not reenter the host actor.
 bool receive(uint16_t local_cid,const uint8_t* bytes,size_t length,bool secure,uint32_t now_ms);
 // Pure eligibility check; never consumes a response or changes CT state.
 bool independentReadResponse(uint16_t local_cid,const uint8_t* bytes,size_t length,uint32_t now_ms) const;
 // Expiration always runs; older retained input can suppress optional requests.
 void poll(bool secure,uint32_t now_ms,bool allow_transmit=true);
 Status setVolume(uint8_t);
 bool wantsMediaChannel() const;
 bool takeCloseMedia();
 bool takeCloseAvrcp();
 uint16_t signalCid() const;
 uint16_t mediaCid() const;
 uint16_t avrcpCid() const;
 uint16_t avrcpRemoteCid() const;
 uint16_t retiringAvrcpLocal() const {return retired_avrcp_owner_==generation_?retired_avrcp_local_:0;}
 uint16_t retiringAvrcpRemote() const {return retired_avrcp_owner_==generation_?retired_avrcp_remote_:0;}
 bool avrcpRetirementPending() const;
 bool configured() const;
 bool streaming() const;
 bool abortTransportPending() const;
 void abortTransportRetired(); // Actor confirms all non-signaling AVDTP channels retired.
 const CodecConfig& acceptedConfiguration() const;
 AvrcpStatus avrcpStatus() const;
 AvrcpMetadata avrcpMetadata() const;
 Status avrcpCommand(AvrcpCommand,uint32_t expected_generation,bool secure,uint32_t now_ms,uint32_t& request_id);
 Status avrcpCancel(uint32_t expected_generation,uint32_t request_id);
 uint32_t receivedPackets() const;
 uint32_t receivedBytes() const;
 uint32_t droppedPackets() const;
private:
 struct Channel { uint16_t psm=0,local=0,remote=0,mtu=0; bth_sdp_cursor_t cursor={}; };
 bool validOwner() const;
 Channel* channel(uint16_t);
 void stop(bool invalidate);
 void diagnose(const char*);
 bool finish(const Channel&,bth_control_reservation_t&,bth_status_t,size_t);
 bool signaling(Channel&,const uint8_t*,size_t,bool,uint32_t,bth_control_reservation_t&);
 void controllerResponse(const uint8_t*,size_t,bool,uint32_t);
 void customResponse(uint8_t,const uint8_t*,size_t,uint32_t);
 bool customRequest(uint8_t,uint8_t,uint32_t,bth_control_reservation_t&,size_t,size_t&);
 bool emitMetadata(const uint8_t*,size_t);
 bool commandActive() const;
 void disconnectControl();
 bool transmitTap(Channel&,uint32_t);
 void completeTap();
 void expireControl(uint32_t now_ms);
 RadioCallbacks callbacks_;
 bool(*stream_policy_)(bool,void*)=nullptr;void* stream_policy_user_=nullptr;
 bth_control_tx_t* replies_=nullptr;
 uint32_t generation_=0,mask_=0,received_=0,bytes_=0,dropped_=0;
 uint8_t peer_=0;
 bool active_=false,valid_=false,playing_=false,close_media_=false,close_avrcp_=false;
 uint16_t signal_=0,media_=0,avrcp_=0;
 uint16_t retired_avrcp_local_=0,retired_avrcp_remote_=0;uint32_t retired_avrcp_owner_=0;
 CodecConfig accepted_;
 bth_avdtp_endpoint_t endpoints_[9]={};
 size_t endpoint_count_=0;
 bth_avdtp_t avdtp_={};
 uint8_t avdtp_assembly_[256]={};
 bth_sdp_t sdp_={};
 bth_sdp_record_t records_[5]={};
 size_t record_count_=0;
 Channel channels_[6];
 bth_avrcp_t target_={};
 bth_avrcp_ct_t controller_={};
 AvrcpStatus control_status_;
 AvrcpMetadata metadata_;
 uint32_t control_generation_=0,command_serial_=0,tap_deadline_=0;
 uint8_t press_label_=0,release_label_=0;
 bool press_done_=false,release_done_=false,tap_timed_out_=false;
 bool metadata_obsolete_=false,status_obsolete_=false;
 uint32_t avdtp_deadline_=0,target_deadline_=0,controller_deadline_=0,custom_deadline_=0,metadata_deadline_=0;
 uint8_t work_=0;
 bool capabilities_sent_=false,metadata_active_=false,metadata_continue_=false,metadata_abort_=false;
 uint8_t metadata_fragments_=0;
 size_t metadata_used_=0;
 uint8_t metadata_bytes_[1024]={};
 bool custom_active_=false;
 uint8_t custom_label_=0,custom_remaining_=0;
 size_t custom_used_=0;
 uint8_t custom_bytes_[2048]={};
};
}}
