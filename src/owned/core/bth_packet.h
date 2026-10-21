#ifndef SPEAKER_BTH_PACKET_H
#define SPEAKER_BTH_PACKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTH_OK = 0,
    BTH_MORE = 1,
    BTH_UNSUPPORTED = 2,
    BTH_IGNORED = 3,
    BTH_ERR_PARAM = -1,
    BTH_ERR_LENGTH = -2,
    BTH_ERR_CAPACITY = -3,
    BTH_ERR_STATE = -4
} bth_status_t;

enum { BTH_H4_COMMAND = 1, BTH_H4_ACL = 2, BTH_H4_SCO = 3, BTH_H4_EVENT = 4 };

typedef struct { const uint8_t *data; size_t len; } bth_span_t;
typedef struct { uint8_t code; bth_span_t params; } bth_event_view_t;
typedef struct {
    uint16_t handle;
    uint8_t pb;
    uint8_t bc;
    bth_span_t payload;
} bth_acl_view_t;
typedef struct { uint16_t handle; uint8_t packet_status; bth_span_t payload; } bth_sco_view_t;
typedef struct {
    uint8_t type;
    union { bth_event_view_t event; bth_acl_view_t acl; bth_sco_view_t sco; };
} bth_h4_frame_t;

// Decode exactly one complete VHCI/H4 frame. Views borrow input storage and
// remain valid only while the caller owns it. Output is cleared on failure.
bth_status_t bth_h4_decode(const uint8_t *data, size_t len, bth_h4_frame_t *out);
bth_status_t bth_hci_command_encode(uint8_t *out, size_t capacity, uint16_t opcode,
                                    const uint8_t *params, size_t len, size_t *written);

static inline uint16_t bth_read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

#ifdef __cplusplus
}
#endif
#endif
