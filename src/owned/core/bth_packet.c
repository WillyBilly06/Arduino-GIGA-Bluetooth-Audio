#include "bth_packet.h"
#include <string.h>

bth_status_t bth_h4_decode(const uint8_t *data, size_t len, bth_h4_frame_t *out) {
    if (!out) return BTH_ERR_PARAM;
    memset(out, 0, sizeof *out);
    if (!data) return BTH_ERR_PARAM;
    if (!len) return BTH_ERR_LENGTH;

    if (data[0] == BTH_H4_EVENT) {
        if (len < 3 || len != (size_t)data[2] + 3u) return BTH_ERR_LENGTH;
        out->event.code = data[1];
        out->event.params.data = data + 3;
        out->event.params.len = data[2];
    } else if (data[0] == BTH_H4_ACL) {
        if (len < 5 || len != (size_t)bth_read_le16(data + 3) + 5u) return BTH_ERR_LENGTH;
        const uint16_t flags = bth_read_le16(data + 1);
        const uint16_t handle = flags & 0x0fffu;
        if (handle > 0x0effu) return BTH_ERR_PARAM;
        out->acl.handle = handle;
        out->acl.pb = (uint8_t)((flags >> 12) & 3u);
        out->acl.bc = (uint8_t)(flags >> 14);
        out->acl.payload.data = data + 5;
        out->acl.payload.len = len - 5;
    } else if (data[0] == BTH_H4_SCO) {
        if (len < 4 || len != (size_t)data[3] + 4u) return BTH_ERR_LENGTH;
        const uint16_t flags = bth_read_le16(data + 1);
        if ((flags & 0xc000u) || (flags & 0x0fffu) > 0x0effu) return BTH_ERR_PARAM;
        out->sco.handle = flags & 0x0fffu;
        out->sco.packet_status = (uint8_t)((flags >> 12) & 3u);
        out->sco.payload = (bth_span_t){data + 4, len - 4};
    } else {
        return BTH_UNSUPPORTED;
    }
    out->type = data[0];
    return BTH_OK;
}

bth_status_t bth_hci_command_encode(uint8_t *out, size_t capacity, uint16_t opcode,
                                    const uint8_t *params, size_t len, size_t *written) {
    if (!written) return BTH_ERR_PARAM;
    *written = 0;
    if (!out || (!params && len)) return BTH_ERR_PARAM;
    if (len > 255) return BTH_ERR_LENGTH;
    if (capacity < len + 4) return BTH_ERR_CAPACITY;
    out[0] = BTH_H4_COMMAND;
    out[1] = (uint8_t)opcode;
    out[2] = (uint8_t)(opcode >> 8);
    out[3] = (uint8_t)len;
    if (len) memcpy(out + 4, params, len);
    *written = len + 4;
    return BTH_OK;
}
