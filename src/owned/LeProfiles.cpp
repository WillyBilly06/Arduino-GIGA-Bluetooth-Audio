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
        config.advertising_interval_min<0x20 || config.advertising_interval_max>0x4000 ||
        config.advertising_interval_min>config.advertising_interval_max ||
        config.security!=BleSecurityPolicy::PublicUnencrypted) return Status::InvalidArgument;
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
bool LeProfiles::attach(uint32_t generation,uint16_t handle,uint8_t role) {
    if (!initialized_ || !config_.enabled || !generation || handle>0x0eff || role!=1 || stats_.connected ||
        !replies_->peers[1].attached || replies_->peers[1].owner!=generation || replies_->peers[1].handle!=handle) return false;
    if (!initDatabase()) return false;
    stats_.generation=generation; stats_.connection_handle=handle; stats_.att_mtu=23;
    stats_.connected=true; stats_.advertising=false; stats_.notify_enabled=false;
    pending_request_length_=0; pending_response_=false;
    bth_le_gap_connection(&gap_,true); return true;
}
void LeProfiles::detach(uint32_t generation) {
    if (!stats_.connected || stats_.generation!=generation) return;
    stats_.connected=false; stats_.generation=0; stats_.connection_handle=0;
    stats_.att_mtu=23; stats_.notify_enabled=false; write_pending_=notify_pending_=false;
    ltk_pending_=parameters_pending_=false;
    pending_request_length_=0; pending_response_=false;
    write_=BleWrite{}; bth_le_gap_connection(&gap_,false);
}
void LeProfiles::event(const bth_h4_frame_t& event) {
    if (!initialized_ || !config_.enabled) return;
    const bth_status_t gapStatus=bth_le_gap_on_event(&gap_,&event);
    if (gapStatus<0) stats_.status=Status::HardwareError;
    stats_.advertising=gap_.advertising;
    if (!stats_.connected || event.type!=BTH_H4_EVENT || event.event.code!=0x3e) return;
    const bth_span_t p=event.event.params;
    if (!p.data || !p.len || (p.data[0]!=5 && p.data[0]!=6)) return;
    const size_t expected=p.data[0]==5?sizeof pending_ltk_:sizeof pending_parameters_;
    if (p.len!=expected) { ++stats_.malformed_pdus; return; }
    if (bth_read_le16(p.data+1)!=stats_.connection_handle) return;
    if (p.data[0]==5) { memcpy(pending_ltk_,p.data,p.len); ltk_pending_=true; }
    else { memcpy(pending_parameters_,p.data,p.len); parameters_pending_=true; }
}
bool LeProfiles::receive(uint32_t generation,uint16_t cid,const uint8_t* data,size_t length,uint32_t now) {
    if (!initialized_ || !stats_.connected || generation!=stats_.generation) return true;
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
void LeProfiles::poll(uint32_t now,bool mayAdvertise) {
    if (!initialized_ || !config_.enabled) return;
    if (stats_.connected) {
        for (unsigned i=0;i<2;++i) {
            bool& pending=i?parameters_pending_:ltk_pending_;
            if (!pending) continue;
            bth_h4_frame_t peerEvent={}; peerEvent.type=BTH_H4_EVENT;
            peerEvent.event={0x3e,{i?pending_parameters_:pending_ltk_,i?sizeof pending_parameters_:sizeof pending_ltk_}};
            const bth_status_t result=bth_le_gap_peer_event(&gap_,stats_.generation,stats_.connection_handle,&peerEvent);
            if (result==BTH_ERR_CAPACITY) break;
            pending=false;
            if (result<0) stats_.status=Status::HardwareError;
        }
    }
    if (bth_le_gap_expire(&gap_,now)<0 || bth_le_gap_next(&gap_,mayAdvertise && !stats_.connected,now)<0)
        stats_.status=Status::HardwareError;
    stats_.advertising=gap_.advertising;
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
Status LeProfiles::notify(const uint8_t* value,size_t length) {
    if (!initialized_ || !config_.enabled) return Status::Unsupported;
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
