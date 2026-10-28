#pragma once
#include "ClassicProfiles.h"
#include "LeProfiles.h"
#include "ControllerAudioPriority.h"
#include "VoiceProfiles.h"
#include "OwnedVoiceAudio.h"
#include "VoiceSdp.h"
#ifndef GIGA_BREDR_PRIVATE_SCHED_WITNESS
#define GIGA_BREDR_PRIVATE_SCHED_WITNESS 0
#endif
extern "C" {
#include "core/bth_gap.h"
#include "core/bth_avrcp_link.h"
#include "core/bth_links.h"
#include "core/bth_l2cap_rx.h"
#include "core/bth_l2cap_sig.h"
#include "core/bth_rx_flow.h"
#include "core/bth_rfcomm_link.h"
}
namespace giga_bredr { namespace detail {
struct OwnedHostConfig {
 const char* name=nullptr;uint32_t codecMask=AllCodecs;RadioCallbacks callbacks;BleConfig ble;
 const uint8_t* firmware=nullptr;size_t firmwareSize=0;uint8_t address[6]={};uint32_t baud=2000000;
 // Opt-in unadvertised control integration. Caller reserves this object and
 // retains it through end(); only the serialized actor may operate on it.
 VoiceProfiles* voice_control=nullptr;VoiceControlConfig voice_config;
 // Optional preconfigured caller-owned transport. Hooks only copy bounded
 // worker mailboxes. The null default performs no SCO setup or extra I/O.
 OwnedVoiceAudio* voice_audio=nullptr;
 VoiceSdp* voice_sdp=nullptr;
};
struct OwnedHostHooks {
 bool (*txReady)(void*)=nullptr;
 // On true, copies the complete H4 packet before returning. A false result is
 // terminal: queue/credit state was committed for this available submission.
 bool (*sendH4)(const uint8_t*,size_t,void*)=nullptr;
 bool (*setBaud)(uint32_t,void*)=nullptr;void* user=nullptr;
};
// Single actor owner. No IRQ entry, worker callbacks, heap or platform headers.
// Firmware is borrowed through startup; all other input is copied or consumed.
class OwnedHost {
public:
 Status begin(const OwnedHostConfig&,const OwnedHostHooks&);
 void poll(uint32_t nowMs);
 // Serialized foreground only: return real RX credits without advancing actor work.
 void serviceReceiveCredits(uint32_t nowMs);
 void receive(uint8_t type,const uint8_t* packetWithoutType,size_t length,uint32_t nowMs);
 void end();
 RadioStats radioStats() const;
 BleStats bleStats() const {BleStats s=le_.stats();s.qos_data_fragments=qos_data_fragments_;s.qos_data_bytes=qos_data_bytes_;s.qos_paced_deferrals=qos_paced_deferrals_;s.qos_urgent_bypasses=qos_urgent_bypasses_;if(!active_){s.advertising=s.connected=s.notify_enabled=s.encrypted=false;s.generation=0;s.connection_handle=0;}return s;}
 Status bleNotify(const uint8_t* data,size_t length) {return active_?le_.notify(data,length):Status::Unsupported;}
 Status bleDisconnect(uint32_t expected_generation,bool advertise_after_disconnect=true);
 Status bleResumeAdvertising();
 Status voiceStatus(VoiceStatus&)const;
 Status voiceCommand(HfpCommand,uint32_t expected_generation,const char* argument);
 bool bleReadWrite(BleWrite& value) {return active_&&le_.readWrite(value);}
 Status setVolume(uint8_t v) {return active_&&config_.callbacks.enable_a2dp?classic_.setVolume(v):Status::Unsupported;}
 AvrcpStatus avrcpStatus() const {return classic_.avrcpStatus();}
 AvrcpMetadata avrcpMetadata() const {return classic_.avrcpMetadata();}
 Status avrcpCommand(AvrcpCommand command,uint32_t expected,uint32_t& id){
  if(!active_||!config_.callbacks.enable_a2dp)return Status::Unsupported;
  if(avrcp_retiring_)return Status::Busy;
  const bool secure=!deferred_count_&&was_secure_&&peers_[0].attached&&bth_links_secure(&links_,peers_[0].handle);
  return classic_.avrcpCommand(command,expected,secure,now_,id);
 }
 Status avrcpCancel(uint32_t expected,uint32_t id){return active_&&config_.callbacks.enable_a2dp?classic_.avrcpCancel(expected,id):Status::Unsupported;}
 static constexpr unsigned AclBytes=1021,RxSlots=8,RxWindow=4,SduBytes=4096;
private:
 void receiveImpl(uint8_t,const uint8_t*,size_t,uint32_t);
 void pollImpl(uint32_t);
 void serviceReceiveCreditsImpl(uint32_t);
 bool deliverImpl(unsigned,const bth_l2cap_pdu_t&,uint16_t*);
 void synchronizeChannelsImpl();
 void resetClassic(uint32_t);
 void pollClassic(bool,uint32_t,bool);
 Status configureOutput(const CodecConfig&);
 void notifyStream(bool);
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
 void creditBoundary(uint32_t boundary,bool complete,uint32_t generation=0) const;
#else
 void creditBoundary(uint32_t,bool,uint32_t=0) const {}
#endif
 enum class Boot:uint8_t {PatchReset,Minidriver,Patch,Settle,Reset,Version,Features,AddressWrite,AddressRead,Buffers,LeBuffers,LeFeatures,Baud,LeHost,VoiceSetup,Gap,Ready,Failed};
 struct Packet {uint8_t bytes[AclBytes+4];uint16_t length=0;uint32_t order=0,barrier=0;bool used=false,charged=false; bth_rx_lease_t lease={};};
 struct Deferred {uint8_t bytes[257];uint16_t length=0;uint32_t serial=0;};
 struct Peer {
  bth_l2cap_rx_t assembly={};uint8_t bytes[SduBytes]={};bth_l2cap_pdu_t pending={};uint16_t pending_signal_cursor=0;
  uint32_t owner=0,security_deadline=0,disconnect_deadline=0,assembly_deadline=0,pending_deadline=0;
  uint32_t channel_deadlines[BTH_CHANNEL_LIMIT]={};bool opened[BTH_CHANNEL_LIMIT]={};
  uint16_t handle=0;bool attached=false,has_pending=false;int borrowed_slot=-1;
 };
 struct Bond {uint8_t address[6]={};bth_bond_t value={};bool used=false;};
 // Only Classic safe controls use the tail of Peer.bytes. BLE keeps all4096.
 static constexpr unsigned ClassicMtu=2048,ClassicAssemblyBytes=ClassicMtu+4,ClassicDeferredSlots=3;
 static_assert(ClassicAssemblyBytes+ClassicDeferredSlots*BTH_CONTROL_BYTES<=SduBytes,"Classic receive storage partition");
 struct DeferredControl {uint16_t cid=0,length=0;uint32_t deadline=0;uint16_t signal_cursor=0;};
 static bool loadBond(void*,const uint8_t*,bth_bond_t*);
 static bool saveBond(void*,const uint8_t*,const bth_bond_t*);
 static bool removeBond(void*,const uint8_t*);
 static bth_confirmation_t confirm(void*,const uint8_t*,uint32_t);
 void fail(const char*);void diagnose(const char*);void receiveWindowReceipt(const bth_acl_view_t&,unsigned);
 bool submit(size_t);void bootPoll();void bootEvent(const bth_h4_frame_t&);bool bootCommand(uint16_t,const uint8_t*,size_t);
 bool dispatch(const bth_h4_frame_t&);bool defer(const uint8_t*,size_t);
 bool attach(unsigned,const bth_peer_link_t&);void detach(unsigned);
 bool deliver(unsigned,const bth_l2cap_pdu_t&,uint16_t* signal_cursor=nullptr);void processRx(unsigned);
 bool expandedClassicControl(const bth_l2cap_pdu_t&) const;
 bool safeClassicControl(const bth_l2cap_pdu_t&) const;
 bool gateAvrcpRetirement(const bth_l2cap_pdu_t&);
 bool copiedAvrcpRetirement(const bth_l2cap_pdu_t&) const;
 bool avrcpRetirementReady() const;void clearAvrcpRetirement();
 bool gateVoiceRetirement(const bth_l2cap_pdu_t&);
 bool copiedVoiceRetirement(const bth_l2cap_pdu_t&)const;
 bool voiceRetirementReady()const;void clearVoiceRetirement();
 bool deferClassicControl(const bth_l2cap_pdu_t&,uint32_t,uint16_t signal_cursor=0);bool retryClassicControls();void clearClassicControls(uint16_t cid=0);
 void synchronizeChannels();bool disconnectChannel(uint16_t);void chargeWaiting(uint32_t);void closePeer(unsigned,uint8_t);
 void avrcpPoll(bool secure,bool allow_open=true);void runtimePoll();void refreshStats();bool admission(unsigned,uint32_t);void cancelAdmission(unsigned);
 bool updateAudioPriority();
 bool voiceAuthorized()const;owned::VoiceCodec voiceCodec()const;void voicePoll();
 static bool streamPolicy(bool,void*);
 bool requestControllerPriority(bool);void serviceControllerPriority(bool secure);
 bool controllerPriorityCommandPresent(uint32_t owner) const;
 OwnedHostConfig config_;OwnedHostHooks hooks_;RadioStats stats_;
 ClassicProfiles classic_;LeProfiles le_;ControllerAudioPriority controller_priority_;bth_hci_flow_t flow_={};bth_cmdq_t commands_={};bth_links_t links_={};
 bth_control_tx_t control_={};bth_rx_flow_t rx_={};bth_gap_t gap_={};bth_l2cap_sig_t signaling_={};
 Packet packets_[RxSlots];Peer peers_[2];Bond bonds_[4];bth_rx_admission_t admissions_[2]={};bool reserved_[2]={};
 uint8_t expanded_control_budget_=0;
 DeferredControl classic_deferred_[ClassicDeferredSlots];uint32_t classic_deferred_owner_=0;uint8_t classic_deferred_read_=0,classic_deferred_count_=0;
 Deferred deferred_[8];unsigned deferred_read_=0,deferred_count_=0;
 uint8_t output_[1030]={};char name_[64]={};uint16_t uuids_[5]={0x110b,0x110c,0x110e};
 uint8_t le_features_[8]={};bool le_features_known_=false;
 uint8_t voice_features_[8]={},voice_tx_bytes_=0;uint16_t voice_tx_packets_=0;
 size_t patch_position_=0,patch_pending_bytes_=0;uint16_t pending_opcode_=0;
 uint32_t now_=0,begin_ms_=0,deadline_=0,settle_until_=0,packet_order_=0,command_deadline_=0;
 uint32_t sent_at_[BTH_CMD_QUEUE_LIMIT]={};uint32_t accept_deadline_=0;
 uint32_t event_serial_=0,retired_event_=0;uint16_t close_media_=0,close_avrcp_=0;
 uint32_t hnc_commands_=0,hnc_packets_=0,hnc_last_ms_=0;uint16_t hnc_handles_[2]={},hnc_counts_[2]={};
 uint32_t avrcp_at_=0;unsigned avrcp_attempts_=0;bool avrcp_recovering_=false;
 uint32_t avrcp_retire_owner_=0,avrcp_retire_deadline_=0;
 uint16_t avrcp_retire_local_=0,avrcp_retire_remote_=0;bool avrcp_retiring_=false;
 uint32_t voice_retire_owner_=0,voice_retire_deadline_=0;
 uint16_t voice_retire_local_=0,voice_retire_remote_=0;bool voice_retiring_=false;
 unsigned next_peer_=0,bond_cursor_=0,le_trace_=0;Boot boot_=Boot::Failed;
 uint8_t le_reply_observe_trace_=0; // Shared peer-reply CC/update quota, separate from GAP.
 uint32_t qos_last_data_ms_=0,qos_classic_owner_=0,qos_le_owner_=0;
 uint32_t qos_data_fragments_=0,qos_data_bytes_=0,qos_paced_deferrals_=0,qos_urgent_bypasses_=0;
 uint8_t qos_classic_burst_=0;bool qos_have_data_=false,qos_audio_active_=false;
 bool active_=false,clock_started_=false,pending_=false,tx_turn_=false,last_ack_=false,was_secure_=false;
 // Foreground admission state; controller lifecycle alone detaches the peer.
 uint32_t le_disconnect_owner_=0;
 bool le_advertising_held_=false,le_disconnect_advertise_after_=true;
};
}}
