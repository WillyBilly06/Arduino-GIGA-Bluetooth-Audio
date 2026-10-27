#include "bth_sdp.h"
// Immutable wire attributes, sorted by attribute ID. Feature bits match the
// existing speaker: audio sink; AVRCP target category 2; controller category 1.
// No browsing/cover-art capability is advertised without such a backend.
#define HANDLE(n) 9,0,0,10,0,1,0,n
#define BROWSE 9,0,5,0x35,3,0x19,0x10,2
#define LANGUAGE 9,0,6,0x35,9,9,0x65,0x6e,9,0,0x6a,9,1,0
#define NAME 9,1,0,0x25,7,'S','p','e','a','k','e','r'
#define AVCTP 9,0,4,0x35,0x10,0x35,6,0x19,1,0,9,0,0x17,0x35,6,0x19,0,0x17,9,1,4
#define AVRCP_PROFILE 9,0,9,0x35,8,0x35,6,0x19,0x11,0x0e,9,1,6
static const uint8_t sink[]={
    HANDLE(1),9,0,1,0x35,3,0x19,0x11,0x0b,
    9,0,4,0x35,0x10,0x35,6,0x19,1,0,9,0,0x19,0x35,6,0x19,0,0x19,9,1,3,
    BROWSE,LANGUAGE,9,0,9,0x35,8,0x35,6,0x19,0x11,0x0d,9,1,3,NAME,9,3,0x11,9,0,1
};
static const uint8_t target[]={HANDLE(2),9,0,1,0x35,3,0x19,0x11,0x0c,AVCTP,BROWSE,LANGUAGE,AVRCP_PROFILE,NAME,9,3,0x11,9,0,2};
static const uint8_t controller[]={HANDLE(3),9,0,1,0x35,6,0x19,0x11,0x0e,0x19,0x11,0x0f,AVCTP,BROWSE,LANGUAGE,AVRCP_PROFILE,NAME,9,3,0x11,9,0,1};
static const bth_sdp_record_t records[]={
    {0x10001,{sink,sizeof sink}}, {0x10002,{target,sizeof target}}, {0x10003,{controller,sizeof controller}}
};
const bth_sdp_record_t *bth_sdp_speaker_records(size_t *count) {
    if (count) *count=sizeof records/sizeof records[0];
    return records;
}
