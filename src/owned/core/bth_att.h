#ifndef SPEAKER_BTH_ATT_H
#define SPEAKER_BTH_ATT_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_ATT_MTU_MIN=23,BTH_ATT_MTU_MAX=517,BTH_ATT_VALUE_MAX=512,BTH_ATT_ATTRIBUTE_LIMIT=64,BTH_ATT_CCCD_LIMIT=8 };
enum { BTH_ATT_READ=1,BTH_ATT_WRITE=2,BTH_ATT_WRITE_COMMAND=4,BTH_ATT_ENCRYPTED=8,BTH_ATT_AUTHENTICATED=16 };
enum { BTH_ATT_CONSTANT,BTH_ATT_APPLICATION,BTH_ATT_CCCD };
enum { BTH_ATT_ACTION_NONE,BTH_ATT_ACTION_WRITE,BTH_ATT_ACTION_CCCD,BTH_ATT_ACTION_MTU,BTH_ATT_ACTION_CONFIRMED };
typedef struct { bool encrypted,authenticated; uint8_t key_size; } bth_att_security_t;
typedef struct {
    uint16_t handle,group_end,maximum_length,related_handle;
    uint8_t permissions,kind,cccd_index,cccd_mask,minimum_key_size;
    bth_span_t uuid,value;
} bth_att_attribute_t;
typedef struct {
    const bth_att_attribute_t *attributes;
    size_t count;
    uint16_t mtu,maximum_mtu,cccd[BTH_ATT_CCCD_LIMIT],prepared_handle,prepared_length,indication_handle;
    bool initialized,mtu_exchanged,prepared_active,execute_pending,failed;
    uint8_t prepared[BTH_ATT_VALUE_MAX],prepared_seen[BTH_ATT_VALUE_MAX/8];
} bth_att_t;
typedef struct {
    bth_span_t value;
    uint16_t handle,cccd_value;
    uint8_t kind;
    bool prepared;
} bth_att_action_t;
// Single-owner, one instance per LE ATT bearer. Database/value pointers remain
// valid; application changes to values are serialized with this owner. Init is
// permitted only after previous borrowed actions/execute storage are released.
bool bth_att_init(bth_att_t *s,const bth_att_attribute_t *attributes,size_t count,uint16_t maximum_mtu);
// Reserve transport/application capacity before committing an action. Normal
// writes borrow input storage. Prepared execution borrows s->prepared until
// bth_att_execute_release(); a host must keep that buffer/link lease alive.
// Apply/adopt an application action before sending its response. MORE means an
// earlier prepared action still owns storage; retain/retry this request. Short
// output leaves all state unchanged. Remote errors are wire ErrorRsp;
// unsupported commands/responses are ignored without application side effects.
bth_status_t bth_att_process(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t security,
    uint8_t *out,size_t capacity,size_t *written,bth_att_action_t *action);
void bth_att_execute_release(bth_att_t *s);
bth_status_t bth_att_notify(bth_att_t *s,uint16_t value_handle,uint16_t cccd_handle,
    const uint8_t *value,size_t length,bool indication,bth_att_security_t security,
    uint8_t *out,size_t capacity,size_t *written);
// Owner's ATT indication timeout poisons this bearer; disconnect LE without
// resetting the independent Classic stream. No authorization follows a timer.
void bth_att_expire_indication(bth_att_t *s);
#ifdef __cplusplus
}
#endif
#endif
