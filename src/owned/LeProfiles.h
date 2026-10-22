#pragma once
#include "BleTypes.h"
#include "core/bth_att.h"
#include "core/bth_control_tx.h"
#include "core/bth_le_gap.h"

namespace giga_bredr { namespace detail {
// All methods run on the host owner. The public facade serializes API requests.
// Root owns link generations, ACL reassembly/credits, and control_tx attachment.
class LeProfiles {
public:
    enum : uint16_t { RxHandle=9, TxHandle=11, TxCccdHandle=12 };
    struct ControlState {uint16_t opcode;uint8_t stage,error;bool pending;uint32_t deadline;};
    ControlState controlState() const {return {gap_.pending_opcode,uint8_t(gap_.stage),gap_.error,gap_.pending,gap_.deadline};}
    Status begin(const BleConfig&,const char* name,bth_cmdq_t&,bth_control_tx_t&);
    // Privileged owner bootstrap: call only for its matching successful HCI
    // Reset completion before any LE command/connection in this module lifetime.
    bool startAfterReset(const bth_h4_frame_t& resetCompletion);
    bool attach(uint32_t generation,uint16_t handle,uint8_t role=1);
    void detach(uint32_t generation);
    void event(const bth_h4_frame_t&);
    // false exclusively means retain/retry this exact borrowed SDU for capacity.
    bool receive(uint32_t generation,uint16_t cid,const uint8_t*,size_t,uint32_t now);
    // Owner-gated once per poll, before later ATT input; never from GAP poll.
    void servicePendingAtt(uint32_t generation,uint32_t now);
    bool pendingAtt() const { return pending_request_length_!=0; }
    bool pendingAttExpired(uint32_t now) const {
        return pendingAtt() && uint32_t(now-pending_request_started_)>=5000;
    }
    void poll(uint32_t now,bool mayAdvertise);
    // Copies latest readable TX value. Notifications need a live subscription.
    Status notify(const uint8_t*,size_t);
    bool readWrite(BleWrite&);
    const BleStats& stats() const { return stats_; }
private:
    bool initDatabase();
    bool processAtt(uint32_t generation,const uint8_t*,size_t);
    BleConfig config_;
    BleStats stats_;
    bth_cmdq_t* commands_ = nullptr;
    bth_control_tx_t* replies_ = nullptr;
    bth_att_t att_ = {};
    bth_le_gap_t gap_ = {};
    bth_att_attribute_t attributes_[13] = {};
    BleWrite write_;
    uint8_t advertisement_[21] = {}, scan_response_[31] = {};
    uint8_t tx_value_[BleValueCapacity] = {};
    uint8_t pending_ltk_[13] = {}, pending_parameters_[11] = {};
    // Independent of physical HCI storage, reassembly, unread and prepared data.
    uint8_t pending_request_[BTH_ATT_MTU_MAX] = {};
    uint32_t pending_request_started_ = 0;
    uint16_t pending_request_length_ = 0;
    bool pending_response_ = false;
    char name_[64] = {};
    uint16_t tx_length_ = 0;
    uint8_t scan_length_ = 0;
    bool initialized_ = false, write_pending_ = false, notify_pending_ = false;
    bool ltk_pending_ = false, parameters_pending_ = false;
};
}}
