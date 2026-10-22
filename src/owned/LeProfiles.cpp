#include "LeProfiles.h"
#include <string.h>
namespace giga_bredr { namespace detail {
namespace {
#define BLE_UUID(n) 0xd8,0x65,0x3e,0xd2,0x0a,0x03,0x9c,0x91,0xaf,0x4a,0x49,0x9c,(n),0x00,0x77,0x9f
const uint8_t primary[]={0,0x28},declaration[]={3,0x28},cccd[]={2,0x29};
const uint8_t gapService[]={0,0x18},gattService[]={1,0x18},service[]={BLE_UUID(1)};
const uint8_t nameType[]={0,0x2a},appearanceType[]={1,0x2a};
const uint8_t rxType[]={BLE_UUID(2)},txType[]={BLE_UUID(3)},descriptionType[]={1,0x29};
const uint8_t nameDeclaration[]={2,3,0,0,0x2a},appearanceDeclaration[]={2,5,0,1,0x2a};
const uint8_t rxDeclaration[]={0x0c,9,0,BLE_UUID(2)},txDeclaration[]={0x12,11,0,BLE_UUID(3)};
const uint8_t appearance[]={0,0},description[]={'G','I','G','A',' ','d','a','t','a'};
#undef BLE_UUID
template<size_t N> bth_span_t span(const uint8_t (&data)[N]) { return {data,N}; }
bth_att_attribute_t attribute(uint16_t handle,bth_span_t type,bth_span_t value) {
    bth_att_attribute_t a={}; a.handle=handle; a.uuid=type; a.value=value; a.permissions=BTH_ATT_READ; return a;
}
}
bool LeProfiles::initDatabase() {
    attributes_[0]=attribute(1,span(primary),span(gapService)); attributes_[0].group_end=5;
    attributes_[1]=attribute(2,span(declaration),span(nameDeclaration));
    attributes_[2]=attribute(3,span(nameType),{reinterpret_cast<const uint8_t*>(name_),strlen(name_)});
    attributes_[2].maximum_length=63;
    attributes_[3]=attribute(4,span(declaration),span(appearanceDeclaration));
    attributes_[4]=attribute(5,span(appearanceType),span(appearance));
    attributes_[5]=attribute(6,span(primary),span(gattService)); attributes_[5].group_end=6;
    attributes_[6]=attribute(7,span(primary),span(service)); attributes_[6].group_end=13;
    attributes_[7]=attribute(8,span(declaration),span(rxDeclaration));
    attributes_[8]=attribute(RxHandle,span(rxType),{nullptr,0});
    attributes_[8].kind=BTH_ATT_APPLICATION;
    attributes_[8].permissions=BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND;
    attributes_[8].maximum_length=BleValueCapacity;
    attributes_[9]=attribute(10,span(declaration),span(txDeclaration));
    attributes_[10]=attribute(TxHandle,span(txType),{tx_value_,tx_length_});
    attributes_[10].kind=BTH_ATT_APPLICATION; attributes_[10].maximum_length=BleValueCapacity;
    attributes_[11]=attribute(TxCccdHandle,span(cccd),{nullptr,0});
    attributes_[11].kind=BTH_ATT_CCCD; attributes_[11].related_handle=TxHandle;
    attributes_[11].permissions=BTH_ATT_READ|BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND;
    attributes_[11].maximum_length=2; attributes_[11].cccd_mask=1;
    attributes_[12]=attribute(13,span(descriptionType),span(description));
    return bth_att_init(&att_,attributes_,13,config_.maximum_mtu);
}
Status LeProfiles::begin(const BleConfig& config,const char* name,bth_cmdq_t& commands,bth_control_tx_t& replies) {
    if (initialized_) return Status::Busy;
    if (config.security==BleSecurityPolicy::EncryptedPairing) return Status::Unsupported;
    if (!name || !name[0] || !commands.initialized || !replies.initialized ||
        config.maximum_mtu<23 || config.maximum_mtu>517 ||
        config.audio_data_interval_ms<1 || config.audio_data_interval_ms>100 ||
        config.audio_acl_fragment_bytes<4 || config.audio_acl_fragment_bytes>251 ||
        config.advertising_interval_min<0x20 || config.advertising_interval_max>0x4000 ||
        config.advertising_interval_min>config.advertising_interval_max ||
        config.security!=BleSecurityPolicy::PublicUnencrypted) return Status::InvalidArgument;
    const auto& preference=config.audio_connection_preference;
    const bth_le_transport_policy_t policy={preference.interval_min,preference.interval_max,preference.latency,preference.timeout};
    if (!bth_le_transport_policy_valid(&policy)) return Status::InvalidArgument;
    size_t nameLength=0; while (nameLength<sizeof name_ && name[nameLength]) ++nameLength;
    if (nameLength>=sizeof name_) return Status::InvalidArgument;
    config_=config; commands_=&commands; replies_=&replies;
    memcpy(name_,name,nameLength+1);
    stats_=BleStats{}; stats_.enabled=config.enabled;
    if (!initDatabase()) return Status::InvalidArgument;
    advertisement_[0]=2; advertisement_[1]=1; advertisement_[2]=2;
    advertisement_[3]=17; advertisement_[4]=7; memcpy(advertisement_+5,service,16);
    size_t scanName=nameLength<29?nameLength:29;
    if(scanName<nameLength && (uint8_t(name_[scanName])&0xc0)==0x80)
        while(scanName && (uint8_t(name_[scanName])&0xc0)==0x80) --scanName;
    scan_response_[0]=static_cast<uint8_t>(scanName+1);
    scan_response_[1]=nameLength<=29?9:8;
    memcpy(scan_response_+2,name_,scanName); scan_length_=static_cast<uint8_t>(scanName+2);
    const bth_le_gap_config_t gapConfig={{advertisement_,sizeof advertisement_},{scan_response_,scan_length_},
        config.advertising_interval_min,config.advertising_interval_max,false};
    if (!bth_le_gap_start(&gap_,commands_,&gapConfig)) return Status::InvalidArgument;
    initialized_=true; return Status::Ok;
}
bool LeProfiles::startAfterReset(const bth_h4_frame_t& event) {
    if (!initialized_ || !config_.enabled || stats_.connected || gap_.pending || gap_.advertising ||
        gap_.stage!=BTH_LE_STOP || commands_->used || commands_->sent_count ||
        event.type!=BTH_H4_EVENT || event.event.code!=0x0e) return false;
    const bth_span_t p=event.event.params;
    if (!p.data || p.len!=4 || bth_read_le16(p.data+1)!=0x0c03 || p.data[3]) return false;
    // Core Vol4 PartE7.3.2: a completed Reset returns LE to standby/defaults.
    // The sole actor has sent no advertising command since that Reset. Retain
    // the STOP stage for ordinary/unknown-state callers; never ignore0x0c.
    gap_.stage=BTH_LE_PARAMETERS; return true;
}
bool LeProfiles::attach(uint32_t generation,uint16_t handle,uint8_t role,const uint8_t* localFeatures,uint32_t now) {
    if (!initialized_ || !config_.enabled || !generation || handle>0x0eff || role!=1 || stats_.connected ||
        !replies_->peers[1].attached || replies_->peers[1].owner!=generation || replies_->peers[1].handle!=handle) return false;
    if (!initDatabase()) return false;
    const auto& p=config_.audio_connection_preference;
    const bth_le_transport_policy_t policy={p.interval_min,p.interval_max,p.latency,p.timeout};
    if (!bth_le_transport_configure(&transport_,generation,handle,role,localFeatures,&policy)) return false;
    connected_at_=now;
    parameter_read_=parameter_count_=0;parameter_reply_seen_=peer_update_waiting_=close_requested_=false;
    ltk_pending_=parameters_pending_=false;resetConnectionStats();
    stats_.generation=generation; stats_.connection_handle=handle; stats_.att_mtu=23;
    stats_.connected=true; stats_.advertising=false; stats_.notify_enabled=false;
    pending_request_length_=0; pending_response_=false;
    bth_le_gap_connection(&gap_,true);syncTransportStats();return true;
}
void LeProfiles::detach(uint32_t generation) {
    if (!stats_.connected || stats_.generation!=generation) return;
    (void)bth_le_transport_cancel(&transport_,generation);
    parameter_read_=parameter_count_=0;parameter_reply_seen_=peer_update_waiting_=close_requested_=false;
    resetConnectionStats();
    stats_.connected=false; stats_.generation=0; stats_.connection_handle=0;
    stats_.att_mtu=23; stats_.notify_enabled=false; write_pending_=notify_pending_=false;
    ltk_pending_=parameters_pending_=false;
    pending_request_length_=0; pending_response_=false;
    write_=BleWrite{}; bth_le_gap_connection(&gap_,false);syncTransportStats();
}
void LeProfiles::syncTransportStats() {
    auto& s=stats_.connection;
    s.interval=transport_.interval;s.latency=transport_.latency;s.timeout=transport_.timeout;
    s.known=transport_.parameters_known;s.source=transport_.parameters_source;
    s.matches_preference=transport_.matches_preference;s.update_reported=transport_.update_reported;
    s.update_status=transport_.update_reported?transport_.last_update_status:0xff;s.preference_started=transport_.started;
    s.preference_stage=uint8_t(transport_.stage);s.preference_error=transport_.error;
}
void LeProfiles::resetConnectionStats() {
    const auto old=stats_.connection;stats_.connection={};
    auto& s=stats_.connection;s.feature_submissions=old.feature_submissions;s.update_submissions=old.update_submissions;
    s.reply_submissions=old.reply_submissions;s.signaling_requests_queued=old.signaling_requests_queued;s.command_status_reports=old.command_status_reports;
}
void LeProfiles::event(const bth_h4_frame_t& event,uint32_t commandOwner) {
    if (!initialized_ || !config_.enabled) return;
    const bth_status_t gapStatus=bth_le_gap_on_event(&gap_,&event);
    if (gapStatus<0) stats_.status=Status::HardwareError;
    stats_.advertising=gap_.advertising;
    if (!stats_.connected) return;
    const auto transportStatus=bth_le_transport_event(&transport_,&event,commandOwner);
    if (transportStatus<0) ++stats_.malformed_pdus;
    if (transportStatus==BTH_OK && event.type==BTH_H4_EVENT && event.event.code==0x3e && event.event.params.len==10 && event.event.params.data[0]==3 && bth_read_le16(event.event.params.data+2)==stats_.connection_handle)
        peer_update_waiting_=false;
    if (commandOwner==stats_.generation && event.type==BTH_H4_EVENT && event.event.code==0x0e &&
        event.event.params.len>=4 && bth_read_le16(event.event.params.data+1)==0x2020 && event.event.params.data[3]) peer_update_waiting_=false;
    if (commandOwner==stats_.generation && event.type==BTH_H4_EVENT && event.event.code==0x0f && event.event.params.len==4) {
        const auto p=event.event.params;const uint16_t op=bth_read_le16(p.data+2);
        if (op==0x2013 || op==0x2016) {++stats_.connection.command_status_reports;stats_.connection.command_status=p.data[0];}
    }
    syncTransportStats();
    if (event.type!=BTH_H4_EVENT || event.event.code!=0x3e) return;
    const bth_span_t p=event.event.params;
    if (!p.data || !p.len || (p.data[0]!=5 && p.data[0]!=6)) return;
    const size_t expected=p.data[0]==5?sizeof pending_ltk_:sizeof pending_parameters_;
    if (p.len!=expected) { ++stats_.malformed_pdus; return; }
    if (bth_read_le16(p.data+1)!=stats_.connection_handle) return;
    // Copying a same-handle exact request requires no command slot. It must stop
    // optional local work now, even if the mandatory reply cannot yet queue.
    if (transport_.started) (void)bth_le_transport_yield(&transport_,0x23);
    if (p.data[0]==5) { memcpy(pending_ltk_,p.data,p.len); ltk_pending_=true; }
    else if (!parameters_pending_) { memcpy(pending_parameters_,p.data,p.len); parameters_pending_=true; }
    else if (parameter_count_<BTH_CMD_QUEUE_LIMIT-1) {
        memcpy(parameter_queue_[(parameter_read_+parameter_count_)%(BTH_CMD_QUEUE_LIMIT-1)],p.data,p.len);++parameter_count_;
    } else {++stats_.capacity_retries;close_requested_=true;}
    syncTransportStats();
}
void LeProfiles::commandSubmitted(uint32_t owner,uint16_t opcode,const uint8_t* p,size_t n) {
    if (!stats_.connected || owner!=stats_.generation || !p || n<2 || bth_read_le16(p)!=stats_.connection_handle) return;
    auto& s=stats_.connection;
    if (opcode==0x2016 && n==2) ++s.feature_submissions;
    if (opcode==0x2013 && n==14) ++s.update_submissions;
    if ((opcode==0x2020 && n==14) || (opcode==0x2021 && n==3)) ++s.reply_submissions;
    if ((opcode==0x2013 || opcode==0x2020) && n==14) {
        s.submitted_min=bth_read_le16(p+2);s.submitted_max=bth_read_le16(p+4);
        s.submitted_latency=bth_read_le16(p+6);s.submitted_timeout=bth_read_le16(p+8);
    }
}
bool LeProfiles::receive(uint32_t generation,uint16_t cid,const uint8_t* data,size_t length,uint32_t now) {
    if (!initialized_ || !stats_.connected || generation!=stats_.generation) return true;
    if (close_requested_) return true;
    if (!data || !length) { ++stats_.malformed_pdus; return true; }
    if (cid!=4) {
        uint8_t response[6]={}; size_t bytes=0;
        if (cid==6) {
            // No pairing or key negotiation is claimed without a qualified backend.
            if (data[0]!=5) { response[0]=5; response[1]=5; bytes=2; }
            else if (length!=2) ++stats_.malformed_pdus;
        } else if (cid==5) {
            if (length<4 || length>23 || !data[1] || bth_read_le16(data+2)!=length-4) {
                ++stats_.malformed_pdus; return true;
            }
            const auto status=bth_le_transport_signal(&transport_,data,length);
            if (status<0) ++stats_.malformed_pdus;
            syncTransportStats();
            if (data[0]!=1 && data[0]!=0x13 && data[0]!=0x15 && data[0]!=0x18 && data[0]!=0x1a) {
                response[0]=1; response[1]=data[1]; response[2]=2; bytes=6;
            }
        }
        if (bytes) {
            bth_control_reservation_t slot;
            if (bth_control_tx_reserve(replies_,1,&slot)!=BTH_OK) { ++stats_.capacity_retries; return false; }
            memcpy(slot.data,response,bytes);
            if (bth_control_tx_commit(replies_,slot.token,cid,bytes,cid==6?65:23)!=BTH_OK) stats_.status=Status::HardwareError;
        }
        ++stats_.received_pdus; return true;
    }
    const bool request=!(data[0]&0x41) && data[0]!=0x1e;
    // Commands and confirmations remain independent of an older request.
    // A second request cannot overwrite it or mutate prepared-write state.
    if (request && pendingAtt()) return true;
    if (processAtt(generation,data,length)) return true;
    if (!request) return false;
    if (length<=att_.mtu && length<=sizeof pending_request_) {
        memcpy(pending_request_,data,length); pending_response_=false;
        pending_request_length_=static_cast<uint16_t>(length);
    } else {
        // Validate the oversize PDU through the real parser, retaining only its
        // Error Response, never a truncated request that could later execute.
        size_t bytes=0; bth_att_action_t action={};
        const auto status=bth_att_process(&att_,data,length,bth_att_security_t{},pending_request_,5,&bytes,&action);
        if (status!=BTH_OK || bytes!=5 || pending_request_[0]!=1 || action.kind!=BTH_ATT_ACTION_NONE) return false;
        pending_request_length_=5; pending_response_=true;
    }
    pending_request_started_=now;
    return true; // Immutable ATT ownership exists before physical RX retirement.
}
void LeProfiles::servicePendingAtt(uint32_t generation,uint32_t now) {
    if (!initialized_ || !stats_.connected || generation!=stats_.generation ||
        !pendingAtt() || pendingAttExpired(now)) return;
    if (pending_response_) {
        bth_control_reservation_t slot;
        if (bth_control_tx_reserve(replies_,1,&slot)!=BTH_OK) { ++stats_.capacity_retries; return; }
        memcpy(slot.data,pending_request_,pending_request_length_);
        if (bth_control_tx_commit(replies_,slot.token,4,pending_request_length_,att_.mtu)!=BTH_OK) stats_.status=Status::HardwareError;
        ++stats_.received_pdus;
    } else if (!processAtt(generation,pending_request_,pending_request_length_)) return;
    pending_request_length_=0; pending_response_=false;
}
bool LeProfiles::processAtt(uint32_t generation,const uint8_t* data,size_t length) {
    const bool request=!(data[0]&0x41) && data[0]!=0x1e;
    const bool command=data[0]==0x52;
    const bool possibleWrite=((data[0]==0x12 || data[0]==0x52) && length>=3 && bth_read_le16(data+1)==RxHandle) ||
        (data[0]==0x18 && length==2 && data[1]==1 && att_.prepared_handle==RxHandle);
    if (possibleWrite && write_pending_ && !command) { ++stats_.capacity_retries; return false; }
    bth_control_reservation_t reservation={};
    if (request && bth_control_tx_reserve(replies_,1,&reservation)!=BTH_OK) { ++stats_.capacity_retries; return false; }
    size_t written=0; bth_att_action_t action={};
    // Commands are validated by the same ATT processor but never need a reply slot.
    uint8_t command_scratch=0;
    const bth_status_t status=bth_att_process(&att_,data,length,bth_att_security_t{},
        request?reservation.data:&command_scratch,request?reservation.capacity:0,&written,&action);
    if (status==BTH_MORE || status==BTH_ERR_CAPACITY) {
        if (request) bth_control_tx_cancel(replies_,reservation.token);
        ++stats_.capacity_retries; return false;
    }
    ++stats_.received_pdus;
    if (status<0) ++stats_.malformed_pdus;
    if (action.kind==BTH_ATT_ACTION_WRITE) {
        if (command && (write_pending_ || pendingAtt())) ++stats_.write_command_drops;
        else {
            write_.generation=generation; write_.length=static_cast<uint16_t>(action.value.len);
            if (action.value.len) memcpy(write_.data,action.value.data,action.value.len);
            write_pending_=true; ++stats_.writes;
            if (action.prepared) bth_att_execute_release(&att_);
        }
    }
    stats_.att_mtu=att_.mtu; stats_.notify_enabled=(att_.cccd[0]&1)!=0;
    // Unsubscribe cancels only the pending application update, preserving TX reads.
    if (status==BTH_OK && action.kind==BTH_ATT_ACTION_CCCD && action.handle==TxCccdHandle && !stats_.notify_enabled) notify_pending_=false;
    if (request) {
        if (status==BTH_OK && written) {
            if (bth_control_tx_commit(replies_,reservation.token,4,written,att_.mtu)!=BTH_OK) stats_.status=Status::HardwareError;
        } else bth_control_tx_cancel(replies_,reservation.token);
    }
    return true;
}
void LeProfiles::poll(uint32_t now,bool mayAdvertise,bool audioActive) {
    if (!initialized_ || !config_.enabled) return;
    const bool prefer=audioActive && config_.audio_connection_preference.enabled;
    if (stats_.connected) {
        // Clocks and cancellation must run even with fewer than three command
        // slots. Only producers preserve the two-slot security reserve.
        if (!prefer && transport_.started) (void)bth_le_transport_yield(&transport_,0);
        if (transport_.started) (void)bth_le_transport_expire(&transport_,now);
        if (peer_update_waiting_ && uint32_t(now-parameter_reply_at_)>=40000) {peer_update_waiting_=false;parameter_reply_at_=now;}
        if (close_requested_) {syncTransportStats();return;}
    }
    if (stats_.connected) {
        for (unsigned i=0;i<2;++i) {
            if (bth_cmdq_available(commands_)<3) break;
            bool& pending=i?parameters_pending_:ltk_pending_;
            if (!pending) continue;
            bth_h4_frame_t peerEvent={}; peerEvent.type=BTH_H4_EVENT;
            peerEvent.event={0x3e,{i?pending_parameters_:pending_ltk_,i?sizeof pending_parameters_:sizeof pending_ltk_}};
            bth_status_t result;
            if (!i) result=bth_le_gap_peer_event(&gap_,stats_.generation,stats_.connection_handle,&peerEvent);
            else {
                uint8_t reply[14];size_t written=0;uint16_t opcode=0;
                result=bth_le_transport_peer_reply(&transport_,stats_.generation,&peerEvent,
                    audioActive && config_.audio_connection_preference.enabled,reply,sizeof reply,&written,&opcode);
                if (result==BTH_OK) result=bth_cmdq_push_owned(commands_,opcode,reply,written,true,stats_.generation);
                if (result==BTH_OK) {parameter_reply_seen_=true;parameter_reply_at_=now;peer_update_waiting_=opcode==0x2020;}
            }
            if (result==BTH_ERR_CAPACITY) break;
            if (i && parameter_count_) {
                memcpy(pending_parameters_,parameter_queue_[parameter_read_],sizeof pending_parameters_);
                parameter_read_=(parameter_read_+1)%(BTH_CMD_QUEUE_LIMIT-1);--parameter_count_;
            } else pending=false;
            if (result<0) {if(!i)stats_.status=Status::HardwareError;else ++stats_.malformed_pdus;}
        }
    }
    if (bth_le_gap_expire(&gap_,now)<0 ||
        (bth_cmdq_available(commands_)>=3 && bth_le_gap_next(&gap_,mayAdvertise && !stats_.connected,now)<0))
        stats_.status=Status::HardwareError;
    stats_.advertising=gap_.advertising;
    if (stats_.connected) {
        if (prefer && !transport_.started && !parameters_pending_ && !ltk_pending_ &&
            (!parameter_reply_seen_ || (!peer_update_waiting_ && uint32_t(now-parameter_reply_at_)>=5000)) &&
            bth_cmdq_available(commands_)>=3 && uint32_t(now-connected_at_)>=5000)
            (void)bth_le_transport_start(&transport_,now);
        if (transport_.started && transport_.stage!=BTH_LE_TRANSPORT_SIGNAL && bth_cmdq_available(commands_)>=3) {
                uint8_t unused[16];size_t written=0;
                const auto status=bth_le_transport_next(&transport_,commands_,now,unused,sizeof unused,&written);
                if (status<0 && status!=BTH_ERR_CAPACITY) (void)bth_le_transport_yield(&transport_,0xfe);
        }
        syncTransportStats();
    }
    if (!stats_.connected || !notify_pending_) return;
    bth_control_reservation_t slot;
    if (bth_control_tx_reserve(replies_,1,&slot)!=BTH_OK) return;
    size_t written=0;
    const bth_status_t status=bth_att_notify(&att_,TxHandle,TxCccdHandle,tx_value_,tx_length_,false,
        bth_att_security_t{},slot.data,slot.capacity,&written);
    if (status==BTH_OK && written) {
        if (bth_control_tx_commit(replies_,slot.token,4,written,att_.mtu)!=BTH_OK) stats_.status=Status::HardwareError;
        else ++stats_.notifications_queued;
    } else bth_control_tx_cancel(replies_,slot.token);
    notify_pending_=false;
}
bool LeProfiles::prepareOptionalSignal(uint32_t now,bool audioActive,const uint32_t owners[2],const bth_hci_flow_t& flow,uint8_t firstPeer) {
    if (!stats_.connected || !audioActive || !config_.audio_connection_preference.enabled ||
        transport_.stage!=BTH_LE_TRANSPORT_SIGNAL || parameters_pending_ || ltk_pending_ || close_requested_ || !owners || firstPeer>1) return false;
    (void)bth_le_transport_expire(&transport_,now);
    if (transport_.stage!=BTH_LE_TRANSPORT_SIGNAL) {syncTransportStats();return false;}
    const auto& le=replies_->peers[1];const auto& classic=replies_->peers[0];
    if (!le.attached || le.owner!=stats_.generation || le.owner!=owners[1] || le.handle!=stats_.connection_handle ||
        le.count || le.owned || le.tx.active) return false;
    const bth_acl_pool_t* pool=nullptr;
    for (const auto& link:flow.links) if (link.active && link.handle==le.handle && link.type==BTH_LINK_LE)
        pool=flow.le.total?&flow.le:&flow.classic;
    if (!pool || !pool->available) return false;
    if (!flow.le.total && pool->total>1 && pool->available<=1) return false;
    if (pool->length<16) {(void)bth_le_transport_yield(&transport_,0xfe);syncTransportStats();return false;}
    if (classic.count && (!classic.attached || classic.owner!=owners[0])) return false;
    if (firstPeer==0 && classic.count && flow.classic.available) return false;
    bth_control_reservation_t slot;
    if (bth_control_tx_reserve(replies_,1,&slot)!=BTH_OK) return false;
    size_t written=0;const auto status=bth_le_transport_next(&transport_,commands_,now,slot.data,slot.capacity,&written);
    if (status!=BTH_OK || written!=12 || bth_control_tx_commit(replies_,slot.token,5,written,23)!=BTH_OK) {
        bth_control_tx_cancel(replies_,slot.token);(void)bth_le_transport_yield(&transport_,0xfe);syncTransportStats();return false;
    }
    ++stats_.connection.signaling_requests_queued;syncTransportStats();return true;
}
Status LeProfiles::notify(const uint8_t* value,size_t length) {
    if (!initialized_ || !config_.enabled) return Status::Unsupported;
    if (close_requested_) return Status::Busy;
    if ((!value && length) || length>BleValueCapacity ||
        (stats_.connected && stats_.notify_enabled && length>att_.mtu-3u)) return Status::InvalidArgument;
    if (notify_pending_) return Status::Busy;
    if (length) memcpy(tx_value_,value,length);
    tx_length_=static_cast<uint16_t>(length); attributes_[10].value.len=length;
    notify_pending_=stats_.connected && stats_.notify_enabled;
    return Status::Ok;
}
bool LeProfiles::readWrite(BleWrite& value) {
    if (!write_pending_) return false;
    value=write_; write_pending_=false; return true;
}
}}
