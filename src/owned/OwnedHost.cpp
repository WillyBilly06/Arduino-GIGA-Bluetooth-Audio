#include "OwnedHost.h"
#include <new>
#include <stdio.h>
#include <string.h>
namespace giga_bredr { namespace detail {
namespace {
bool due(uint32_t now,uint32_t deadline){return deadline&&int32_t(now-deadline)>=0;}
void erase(void* data,size_t n){volatile uint8_t* p=static_cast<volatile uint8_t*>(data);while(n--)*p++=0;}
unsigned completionSlots(const bth_h4_frame_t& e){
 // These four passive link events cannot enqueue an HCI command.
 if(e.event.code==0x1b||e.event.code==0x1d||e.event.code==0x14||e.event.code==0x12)return 0;
 const auto p=e.event.params;
 // Transport observations never enqueue commands from event dispatch.
 if(e.event.code==0x3e&&p.data&&((p.len==10&&p.data[0]==3)||(p.len==12&&p.data[0]==4)||(p.len==11&&p.data[0]==7)))return 0;
 // These LE requests copy into profile-owned pending fields; replies queue in poll.
 if(e.event.code==0x3e&&p.data&&((p.len==13&&p.data[0]==5)||(p.len==11&&p.data[0]==6)))return 0;
 if(e.event.code==0x0e&&p.len>=3){uint16_t op=bth_read_le16(p.data+1);return op==0x040b||op==0x040c||op==0x040d||op==0x040e||op==0x042b||op==0x0434||op==0x042c||op==0x042d||op==0x1408?1:0;}
 if(e.event.code==0x0f&&p.len==4){uint16_t op=bth_read_le16(p.data+2);return op==0x0411||op==0x0413?1:0;}
 return 2;
}
bool independentCompletion(const bth_h4_frame_t& e){
 // Passive events still remain FIFO behind older lifecycle/security events.
 if(e.event.code!=0x0e&&e.event.code!=0x0f)return false;
 if(completionSlots(e)!=0)return false;
 if(e.event.code==0x0f){uint16_t op=bth_read_le16(e.event.params.data+2);if(op==0x0409||op==0x0406)return false;}
 return true;
}
bool leCommandPreflight(const bth_cmdq_t& q,const bth_h4_frame_t& e){
 const auto p=e.event.params;uint16_t op;
 if(e.event.code==0x0e&&p.len>=3)op=bth_read_le16(p.data+1);
 else if(e.event.code==0x0f&&p.len>=4)op=bth_read_le16(p.data+2);
 else return true; // Unidentifiable malformed events retain generic HCI handling.
 const bool status=op==0x2013||op==0x2016;
 if(!status&&op!=0x201b&&op!=0x2020&&op!=0x2021)return true;
 const bth_command_sent_t* sent=nullptr;
 for(unsigned i=0;i<q.sent_count;++i)if(q.sent[i].opcode==op){sent=&q.sent[i];break;}
 if(!sent)return false; // No duplicate/unowned peer completion can regrant credit.
 if(status)return e.event.code==0x0f&&p.len==4;
 if(e.event.code!=0x0e||p.len<4)return false;
 if(p.data[3])return p.len==4||p.len==6; // Handle is undefined on command failure.
 return p.len==6&&bth_read_le16(p.data+4)==sent->handle;
}


}
void OwnedHost::diagnose(const char* s){if(config_.callbacks.diagnostic)config_.callbacks.diagnostic(s,config_.callbacks.user);}
void OwnedHost::fail(const char* reason){if(boot_==Boot::Failed)return;++stats_.transport_errors;diagnose(reason);end();boot_=Boot::Failed;}
bool OwnedHost::loadBond(void* context,const uint8_t* address,bth_bond_t* out){
 auto& h=*static_cast<OwnedHost*>(context);for(const auto& b:h.bonds_)if(b.used&&!memcmp(address,b.address,6)){*out=b.value;return true;}return false;
}
bool OwnedHost::saveBond(void* context,const uint8_t* address,const bth_bond_t* value){
 auto& h=*static_cast<OwnedHost*>(context);Bond* slot=nullptr;for(auto& b:h.bonds_)if(b.used&&!memcmp(address,b.address,6)){slot=&b;break;}
 if(!slot)slot=&h.bonds_[h.bond_cursor_++%4];erase(slot,sizeof *slot);memcpy(slot->address,address,6);slot->value=*value;slot->used=true;return true;
}
bool OwnedHost::removeBond(void* context,const uint8_t* address){auto& h=*static_cast<OwnedHost*>(context);for(auto& b:h.bonds_)if(b.used&&!memcmp(address,b.address,6))erase(&b,sizeof b);return true;}
bth_confirmation_t OwnedHost::confirm(void*,const uint8_t*,uint32_t){return BTH_CONFIRM_ACCEPT;}
Status OwnedHost::begin(const OwnedHostConfig& config,const OwnedHostHooks& hooks){
 if(active_)return Status::Busy;
 if(!config.name||!config.name[0]||!config.callbacks.configure||!hooks.txReady||!hooks.sendH4||!hooks.setBaud||!config.firmware||!config.firmwareSize||
    !(config.codecMask&codecBit(Codec::Sbc))||(config.codecMask&~AllCodecs)||
    (config.baud!=115200&&config.baud!=460800&&config.baud!=921600&&config.baud!=2000000))return Status::InvalidArgument;
 size_t name=0;while(name<sizeof name_&&config.name[name])++name;if(name==sizeof name_)return Status::InvalidArgument;
 bool zero=true,ones=true;for(auto b:config.address){zero&=b==0;ones&=b==255;}if(zero||ones)return Status::InvalidArgument;
 for(size_t p=0;p<config.firmwareSize;){if(config.firmwareSize-p<3||size_t(config.firmware[p+2])+3>config.firmwareSize-p)return Status::FirmwareMismatch;p+=size_t(config.firmware[p+2])+3;}
 config_=config;hooks_=hooks;memcpy(name_,config.name,name+1);config_.name=name_;stats_=RadioStats{};stats_.baud_rate=115200;
 classic_.~ClassicProfiles();new(&classic_)ClassicProfiles();le_.~LeProfiles();new(&le_)LeProfiles();
 bth_hci_flow_init(&flow_);bth_cmdq_init(&commands_);bth_control_tx_init(&control_);
 if(!bth_rx_flow_init(&rx_,RxSlots,RxWindow))return Status::HardwareError;
 const bth_security_policy_t policy={3,4,16,4,{'0','0','0','0'}};
 const bth_security_callbacks_t callbacks={loadBond,saveBond,removeBond,confirm};
 if(!bth_links_init(&links_,&flow_,&commands_,&policy,&callbacks,this))return Status::HardwareError;
 Status result=classic_.begin(config.codecMask,config.callbacks,control_);if(result!=Status::Ok)return result;
 result=le_.begin(config.ble,name_,commands_,control_);if(result!=Status::Ok)return result;
 const bth_gap_config_t gapConfig={{reinterpret_cast<const uint8_t*>(name_),name},uuids_,0x240414,AclBytes,RxWindow,uint8_t(config.callbacks.volume?3:1),false};
 if(!bth_gap_start(&gap_,&gapConfig))return Status::HardwareError;
 memset(&signaling_,0,sizeof signaling_);for(auto& p:packets_) {p.used=p.charged=false;p.length=0;p.order=p.barrier=0;p.lease={};}
 for(auto& p:peers_) {p.attached=p.has_pending=false;p.owner=0;p.borrowed_slot=-1;}clearClassicControls();
 for(unsigned i=0;i<2;++i){reserved_[i]=false;admissions_[i]={};}
 erase(bonds_,sizeof bonds_);bond_cursor_=0;patch_position_=patch_pending_bytes_=0;pending_opcode_=0;
 hnc_commands_=hnc_packets_=hnc_last_ms_=0;memset(hnc_handles_,0,sizeof hnc_handles_);memset(hnc_counts_,0,sizeof hnc_counts_);
 deferred_count_=deferred_read_=0;event_serial_=retired_event_=0;close_media_=close_avrcp_=0;now_=begin_ms_=deadline_=settle_until_=packet_order_=command_deadline_=0;
 qos_last_data_ms_=qos_classic_owner_=qos_le_owner_=0;qos_data_fragments_=qos_data_bytes_=qos_paced_deferrals_=qos_urgent_bypasses_=0;qos_classic_burst_=0;qos_have_data_=qos_audio_active_=false;
 memset(le_features_,0,sizeof le_features_);le_features_known_=false;
 le_disconnect_owner_=0;le_advertising_held_=false;le_disconnect_advertise_after_=true;
 memset(sent_at_,0,sizeof sent_at_);accept_deadline_=0;next_peer_=le_trace_=0;clock_started_=pending_=tx_turn_=last_ack_=was_secure_=false;boot_=Boot::PatchReset;active_=true;return Status::Ok;
}
void OwnedHost::end(){
 le_disconnect_owner_=0;le_advertising_held_=false;le_disconnect_advertise_after_=true;
 if(!active_)return;active_=false;stats_.controller_ready=stats_.connected=stats_.streaming=false;
 for(unsigned i=0;i<2;++i){detach(i);cancelAdmission(i);}classic_.reset(0);
 for(auto& p:packets_)p.used=false;deferred_count_=0;bth_cmdq_init(&commands_);bth_control_tx_init(&control_);erase(bonds_,sizeof bonds_);erase(&links_,sizeof links_);
  pending_=false;boot_=Boot::Failed;memset(le_features_,0,sizeof le_features_);le_features_known_=false;
}
void OwnedHost::receiveWindowReceipt(const bth_acl_view_t& acl,unsigned uncharged){
 unsigned used=0,busy=0;for(unsigned k=0;k<RxSlots;++k){used+=packets_[k].used?1:0;busy+=rx_.slots[k].busy?1:0;}
 char line[128];snprintf(line,sizeof line,"Owned RXWIN handle=%04x pb=%u bc=%u n=%u out=%u raw=%u used=%u busy=%u budget=%u",acl.handle,acl.pb,acl.bc,unsigned(acl.payload.len),rx_.outstanding,uncharged,used,busy,rx_.budget);diagnose(line);
 for(unsigned i=0;i<BTH_RX_LINK_LIMIT;++i){const auto& link=rx_.links[i];unsigned slots=0;for(unsigned k=0;k<RxSlots;++k)if(rx_.slots[k].busy&&rx_.slots[k].owner==link.owner&&rx_.slots[k].handle==link.handle)++slots;
  snprintf(line,sizeof line,"Owned RXWIN link=%u active=%u reserved=%u handle=%04x owner=%lu out=%u pending=%u slots=%u",i,link.active,link.reserved,link.handle,(unsigned long)link.owner,link.outstanding,link.pending,slots);diagnose(line);
 }
 snprintf(line,sizeof line,"Owned HNC commands=%lu packets=%lu last_ms=%lu h0=%04x n0=%u h1=%04x n1=%u",(unsigned long)hnc_commands_,(unsigned long)hnc_packets_,(unsigned long)hnc_last_ms_,hnc_handles_[0],hnc_counts_[0],hnc_handles_[1],hnc_counts_[1]);diagnose(line);
}
bool OwnedHost::submit(size_t n){
 // These are actor commits, not UART transmission or controller reception.
 // The adapter independently records accepted, handed-to-UART and TC stages.
 if(n>=5&&output_[0]==1&&bth_read_le16(output_+1)==0x0c35){++hnc_commands_;hnc_last_ms_=now_;memset(hnc_handles_,0,sizeof hnc_handles_);memset(hnc_counts_,0,sizeof hnc_counts_);
  for(unsigned i=0;i<output_[4]&&i<2&&9u+4u*i<=n;++i){hnc_handles_[i]=bth_read_le16(output_+5+4*i);hnc_counts_[i]=bth_read_le16(output_+7+4*i);hnc_packets_+=hnc_counts_[i];}
 }
 if(n>sizeof output_||!hooks_.sendH4(output_,n,hooks_.user)){fail("Owned HCI submission failed; controller reset required");return false;}return true;}
bool OwnedHost::bootCommand(uint16_t opcode,const uint8_t* params,size_t count){
 size_t n=0;if(!flow_.command_credits||!hooks_.txReady(hooks_.user))return false;
 if(bth_hci_command_encode(output_,sizeof output_,opcode,params,count,&n)!=BTH_OK||!bth_hci_flow_take_command(&flow_)){fail("Owned boot command encoding failed");return false;}
 pending_=true;pending_opcode_=opcode;deadline_=now_+3000;return submit(n);
}
void OwnedHost::bootPoll(){
 if(pending_) {if(due(now_,deadline_))fail("Owned controller command ACK timeout");return;}
 if(boot_==Boot::Settle){if(!due(now_,settle_until_))return;boot_=Boot::Reset;}
 uint8_t params[8]={};
 switch(boot_){
 case Boot::PatchReset:bootCommand(0x0c03,nullptr,0);break;
 case Boot::Minidriver:bootCommand(0xfc2e,nullptr,0);break;
 case Boot::Patch:
  if(patch_position_==config_.firmwareSize){boot_=Boot::Settle;settle_until_=now_+1000;break;}
  patch_pending_bytes_=size_t(config_.firmware[patch_position_+2])+3;
  bootCommand(bth_read_le16(config_.firmware+patch_position_),config_.firmware+patch_position_+3,patch_pending_bytes_-3);break;
 case Boot::Reset:bootCommand(0x0c03,nullptr,0);break;
 case Boot::Version:bootCommand(0x1001,nullptr,0);break;
 case Boot::Features:bootCommand(0x1003,nullptr,0);break;
 case Boot::AddressWrite:for(unsigned i=0;i<6;++i)params[i]=config_.address[5-i];bootCommand(0xfc01,params,6);break;
 case Boot::AddressRead:bootCommand(0x1009,nullptr,0);break;
 case Boot::Buffers:bootCommand(0x1005,nullptr,0);break;
 case Boot::LeBuffers:bootCommand(0x2002,nullptr,0);break;
 case Boot::LeFeatures:bootCommand(0x2003,nullptr,0);break;
 case Boot::Baud:
  if(config_.baud==115200){boot_=Boot::LeHost;break;}
  params[2]=uint8_t(config_.baud);params[3]=uint8_t(config_.baud>>8);params[4]=uint8_t(config_.baud>>16);params[5]=uint8_t(config_.baud>>24);bootCommand(0xfc18,params,6);break;
 case Boot::LeHost:params[0]=config_.ble.enabled?1:0;bootCommand(0x0c6d,params,2);break;
 case Boot::Gap:{
  if(!hooks_.txReady(hooks_.user))break;size_t n=0;bth_status_t result=bth_gap_next_command(&gap_,&flow_,output_,sizeof output_,&n);
  if(result==BTH_OK){pending_=true;pending_opcode_=gap_.pending_opcode;deadline_=now_+3000;submit(n);}
  else if(result<0)fail("Owned GAP setup failed");
  else if(gap_.stage==BTH_GAP_READY){boot_=Boot::Ready;stats_.controller_ready=true;diagnose("Owned Classic/LE controller verified; profile admission ready");}
  break;}
 default:break;
 }
}
void OwnedHost::bootEvent(const bth_h4_frame_t& event){
 if(!pending_||event.type!=BTH_H4_EVENT)return;const auto p=event.event.params;
 if(event.event.code==0x0f){if(p.len==4&&bth_read_le16(p.data+2)==pending_opcode_&&p.data[0])fail("Owned boot command status rejected");return;}
 if(event.event.code!=0x0e||p.len<3||bth_read_le16(p.data+1)!=pending_opcode_)return;
 if(p.len<4||p.data[3]){fail("Owned boot command rejected");return;}
 if(boot_==Boot::Gap){if(bth_gap_on_event(&gap_,&event)<0){fail("Owned GAP completion rejected");return;}pending_=false;return;}
 bool valid=true;
 switch(boot_){
 case Boot::PatchReset:boot_=Boot::Minidriver;break;
 case Boot::Minidriver:boot_=Boot::Patch;break;
 case Boot::Patch:patch_position_+=patch_pending_bytes_;patch_pending_bytes_=0;break;
 case Boot::Reset:if(config_.ble.enabled&&!le_.startAfterReset(event)){fail("Owned LE reset bootstrap rejected");return;}boot_=Boot::Version;break;
 case Boot::Version:
  valid=p.len==12;if(valid){stats_.manufacturer=bth_read_le16(p.data+8);stats_.lmp_subversion=bth_read_le16(p.data+10);valid=stats_.manufacturer==15||stats_.manufacturer==305;}boot_=Boot::Features;break;
 case Boot::Features:
  valid=p.len==12;if(valid){stats_.bredr_supported=(p.data[8]&0x20)==0;valid=stats_.bredr_supported&&(p.data[10]&8)&&(!config_.ble.enabled||(p.data[8]&0x40));}boot_=Boot::AddressWrite;break;
 case Boot::AddressWrite:valid=p.len==4;boot_=Boot::AddressRead;break;
 case Boot::AddressRead:
  valid=p.len==10;if(valid)for(unsigned i=0;i<6;++i){stats_.address[i]=p.data[9-i];if(stats_.address[i]!=config_.address[i])valid=false;}boot_=Boot::Buffers;break;
 case Boot::Buffers:
  valid=p.len==11;if(valid){uint16_t length=bth_read_le16(p.data+4);valid=bth_hci_flow_set_buffers(&flow_,length>AclBytes?AclBytes:length,bth_read_le16(p.data+7),0,0);}boot_=config_.ble.enabled?Boot::LeBuffers:Boot::Baud;break;
 case Boot::LeBuffers:
  valid=p.len==7;if(valid){uint16_t length=bth_read_le16(p.data+4);valid=bth_hci_flow_set_buffers(&flow_,flow_.classic.length,flow_.classic.total,length>AclBytes?AclBytes:length,p.data[6]);}boot_=Boot::LeFeatures;break;
 case Boot::LeFeatures:valid=p.len==12;if(valid){uint8_t any=0;for(unsigned i=4;i<12;++i)any|=p.data[i];valid=any!=0;if(valid){memcpy(le_features_,p.data+4,8);le_features_known_=true;}}boot_=Boot::Baud;break;
 case Boot::Baud:valid=p.len==4&&hooks_.setBaud(config_.baud,hooks_.user);if(valid)stats_.baud_rate=config_.baud;boot_=Boot::LeHost;break;
 case Boot::LeHost:valid=p.len==4;boot_=Boot::Gap;break;
 default:valid=false;break;
 }
 if(!valid){fail("Owned post-patch controller verification failed");return;}pending_=false;
}
bool OwnedHost::admission(unsigned i,uint32_t owner){if(reserved_[i])return true;if(!bth_rx_flow_reserve(&rx_,owner,&admissions_[i]))return false;reserved_[i]=true;return true;}
void OwnedHost::cancelAdmission(unsigned i){if(reserved_[i]){bth_rx_flow_cancel_admission(&rx_,admissions_[i]);reserved_[i]=false;}}
bool OwnedHost::attach(unsigned i,const bth_peer_link_t& link){
 if(peers_[i].attached)return peers_[i].owner==link.owner;
 if(!reserved_[i]||!bth_rx_flow_attach_reserved(&rx_,admissions_[i],link.handle,link.owner))return false;reserved_[i]=false;
 if(!bth_control_tx_attach(&control_,uint8_t(i),link.handle,link.owner))return false;
 Peer& peer=peers_[i];peer.owner=link.owner;peer.handle=link.handle;peer.attached=true;peer.has_pending=false;peer.borrowed_slot=-1;peer.pending_signal_cursor=0;
 peer.security_deadline=now_+35000;peer.disconnect_deadline=peer.assembly_deadline=peer.pending_deadline=0;memset(peer.opened,0,sizeof peer.opened);memset(peer.channel_deadlines,0,sizeof peer.channel_deadlines);
 if(!bth_l2cap_rx_init(&peer.assembly,link.handle,peer.bytes,i==0?ClassicAssemblyBytes:sizeof peer.bytes))return false;
 if(i==0){clearClassicControls();const bth_l2cap_service_t services[]={{1,672,false},{0x19,ClassicMtu,true},{0x17,672,true}};
  if(!bth_l2cap_sig_init(&signaling_,link.handle,services,3))return false;classic_.reset(link.owner);close_media_=close_avrcp_=0;was_secure_=false;avrcp_at_=0;avrcp_attempts_=0;avrcp_recovering_=false;
 }else if(!le_.attach(link.owner,link.handle,link.role,le_features_known_?le_features_:nullptr,now_))return false;
 return true;
}
void OwnedHost::detach(unsigned i){
 Peer& peer=peers_[i];if(!peer.attached)return;
 bth_rx_flow_detach(&rx_,peer.handle,peer.owner);
 for(auto& packet:packets_)if(packet.used&&packet.charged&&rx_.slots[packet.lease.slot].owner==peer.owner){bth_rx_flow_release(&rx_,packet.lease);packet.used=false;}
 bth_control_tx_detach(&control_,uint8_t(i),peer.owner);
 if(i==0){clearClassicControls();classic_.reset(0);memset(&signaling_,0,sizeof signaling_);was_secure_=false;}else le_.detach(peer.owner);
 peer.attached=peer.has_pending=false;peer.owner=0;peer.borrowed_slot=-1;peer.pending_signal_cursor=0;
 if(i==1){le_disconnect_owner_=0;le_disconnect_advertise_after_=true;}
}
Status OwnedHost::bleDisconnect(uint32_t expected,bool advertise_after){
 if(!active_||!config_.ble.enabled)return Status::Unsupported;
 if(!expected)return Status::InvalidArgument;
 if(boot_!=Boot::Ready)return Status::Busy;
 const auto& peer=peers_[1];const auto& link=links_.le;const auto& stats=le_.stats();
 if(!peer.attached||!stats.connected||peer.owner!=expected||stats.generation!=peer.owner||
    stats.connection_handle!=peer.handle||link.state!=BTH_PEER_CONNECTED||link.owner!=peer.owner||link.handle!=peer.handle)
  return Status::InvalidArgument;
 // Retained lifecycle events and an older GAP enable must retire first. In
 // particular, never commit a hold which an in-flight200a could undo at detach.
 if(deferred_count_||le_.controlState().pending)return Status::Busy;
 if(link.disconnecting)return le_disconnect_owner_==expected&&le_disconnect_advertise_after_==advertise_after?Status::Ok:Status::Busy;
 if(bth_cmdq_available(&commands_)<3)return Status::Busy; // Preserve two security slots.
 const auto result=bth_links_disconnect(&links_,peer.handle,0x13);
 if(result==BTH_ERR_CAPACITY)return Status::Busy;
 if(result!=BTH_OK)return Status::HardwareError;
 le_disconnect_owner_=expected;le_disconnect_advertise_after_=advertise_after;
 if(!advertise_after)le_advertising_held_=true;
 if(!peers_[1].disconnect_deadline)peers_[1].disconnect_deadline=now_+5000;
 return Status::Ok;
}
Status OwnedHost::bleResumeAdvertising(){
 if(!active_||!config_.ble.enabled)return Status::Unsupported;
 if(boot_!=Boot::Ready||deferred_count_)return Status::Busy;
 if(!le_advertising_held_)return Status::Ok;
 if(peers_[1].attached||le_.stats().connected)return Status::Busy;
 le_advertising_held_=false;return Status::Ok;
}
void OwnedHost::closePeer(unsigned i,uint8_t reason){if(!peers_[i].attached)return;if(bth_links_disconnect(&links_,peers_[i].handle,reason)==BTH_ERR_CAPACITY)return;
 if(!peers_[i].disconnect_deadline)peers_[i].disconnect_deadline=now_+5000;if(i==0){clearClassicControls();classic_.reset(0);}
}
bool OwnedHost::defer(const uint8_t* packet,size_t n){if(deferred_count_==8||n>sizeof deferred_[0].bytes){fail("Owned event backlog exhausted");return false;}
 auto& d=deferred_[(deferred_read_+deferred_count_)%8];memcpy(d.bytes,packet,n);d.length=uint16_t(n);d.serial=++event_serial_;if(!d.serial)d.serial=++event_serial_;++deferred_count_;return true;
}
bool OwnedHost::dispatch(const bth_h4_frame_t& event){
 // Two command slots cover the largest security transition without mutating a
 // link and then discovering that its required command has no storage.
 if(bth_cmdq_available(&commands_)<completionSlots(event))return false;
 const auto p=event.event.params;
 const uint32_t previous_le_owner=peers_[1].attached?peers_[1].owner:0;
 const uint16_t previous_le_handle=peers_[1].handle;
 if(event.event.code==4&&p.len==10&&links_.classic.state==BTH_PEER_EMPTY&&!reserved_[0]){
  uint8_t reply[7];memcpy(reply,p.data,6);reply[6]=0x0d;bth_cmdq_push(&commands_,0x040a,reply,7,true);return true;
 }
 if(event.event.code==0x3e&&p.len&&(p.data[0]==1||p.data[0]==0x0a)&&p.len>4&&!p.data[1]&&(!config_.ble.enabled||!reserved_[1])){
  uint8_t reply[3]={p.data[2],p.data[3],0x0d};bth_cmdq_push(&commands_,0x0406,reply,3,true);return true;
 }
 // The link kernel consumes the command ownership record exactly once. It
 // deliberately does not update global command/ACL credits.
 unsigned previous_sent=commands_.sent_count,completed_index=BTH_CMD_QUEUE_LIMIT;uint32_t command_owner=0;
 if((event.event.code==0x0e&&p.len>=3)||(event.event.code==0x0f&&p.len==4)){const uint16_t op=bth_read_le16(p.data+(event.event.code==0x0e?1:2));for(unsigned k=0;k<previous_sent;++k)if(commands_.sent[k].opcode==op){completed_index=k;command_owner=commands_.sent[k].owner;break;}}
 links_.security_now_ms=now_;
 bth_status_t result=bth_links_on_event(&links_,&event);
 if(commands_.sent_count<previous_sent&&completed_index<previous_sent){for(unsigned k=completed_index+1;k<previous_sent;++k)sent_at_[k-1]=sent_at_[k];sent_at_[commands_.sent_count]=0;}
 if(result<0){fail("Owned link/security event rejected");return true;}
 if(links_.classic.state==BTH_PEER_PENDING){if(!accept_deadline_)accept_deadline_=now_+15000;}else accept_deadline_=0;
 const bth_peer_link_t* live[]={&links_.classic,&links_.le};
 for(unsigned i=0;i<2;++i){
  if(peers_[i].attached&&(live[i]->state!=BTH_PEER_CONNECTED||live[i]->owner!=peers_[i].owner))detach(i);
  if(live[i]->state==BTH_PEER_CONNECTED&&!peers_[i].attached&&!attach(i,*live[i])){fail("Owned connection lacked reserved receive storage");return true;}
 }
 if(links_.classic.state==BTH_PEER_EMPTY&&reserved_[0]&&event.event.code==0x0f&&p.len==4&&bth_read_le16(p.data+2)==0x0409&&p.data[0])cancelAdmission(0);
 const auto leControl=le_.controlState();
 if(le_trace_<32 && leControl.pending && ((event.event.code==0x0e&&p.len>=3&&bth_read_le16(p.data+1)==leControl.opcode)||(event.event.code==0x0f&&p.len==4&&bth_read_le16(p.data+2)==leControl.opcode))){
  char line[128];const unsigned status=event.event.code==0x0e?(p.len>=4?p.data[3]:0xff):p.data[0];
  snprintf(line,sizeof line,"Owned LE HCI event=%02x op=%04x status=%02x n=%u stage=%u credit=%u",event.event.code,leControl.opcode,status,unsigned(p.len),leControl.stage,flow_.command_credits);diagnose(line);++le_trace_;
 }
 le_.event(event,command_owner);
 // Bounded actor records are copied through the existing diagnostic mailbox;
 // user printing occurs only from foreground poll, never UART/DMA IRQ.
 char lifecycle[96];
 if(previous_le_owner&&!peers_[1].attached&&event.event.code==5&&p.len==4&&!p.data[0]&&bth_read_le16(p.data+1)==previous_le_handle){
  snprintf(lifecycle,sizeof lifecycle,"Owned LE detached owner=%lu handle=%04x generation=0",(unsigned long)previous_le_owner,previous_le_handle);diagnose(lifecycle);
 }else if(!previous_le_owner&&peers_[1].attached){
  snprintf(lifecycle,sizeof lifecycle,"Owned LE attached owner=%lu handle=%04x generation=%lu",(unsigned long)peers_[1].owner,peers_[1].handle,(unsigned long)le_.stats().generation);diagnose(lifecycle);
 }
 if(event.event.code==0x0e&&gap_.pending){if(bth_gap_on_event(&gap_,&event)<0){fail("Owned scan completion rejected");return true;}}
 return true;
}
void OwnedHost::receive(uint8_t type,const uint8_t* packet,size_t n,uint32_t now){
 if(!active_)return;now_=now;if(!clock_started_){begin_ms_=now;clock_started_=true;}
 if(!packet||n>1025){fail("Owned HCI input bounds");return;}uint8_t wire[1026];wire[0]=type;memcpy(wire+1,packet,n);bth_h4_frame_t event={};
 if(bth_h4_decode(wire,n+1,&event)!=BTH_OK){fail("Owned malformed HCI frame");return;}
 ++stats_.hci_packets;stats_.hci_bytes+=uint32_t(n);
 if(type==BTH_H4_EVENT){
  // HNC is exempt from normal command flow control and has no queue owner.
  // Its exceptional error completion must not overwrite ordinary command
  // credits or pass silently through the unrelated-command completion path.
  const auto p=event.event.params;
  const bool hnc_cc=event.event.code==0x0e&&p.len>=3&&bth_read_le16(p.data+1)==0x0c35;
  const bool hnc_cs=event.event.code==0x0f&&p.len>=4&&bth_read_le16(p.data+2)==0x0c35;
  if(hnc_cc||hnc_cs){
   const unsigned status=hnc_cc?(p.len>=4?p.data[3]:0xff):p.data[0];
   if(p.len==4&&!status)return; // Tolerate a success report without granting credits.
   char line[128];
   snprintf(line,sizeof line,"Owned HNC completion ms=%lu event=%02x op=0c35 status=%02x n=%u malformed=%u grant_seen=%u credit_kept=%u",(unsigned long)now_,unsigned(event.event.code),status,unsigned(p.len),unsigned(p.len!=4),unsigned(p.data[hnc_cc?0:1]),unsigned(flow_.command_credits));
   fail(line);return;
  }
  if(boot_==Boot::Ready&&!leCommandPreflight(commands_,event)){le_.malformedEvent();return;}
  if(bth_hci_flow_on_event(&flow_,&event)<0){fail("Owned HCI credit inconsistency");return;}
  if(event.event.code==0x10){fail("Owned controller hardware error");return;}
  if(boot_!=Boot::Ready){bootEvent(event);return;}
  // Credit-bearing completions may unblock an older retained security event.
  // Other events stay FIFO behind it so disconnect cannot overtake an SDU owner.
  if(event.event.code==0x13)return;
  if(deferred_count_&&!independentCompletion(event))defer(packet,n);else if(!dispatch(event))defer(packet,n);
 }else if(type==BTH_H4_ACL){
  if(!stats_.controller_ready){fail("Owned ACL before controller readiness");return;}
  unsigned i=2;for(unsigned k=0;k<2;++k)if(peers_[k].attached&&peers_[k].handle==event.acl.handle)i=k;
  if(i==2&&!deferred_count_){++stats_.dropped_packets;return;}
  unsigned uncharged=0;for(const auto& p:packets_)if(p.used&&!p.charged)++uncharged;
  if(rx_.outstanding+uncharged>=RxWindow){receiveWindowReceipt(event.acl,uncharged);fail("Owned controller exceeded advertised receive window");return;}
  unsigned slot=RxSlots;for(unsigned k=0;k<RxSlots;++k)if(!packets_[k].used&&!rx_.slots[k].busy){slot=k;break;}
  if(slot==RxSlots||n>AclBytes+4){fail("Owned physical ACL receive storage exhausted");return;}
  auto& owned=packets_[slot];owned.charged=false;owned.barrier=deferred_count_?event_serial_:0;
  if(!owned.barrier){if(bth_rx_flow_received(&rx_,uint8_t(slot),event.acl.handle,peers_[i].owner,&owned.lease)!=BTH_OK){fail("Owned controller exceeded advertised receive window");return;}owned.charged=true;}
  memcpy(owned.bytes,packet,n);owned.length=uint16_t(n);owned.order=++packet_order_;owned.used=true;
 }
}
bool OwnedHost::deliver(unsigned i,const bth_l2cap_pdu_t& pdu,uint16_t* signal_cursor){
 if(i==1)return le_.receive(peers_[i].owner,pdu.cid,pdu.payload.data,pdu.payload.len,now_);
 const bool secure=bth_links_secure(&links_,peers_[0].handle);
 if(pdu.cid!=1){const auto* channel=bth_l2cap_sig_find(&signaling_,pdu.cid);if(!channel||channel->state!=BTH_CHANNEL_OPEN)return true;
  if(pdu.payload.len>channel->local_mtu){closePeer(0,0x13);return true;}return classic_.receive(pdu.cid,pdu.payload.data,pdu.payload.len,secure,now_);
 }
 // Only a wholly validated, stateless bundle can commit a response prefix.
 // The high bit records validation even when no first response could queue.
 // Mutable/mixed bundles retain the original all-or-nothing parser below.
 constexpr uint16_t Split=0x8000;
 if(signal_cursor&&((*signal_cursor&Split)||expandedClassicControl(pdu))){
  *signal_cursor|=Split;size_t at=*signal_cursor&~Split;
  if(at>pdu.payload.len){fail("Owned expanded signaling cursor invariant");return true;}
  while(at<pdu.payload.len&&expanded_control_budget_){
   bth_control_reservation_t slot;if(bth_control_tx_reserve(&control_,0,&slot)!=BTH_OK)return false;
   const size_t bytes=4u+bth_read_le16(pdu.payload.data+at+2);size_t n=0;bool collision=false;
   const auto result=bth_avrcp_link_receive(&signaling_,pdu.payload.data+at,bytes,secure,avrcp_recovering_,slot.data,slot.capacity,&n,&collision);
   if(result!=BTH_OK||!n||collision||bth_control_tx_commit(&control_,slot.token,1,n,BTH_SIGNAL_MTU)!=BTH_OK){
    bth_control_tx_cancel(&control_,slot.token);fail("Owned expanded stateless signaling invariant");return true;
   }
   at+=bytes;*signal_cursor=uint16_t(Split|at);--expanded_control_budget_;
  }
  if(at<pdu.payload.len)return false;*signal_cursor=0;return true;
 }
 bth_control_reservation_t reservation;if(bth_control_tx_reserve(&control_,0,&reservation)!=BTH_OK)return false;
 size_t n=0;bool collision=false;bth_status_t result=bth_avrcp_link_receive(&signaling_,pdu.payload.data,pdu.payload.len,secure,avrcp_recovering_,reservation.data,reservation.capacity,&n,&collision);
 if(result==BTH_ERR_CAPACITY){bth_control_tx_cancel(&control_,reservation.token);return false;}
 if(result<0){bth_control_tx_cancel(&control_,reservation.token);++stats_.dropped_packets;return true;}
 if(n){if(bth_control_tx_commit(&control_,reservation.token,1,n,BTH_SIGNAL_MTU)!=BTH_OK)fail("Owned signaling reservation commit failed");}else bth_control_tx_cancel(&control_,reservation.token);
 if(collision){avrcp_recovering_=true;avrcp_at_=now_+500;}
 synchronizeChannels();return true;
}
bool OwnedHost::safeClassicControl(const bth_l2cap_pdu_t& pdu) const{
 if(!pdu.payload.data||!pdu.payload.len||pdu.payload.len>BTH_CONTROL_BYTES)return false;
 if(pdu.cid!=1){const auto* c=bth_l2cap_sig_find(&signaling_,pdu.cid);
  return c&&c->state==BTH_CHANNEL_OPEN&&pdu.payload.len<=c->local_mtu&&(c->psm==1||(c->psm==0x17&&pdu.cid==classic_.avrcpCid()));
 }
 // A whole bundle is independent only if every command is stateless. A later
 // disconnect/configuration command must not hide behind an initial Echo.
 for(size_t at=0;at<pdu.payload.len;){const auto* p=pdu.payload.data+at;const size_t remaining=pdu.payload.len-at;
  if(remaining<4||!p[1])return false;const size_t n=bth_read_le16(p+2);
  if(n>remaining-4||(p[0]!=8&&(p[0]!=10||n!=2)))return false;
  at+=n+4;
 }
 return true;
}
bool OwnedHost::expandedClassicControl(const bth_l2cap_pdu_t& pdu) const{
 if(pdu.cid!=1||!safeClassicControl(pdu))return false;
 size_t response_bytes=0;
 for(size_t at=0;at<pdu.payload.len;){const auto* p=pdu.payload.data+at;const size_t n=bth_read_le16(p+2);
  const uint16_t info=p[0]==10?bth_read_le16(p+4):0;
  response_bytes+=p[0]==8?n+4:(info==2?12u:(info==3?16u:8u));at+=n+4;
 }
 return response_bytes>BTH_CONTROL_BYTES;
}
void OwnedHost::clearClassicControls(uint16_t cid){
 if(!cid){for(auto& p:classic_deferred_)p={};classic_deferred_read_=classic_deferred_count_=0;classic_deferred_owner_=0;return;}
 auto& peer=peers_[0];if(peer.has_pending&&peer.pending.cid==cid){
  if(peer.borrowed_slot>=0){auto& p=packets_[peer.borrowed_slot];bth_rx_flow_release(&rx_,p.lease);p.used=false;}
  peer.has_pending=false;peer.borrowed_slot=-1;peer.pending={};peer.pending_deadline=0;peer.pending_signal_cursor=0;
 }
 unsigned kept=0;const unsigned count=classic_deferred_count_;
 for(unsigned k=0;k<count;++k){const unsigned from=(classic_deferred_read_+k)%ClassicDeferredSlots;
  if(classic_deferred_[from].cid==cid)continue;const unsigned to=(classic_deferred_read_+kept++)%ClassicDeferredSlots;
  if(to!=from){classic_deferred_[to]=classic_deferred_[from];memmove(peers_[0].bytes+ClassicAssemblyBytes+to*BTH_CONTROL_BYTES,peers_[0].bytes+ClassicAssemblyBytes+from*BTH_CONTROL_BYTES,classic_deferred_[to].length);}
 }
 classic_deferred_count_=uint8_t(kept);if(!kept)clearClassicControls();
}
bool OwnedHost::deferClassicControl(const bth_l2cap_pdu_t& pdu,uint32_t deadline,uint16_t signal_cursor){
 if(classic_deferred_count_==ClassicDeferredSlots||!safeClassicControl(pdu))return false;
 auto& peer=peers_[0];if(!peer.attached||(classic_deferred_count_&&classic_deferred_owner_!=peer.owner))return false;
 const unsigned slot=(classic_deferred_read_+classic_deferred_count_)%ClassicDeferredSlots;
 memcpy(peer.bytes+ClassicAssemblyBytes+slot*BTH_CONTROL_BYTES,pdu.payload.data,pdu.payload.len);
 classic_deferred_[slot]={pdu.cid,uint16_t(pdu.payload.len),deadline,signal_cursor};classic_deferred_owner_=peer.owner;++classic_deferred_count_;return true;
}
bool OwnedHost::retryClassicControls(){
 auto& peer=peers_[0];if(!classic_deferred_count_)return true;
 if(!peer.attached||classic_deferred_owner_!=peer.owner){clearClassicControls();return false;}
 const auto* link=bth_links_find(&links_,peer.handle);if(!link||link->disconnecting){clearClassicControls();return false;}
 for(unsigned k=0;k<ClassicDeferredSlots&&classic_deferred_count_;++k){const unsigned slot=classic_deferred_read_;const auto p=classic_deferred_[slot];
  // Occupancy, not a nonzero timestamp, makes this deadline valid at wrap.
  if(int32_t(now_-p.deadline)>=0){closePeer(0,0x13);return false;}
  const bth_l2cap_pdu_t pdu={peer.handle,p.cid,{peer.bytes+ClassicAssemblyBytes+slot*BTH_CONTROL_BYTES,p.length},false};
  if(!deliver(0,pdu,&classic_deferred_[slot].signal_cursor))break;if(!active_||!peer.attached||link->disconnecting)return false;
  classic_deferred_[slot]={};classic_deferred_read_=uint8_t((slot+1)%ClassicDeferredSlots);--classic_deferred_count_;
 }
 if(!classic_deferred_count_)clearClassicControls();return true;
}
void OwnedHost::processRx(unsigned i){
 auto& peer=peers_[i];if(!peer.attached)return;
 if(i==1&&le_.pendingAttExpired(now_))return;
 // Verified link security alone cannot admit media before output preparation.
 // A full control queue can delay the securityReady/reopen transaction.
 if(i==0&&bth_links_secure(&links_,peer.handle)&&!was_secure_)return;
 if(i==0){const auto* link=bth_links_find(&links_,peer.handle);if(!link||link->disconnecting||!retryClassicControls())return;}
 if(peer.has_pending){
  if(i==0&&int32_t(now_-peer.pending_deadline)>=0){closePeer(i,0x13);return;}
  bool consumed=false;
  if(i==0&&classic_deferred_count_)consumed=deferClassicControl(peer.pending,peer.pending_deadline,peer.pending_signal_cursor);
  else {consumed=deliver(i,peer.pending,&peer.pending_signal_cursor);if(!consumed&&i==0)consumed=deferClassicControl(peer.pending,peer.pending_deadline,peer.pending_signal_cursor);}
  if(!consumed){if(due(now_,peer.pending_deadline))closePeer(i,0x13);return;}
  if(peer.borrowed_slot>=0){auto& packet=packets_[peer.borrowed_slot];bth_rx_flow_release(&rx_,packet.lease);packet.used=false;}
  peer.has_pending=false;peer.borrowed_slot=-1;peer.pending_signal_cursor=0;
 }
 for(unsigned limit=0;limit<RxWindow&&active_&&peer.attached;++limit){
  int index=-1;for(unsigned k=0;k<RxSlots;++k)if(packets_[k].used&&packets_[k].charged&&rx_.slots[k].owner==peer.owner&&(index<0||int32_t(packets_[k].order-packets_[index].order)<0))index=int(k);
  if(index<0)break;auto& packet=packets_[index];bth_acl_view_t acl={};acl.handle=bth_read_le16(packet.bytes)&0x0fff;acl.pb=uint8_t((packet.bytes[1]>>4)&3);acl.bc=uint8_t(packet.bytes[1]>>6);acl.payload={packet.bytes+4,size_t(packet.length)-4};
  bth_l2cap_pdu_t pdu={};bth_status_t result=bth_l2cap_rx_feed(&peer.assembly,&acl,&pdu);
  if(result==BTH_MORE)peer.assembly_deadline=now_+2000;else peer.assembly_deadline=0;
  const uint32_t deadline=now_+5000;bool consumed=true;uint16_t signal_cursor=0;
  if(result==BTH_OK){
   if(i==0&&classic_deferred_count_&&pdu.cid!=classic_.mediaCid())consumed=deferClassicControl(pdu,deadline,signal_cursor);
   else {consumed=deliver(i,pdu,&signal_cursor);if(!consumed&&i==0)consumed=deferClassicControl(pdu,deadline,signal_cursor);}
  }
  if(result==BTH_OK&&!consumed){peer.pending=pdu;peer.pending_signal_cursor=signal_cursor;peer.has_pending=true;peer.pending_deadline=deadline;peer.borrowed_slot=pdu.reassembled?-1:index;
   if(pdu.reassembled){bth_rx_flow_release(&rx_,packet.lease);packet.used=false;}break;
  }
  bth_rx_flow_release(&rx_,packet.lease);packet.used=false;if(result<0){++stats_.dropped_packets;closePeer(i,0x13);break;}
 }
 if(due(now_,peer.assembly_deadline)){++stats_.dropped_packets;closePeer(i,0x13);}
}
void OwnedHost::synchronizeChannels(){
 if(!peers_[0].attached)return;auto& peer=peers_[0];
 if(classic_.abortTransportPending()){
  if(deferred_count_)return; // Never retire owners or issue work ahead of a retained event.
  bool pending=false;
  for(const auto& c:signaling_.channels)if(c.psm==0x19&&c.local_cid!=classic_.signalCid()&&c.state!=BTH_CHANNEL_CLOSED){pending=true;disconnectChannel(c.local_cid);}
  if(!pending)classic_.abortTransportRetired();
 }
 for(unsigned k=0;k<BTH_CHANNEL_LIMIT;++k){auto& c=signaling_.channels[k];
  if(peer.opened[k]&&c.state!=BTH_CHANNEL_OPEN){clearClassicControls(uint16_t(0x40+k));classic_.channelClosed(uint16_t(0x40+k));peer.opened[k]=false;}
  if(c.state==BTH_CHANNEL_OPEN&&peer.opened[k]&&(c.psm==1||bth_links_secure(&links_,peer.handle))&&
     !classic_.channelMtuUpdated(peer.owner,c.psm,c.local_cid,c.remote_cid,c.remote_mtu)){
   closePeer(0,0x13);return;
  }
  if(c.state==BTH_CHANNEL_OPEN&&!peer.opened[k]&&(c.psm==1||bth_links_secure(&links_,peer.handle))){
   if(classic_.channelOpened(c.psm,c.local_cid,c.remote_cid,c.remote_mtu)){peer.opened[k]=true;peer.channel_deadlines[k]=0;}
   else disconnectChannel(c.local_cid);
  }else if(c.state==BTH_CHANNEL_CLOSED){if(peer.opened[k])classic_.channelClosed(uint16_t(0x40+k));peer.opened[k]=false;peer.channel_deadlines[k]=0;}
  else if(c.state!=BTH_CHANNEL_OPEN){if(!peer.channel_deadlines[k])peer.channel_deadlines[k]=now_+10000;if(due(now_,peer.channel_deadlines[k]))closePeer(0,0x13);}
 }
}
bool OwnedHost::disconnectChannel(uint16_t cid){
 if(!cid||!peers_[0].attached)return true;bth_control_reservation_t slot;if(bth_control_tx_reserve(&control_,0,&slot)!=BTH_OK)return false;
 size_t n=0;const auto st=bth_l2cap_sig_cancel(&signaling_,cid,slot.data,slot.capacity,&n);
 if(st==BTH_OK&&n)bth_control_tx_commit(&control_,slot.token,1,n,BTH_SIGNAL_MTU);else bth_control_tx_cancel(&control_,slot.token);return st==BTH_OK||st==BTH_MORE||st==BTH_IGNORED;
}
void OwnedHost::chargeWaiting(uint32_t barrier){
 for(unsigned k=0;k<RxSlots;++k){auto& packet=packets_[k];if(!packet.used||packet.charged||int32_t(packet.barrier-barrier)>0)continue;
  const uint16_t handle=bth_read_le16(packet.bytes)&0xfff;unsigned i=2;for(unsigned j=0;j<2;++j)if(peers_[j].attached&&peers_[j].handle==handle)i=j;
  if(i==2){packet.used=false;++stats_.dropped_packets;continue;}
  if(bth_rx_flow_received(&rx_,uint8_t(k),handle,peers_[i].owner,&packet.lease)!=BTH_OK){fail("Owned retained ACL admission failed");return;}packet.charged=true;
 }
}
void OwnedHost::avrcpPoll(bool secure,bool allow_open){
 if(!secure||!classic_.signalCid()||(!config_.callbacks.volume&&!config_.callbacks.metadata))return;
 if(avrcp_recovering_){
  if(bth_avrcp_link_count(&signaling_)){bth_control_reservation_t slot;if(bth_control_tx_reserve(&control_,0,&slot)!=BTH_OK)return;size_t n=0;
   bth_status_t result=bth_avrcp_link_cancel(&signaling_,slot.data,slot.capacity,&n);
   if(result==BTH_OK&&n)bth_control_tx_commit(&control_,slot.token,1,n,BTH_SIGNAL_MTU);else bth_control_tx_cancel(&control_,slot.token);
   synchronizeChannels();return;
  }
  if(!due(now_,avrcp_at_))return;avrcp_recovering_=false;
 }
 if(!allow_open||bth_avrcp_link_count(&signaling_)||avrcp_attempts_>=3)return;
 if(!avrcp_at_){avrcp_at_=now_+500;return;}if(!due(now_,avrcp_at_))return;
 bth_control_reservation_t slot;if(bth_control_tx_reserve(&control_,0,&slot)!=BTH_OK)return;size_t n=0;uint16_t cid=0;
 bth_status_t result=bth_l2cap_sig_connect(&signaling_,0x17,true,slot.data,slot.capacity,&n,&cid);
 if(result==BTH_OK&&n){bth_control_tx_commit(&control_,slot.token,1,n,BTH_SIGNAL_MTU);++avrcp_attempts_;avrcp_at_=now_+1000;synchronizeChannels();}
 else bth_control_tx_cancel(&control_,slot.token);
}
bool OwnedHost::updateAudioPriority(){
 const bool enabled=config_.ble.audio_priority && !deferred_count_ && was_secure_ && peers_[0].attached && bth_links_secure(&links_,peers_[0].handle) && classic_.streaming();
 if(!enabled || !qos_audio_active_ || qos_classic_owner_!=peers_[0].owner || qos_le_owner_!=peers_[1].owner){
  qos_last_data_ms_=0;qos_have_data_=false;qos_classic_burst_=0;
 }
 qos_audio_active_=enabled;qos_classic_owner_=peers_[0].owner;qos_le_owner_=peers_[1].owner;return enabled;
}
void OwnedHost::runtimePoll(){
 expanded_control_budget_=BTH_CONTROL_PEER_LIMIT; // At most three split replies per owner pass.
 while(deferred_count_&&active_){auto& d=deferred_[deferred_read_];bth_h4_frame_t event={};uint8_t wire[258];wire[0]=4;memcpy(wire+1,d.bytes,d.length);
  if(bth_h4_decode(wire,d.length+1,&event)!=BTH_OK){fail("Owned retained event corrupt");return;}if(!dispatch(event))break;if(!active_)return;retired_event_=d.serial;chargeWaiting(retired_event_);if(!active_)return;deferred_read_=(deferred_read_+1)%8;--deferred_count_;
 }
 if(!active_)return;if(due(now_,accept_deadline_)){fail("Owned accepted connection timeout");return;}if(gap_.pending&&due(now_,deadline_)){fail("Owned scan command ACK timeout");return;}for(unsigned k=0;k<commands_.sent_count;++k)if(uint32_t(now_-sent_at_[k])>=5000){fail("Owned peer command completion timeout");return;}if(bth_cmdq_available(&commands_)>=2)bth_links_security_tick(&links_,now_);
  // Ordered events gate new profile work, not verified Classic security.
 const bool secure=peers_[0].attached&&bth_links_secure(&links_,peers_[0].handle);
 const bool classic_ready=!deferred_count_&&secure;
 if(peers_[0].attached){
  if(!secure&&was_secure_){peers_[0].security_deadline=now_+35000;if(config_.callbacks.stream)config_.callbacks.stream(false,config_.callbacks.user);was_secure_=false;}
  if(classic_ready&&!was_secure_){bth_control_reservation_t slot;if(bth_control_tx_reserve(&control_,0,&slot)==BTH_OK){size_t n=0;const auto result=bth_l2cap_sig_security_ready(&signaling_,slot.data,slot.capacity,&n);
    if(result==BTH_OK&&n)bth_control_tx_commit(&control_,slot.token,1,n,BTH_SIGNAL_MTU);else bth_control_tx_cancel(&control_,slot.token);if(result!=BTH_ERR_CAPACITY){
     if(classic_.streaming()&&classic_.configured()){Status prepared=config_.callbacks.configure(classic_.acceptedConfiguration(),config_.callbacks.user);if(prepared==Status::Ok){if(config_.callbacks.stream)config_.callbacks.stream(true,config_.callbacks.user);}else{closePeer(0,5);return;}}
     was_secure_=true;
    }}}
  if(!secure&&due(now_,peers_[0].security_deadline))closePeer(0,5);
  const bool input_waiting=classic_deferred_count_||peers_[0].has_pending;
  if(classic_ready)classic_.poll(true,now_,!input_waiting);synchronizeChannels();avrcpPoll(classic_ready,!classic_deferred_count_&&!peers_[0].has_pending);
  if(classic_.takeCloseMedia())close_media_=classic_.mediaCid();if(classic_.takeCloseAvrcp())close_avrcp_=classic_.avrcpCid();
  if(close_media_&&disconnectChannel(close_media_))close_media_=0;if(close_avrcp_&&disconnectChannel(close_avrcp_))close_avrcp_=0;
 }
 const bool audio_priority=updateAudioPriority();
 if(audio_priority && !deferred_count_){processRx(0);if(!active_)return;}
 if(!deferred_count_&&peers_[1].attached){const auto* link=bth_links_find(&links_,peers_[1].handle);
  if(link&&!link->disconnecting){if(le_.pendingAttExpired(now_))closePeer(1,0x13);else le_.servicePendingAtt(peers_[1].owner,now_);}}
 if(!deferred_count_){
  if(audio_priority){processRx(1);if(!active_)return;}
  else for(unsigned k=0;k<2;++k){unsigned i=(next_peer_+k)%2;processRx(i);if(!active_)return;}
 }
 next_peer_^=1;
 for(unsigned i=0;i<2;++i)if(peers_[i].attached){const auto* link=bth_links_find(&links_,peers_[i].handle);
  if(link&&link->disconnecting&&!peers_[i].disconnect_deadline)peers_[i].disconnect_deadline=now_+5000;
  if(due(now_,peers_[i].disconnect_deadline)){fail("Owned disconnect completion timeout");return;}}
 if(links_.classic.state==BTH_PEER_EMPTY&&!reserved_[0])admission(0,0);
 if(config_.ble.enabled&&links_.le.state==BTH_PEER_EMPTY&&!reserved_[1])admission(1,0);
  le_.poll(now_,reserved_[1]&&!le_advertising_held_,updateAudioPriority());
  if(le_.closeRequested())closePeer(1,0x13);
 if(le_.stats().status==Status::HardwareError){const auto state=le_.controlState();char line[128];snprintf(line,sizeof line,"Owned LE failure op=%04x stage=%u error=%02x pending=%u now=%lu deadline=%lu",state.opcode,state.stage,state.error,state.pending,(unsigned long)now_,(unsigned long)state.deadline);diagnose(line);fail("Owned LE profile control failure");return;}
 if(!hooks_.txReady(hooks_.user))return;
 size_t n=0;
 if((!last_ack_||rx_.outstanding>=RxWindow-1)&&bth_rx_flow_next_completed(&rx_,output_,sizeof output_,&n)==BTH_OK){last_ack_=true;submit(n);return;}
 const uint32_t owners[]={peers_[0].owner,peers_[1].owner};
 const bool prioritize=updateAudioPriority();
 const bool le_control_pending=peers_[1].has_pending && (peers_[1].pending.cid==5 || peers_[1].pending.cid==6);
 const bth_control_tx_policy_t policy={uint8_t(qos_classic_burst_>=3?1:0),!qos_have_data_||uint32_t(now_-qos_last_data_ms_)>=config_.ble.audio_data_interval_ms,config_.ble.audio_acl_fragment_bytes,le_control_pending};
 for(unsigned k=0;k<2;++k){bool command=(k==0)?tx_turn_:!tx_turn_;unsigned old_sent=commands_.sent_count;bth_control_tx_info_t info={};
   const bool optional_signal=!command&&prioritize&&le_.prepareOptionalSignal(now_,prioritize,owners,flow_,policy.first_peer);
  bth_status_t result=command?bth_cmdq_next(&commands_,&flow_,output_,sizeof output_,&n):bth_control_tx_next_policy(&control_,owners,&flow_,prioritize?&policy:nullptr,&info,output_,sizeof output_,&n);
   if(optional_signal && (result!=BTH_OK || info.peer!=1)){fail("Owned optional LE send admission invariant");return;}
  if(prioritize&&!command&&info.paced)++qos_paced_deferrals_;
  if(result==BTH_OK){
   if(command&&n>=4&&output_[2]==0x20&&le_trace_<32){char line[96];snprintf(line,sizeof line,"Owned LE HCI TX op=%04x n=%u credit=%u",bth_read_le16(output_+1),unsigned(n),flow_.command_credits);diagnose(line);++le_trace_;}
   tx_turn_=!command;last_ack_=false;if(command&&commands_.sent_count>old_sent)sent_at_[commands_.sent_count-1]=now_;
   const uint32_t submitted_owner=command&&commands_.sent_count>old_sent?commands_.sent[commands_.sent_count-1].owner:0;
   const bool submitted=submit(n);
   if(submitted&&command)le_.commandSubmitted(submitted_owner,bth_read_le16(output_+1),output_+4,output_[3]);
   if(submitted&&prioritize&&!command){
    if(info.peer==0){if(qos_classic_burst_<3)++qos_classic_burst_;}
    else {qos_classic_burst_=0;if(info.le_data){qos_last_data_ms_=now_;qos_have_data_=true;++qos_data_fragments_;qos_data_bytes_+=info.payload_bytes;}
     if(info.urgent_bypass)++qos_urgent_bypasses_;}
   }return;}
  if(result<0&&result!=BTH_ERR_CAPACITY){fail("Owned transmit queue invariant failed");return;}}
 if(bth_rx_flow_next_completed(&rx_,output_,sizeof output_,&n)==BTH_OK){last_ack_=true;submit(n);return;}
 const uint8_t scan=uint8_t(reserved_[0]&&links_.classic.state==BTH_PEER_EMPTY?3:1);
 if(!gap_.pending&&gap_.scan!=scan){const auto result=bth_gap_set_scan(&gap_,&flow_,true,scan,output_,sizeof output_,&n);if(result==BTH_OK){deadline_=now_+3000;last_ack_=false;submit(n);return;}if(result<0){fail("Owned scan admission failed");return;}}
 if(gap_.pending&&due(now_,deadline_)){fail("Owned scan command ACK timeout");return;}

}
void OwnedHost::refreshStats(){stats_.connected=peers_[0].attached;stats_.streaming=!deferred_count_&&was_secure_&&peers_[0].attached&&bth_links_secure(&links_,peers_[0].handle)&&classic_.streaming();stats_.received_packets=classic_.receivedPackets();stats_.received_bytes=classic_.receivedBytes();}
RadioStats OwnedHost::radioStats()const {RadioStats s=stats_;s.connected=peers_[0].attached;s.streaming=!deferred_count_&&was_secure_&&peers_[0].attached&&bth_links_secure(&links_,peers_[0].handle)&&classic_.streaming();s.received_packets=classic_.receivedPackets();s.received_bytes=classic_.receivedBytes();s.dropped_packets+=classic_.droppedPackets();return s;}
void OwnedHost::serviceReceiveCredits(uint32_t now){
 if(!active_||boot_!=Boot::Ready||deferred_count_||!hooks_.txReady(hooks_.user))return;
 size_t n=0;if(bth_rx_flow_next_completed(&rx_,output_,sizeof output_,&n)!=BTH_OK)return;
 now_=now;last_ack_=true;submit(n);
}
void OwnedHost::poll(uint32_t now){if(!active_)return;now_=now;if(!clock_started_){begin_ms_=now;clock_started_=true;}
 if(boot_!=Boot::Ready){if(uint32_t(now_-begin_ms_)>60000){fail("Owned controller startup timeout");return;}bootPoll();}else runtimePoll();refreshStats();}
}}
