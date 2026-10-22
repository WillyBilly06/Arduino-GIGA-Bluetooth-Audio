#ifndef SPEAKER_BTH_LE_TRANSPORT_H
#define SPEAKER_BTH_LE_TRANSPORT_H
#include "bth_cmdq.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    BTH_LE_TRANSPORT_FEATURES, BTH_LE_TRANSPORT_FEATURES_WAIT,
    BTH_LE_TRANSPORT_UPDATE, BTH_LE_TRANSPORT_SIGNAL,
    BTH_LE_TRANSPORT_SIGNAL_WAIT, BTH_LE_TRANSPORT_UPDATE_WAIT,
    BTH_LE_TRANSPORT_DONE, BTH_LE_TRANSPORT_IDLE
} bth_le_transport_stage_t;
typedef struct {
    uint16_t interval_min,interval_max,latency,timeout;
} bth_le_transport_policy_t;
typedef struct {
    uint32_t owner,deadline;
    uint16_t handle,interval,latency,timeout,tx_octets,tx_time,rx_octets,rx_time;
    bth_le_transport_stage_t stage;
    uint8_t local_features,remote_features,role,identifier,error;
    bth_le_transport_policy_t policy;
    uint8_t local_feature_set[8],remote_feature_set[8];
    uint32_t attempt_deadline;
    bth_cmdq_t *commands;
    uint16_t command_opcode;
    bool started,attempt_timed,local_features_known,remote_features_known;
    bool parameters_known,matches_preference,fallback_used;
    // Source of the current valid actual tuple:01,0a or03; zero is unknown.
    // A failed later update changes last_update_status, never the actual tuple.
    uint8_t parameters_source,last_update_status;
    bool update_reported;
} bth_le_transport_t;
// One caller-owned peer generation. Original speaker preference:18..24,0,400.
bool bth_le_transport_attach(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,uint8_t local_features);
// Configured path: e.g.{64,96,2,500}=80..120ms,latency2,supervision5s.
// Configure once per generation, after canceling the prior instance. NULL local
// features means unknown; do not infer unsupported LL procedures from it.
// Configuration is idle: start once when the owner wants the preference (e.g.
// A2DP becomes active). Cached remote features/actual updates can arrive first.
bool bth_le_transport_policy_valid(const bth_le_transport_policy_t *policy);
bool bth_le_transport_configure(bth_le_transport_t *t,uint32_t owner,uint16_t handle,uint8_t role,
                               const uint8_t local_features[8],const bth_le_transport_policy_t *policy);
bth_status_t bth_le_transport_start(bth_le_transport_t *t,uint32_t now_ms);
// Optional collision/peer-procedure yield: no automatic retries this generation.
// Cancels only still-queued2016/2013 owned here; sent tombstones remain intact.
bth_status_t bth_le_transport_yield(bth_le_transport_t *t,uint8_t error);
bth_status_t bth_le_transport_cancel(bth_le_transport_t *t,uint32_t owner);
// Queues an owned HCI command, or returns one CID5 signaling SDU in out.
// Reserve transport space before calling in SIGNAL state. No completion/grants
// are consumed here; the central dispatcher retires each command exactly once.
bth_status_t bth_le_transport_next(bth_le_transport_t *t,bth_cmdq_t *commands,uint32_t now_ms,uint8_t *out,size_t capacity,size_t *written);
// command_owner is the oldest matching sent command's generation, read before
// central retirement. A bare opcode/handle is never enough to match a response.
bth_status_t bth_le_transport_event(bth_le_transport_t *t,const bth_h4_frame_t *event,uint32_t command_owner);
bth_status_t bth_le_transport_signal(bth_le_transport_t *t,const uint8_t *data,size_t length);
// Optional preference failure is reported in error; it does not poison the host.
bth_status_t bth_le_transport_expire(bth_le_transport_t *t,uint32_t now_ms);
// Pure reply builder for LE Remote Connection Parameter Request (subevent06).
// Caller queues the result as an urgent command with this live generation only
// after capacity is available. No command, credit or transport state is mutated.
// If enforcing policy, offer only the peer/policy interval intersection; preserve
// valid peer latency/timeout. Empty/invalid offers produce2021/reason3b.
bth_status_t bth_le_transport_peer_reply(const bth_le_transport_t *t,uint32_t owner,
                                        const bth_h4_frame_t *event,bool enforce_policy,
                                        uint8_t *out,size_t capacity,size_t *written,uint16_t *opcode);
#ifdef __cplusplus
}
#endif
#endif
