#ifndef GIGA_BREDR_BTH_RFCOMM_H
#define GIGA_BREDR_BTH_RFCOMM_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_RFCOMM_PSM=3, BTH_RFCOMM_DLC_LIMIT=2, BTH_RFCOMM_MTU_LIMIT=512,
 BTH_RFCOMM_SABM=0x2f, BTH_RFCOMM_UA=0x63, BTH_RFCOMM_DM=0x0f,
 BTH_RFCOMM_DISC=0x43, BTH_RFCOMM_UIH=0xef };
typedef enum { BTH_RFCOMM_CLOSED=0,BTH_RFCOMM_CONNECTING,BTH_RFCOMM_OPEN,BTH_RFCOMM_DISCONNECTING } bth_rfcomm_state_t;
typedef enum { BTH_RFCOMM_NONE=0,BTH_RFCOMM_DLC_OPENED,BTH_RFCOMM_DLC_CLOSED,BTH_RFCOMM_DATA,BTH_RFCOMM_MUX_CLOSED,BTH_RFCOMM_LINE_STATUS } bth_rfcomm_event_type_t;
typedef struct { uint8_t dlci,control,credit;bool cr,pf,has_credit;bth_span_t payload; } bth_rfcomm_frame_t;
typedef struct { bth_rfcomm_event_type_t type;uint32_t generation;uint8_t dlci,status;bth_span_t payload; } bth_rfcomm_event_t;
typedef struct {
 uint32_t deadline;uint16_t mtu,tx_credits;uint8_t dlci,state,rx_credits,grant_pending;
 bool credit,msc_received,msc_sent,msc_acked,peer_flow_off,local_flow_off;
} bth_rfcomm_dlc_t;
typedef struct {
 uint32_t generation,deadline;uint16_t mtu;uint8_t server_channel,window,mode;
 bool initialized,initiator,peer_flow_off;bth_rfcomm_state_t mux;
 bth_rfcomm_dlc_t dlc[BTH_RFCOMM_DLC_LIMIT];
} bth_rfcomm_t;
// Exactly one RFCOMM frame per L2CAP SDU. Views borrow input only for this call.
// No parser callback, allocation, clock, HCI, security or transport registration.
bth_status_t bth_rfcomm_decode(const uint8_t*,size_t,bool,bth_rfcomm_frame_t*);
bth_status_t bth_rfcomm_encode(uint8_t dlci,bool cr,uint8_t control,bool pf,bool has_credit,uint8_t credit,
 const uint8_t*,size_t,uint8_t*,size_t,size_t*);
// Reserve window*mtu application RX bytes per admitted DLC before init (window1..7).
// mtu127..512; server_channel0 means no incoming service. All actor calls serialized.
bool bth_rfcomm_init(bth_rfcomm_t*,uint32_t,bool initiator,uint8_t server_channel,uint16_t mtu,uint8_t window);
void bth_rfcomm_close(bth_rfcomm_t*);
bth_status_t bth_rfcomm_start(bth_rfcomm_t*,uint32_t,uint32_t now_ms,uint8_t*,size_t,size_t*);
// Production RFCOMM1.2 initiators must try_credit=true; false supports legacy-peer fixtures.
bth_status_t bth_rfcomm_open(bth_rfcomm_t*,uint32_t,uint8_t remote_scn,bool try_credit,uint32_t,uint8_t*,size_t,size_t*);
// Durable TX capacity and application RX storage must be reserved before receive.
// On errors/short output, state is unchanged and written=0. Stale generation ignored.
bth_status_t bth_rfcomm_receive(bth_rfcomm_t*,uint32_t,uint32_t,const uint8_t*,size_t,uint8_t*,size_t,size_t*,bth_rfcomm_event_t*);
bth_status_t bth_rfcomm_next(bth_rfcomm_t*,uint32_t,uint32_t,uint8_t*,size_t,size_t*);
bth_status_t bth_rfcomm_send(bth_rfcomm_t*,uint32_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,size_t*);
// One credit returned only when the owner has consumed the corresponding RX frame.
bth_status_t bth_rfcomm_release(bth_rfcomm_t*,uint32_t,uint8_t,uint8_t);
bth_status_t bth_rfcomm_set_flow(bth_rfcomm_t*,uint32_t,uint8_t,bool off,uint8_t*,size_t,size_t*);
bth_status_t bth_rfcomm_disconnect(bth_rfcomm_t*,uint32_t,uint8_t,uint32_t,uint8_t*,size_t,size_t*);
// BTH_OK retires the entire mux after an uncertain timeout (initialized=false).
// The outer owner must close its L2CAP channel before initializing a fresh generation.
bth_status_t bth_rfcomm_expire(bth_rfcomm_t*,uint32_t,uint32_t);
bool bth_rfcomm_ready(const bth_rfcomm_t*,uint32_t,uint8_t);
#ifdef __cplusplus
}
#endif
#endif
