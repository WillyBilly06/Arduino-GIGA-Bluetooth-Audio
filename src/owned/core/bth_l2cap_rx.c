#include "bth_l2cap_rx.h"
#include <string.h>

static void reset_frame(bth_l2cap_rx_t *rx) {
    rx->used = rx->expected = 0;
    rx->active = false;
}

bool bth_l2cap_rx_init(bth_l2cap_rx_t *rx, uint16_t handle, uint8_t *storage, size_t capacity) {
    if (!rx) return false;
    memset(rx, 0, sizeof *rx);
    if (!storage || capacity < 4 || handle > 0x0effu) return false;
    rx->storage = storage;
    rx->capacity = capacity;
    rx->handle = handle;
    rx->initialized = true;
    return true;
}

static bth_status_t reject(bth_l2cap_rx_t *rx, bth_status_t why) {
    reset_frame(rx);
    return why;
}

bth_status_t bth_l2cap_rx_feed(bth_l2cap_rx_t *rx, const bth_acl_view_t *acl, bth_l2cap_pdu_t *out) {
    if (!out) return BTH_ERR_PARAM;
    memset(out, 0, sizeof *out);
    if (!rx || !acl) return BTH_ERR_PARAM;
    if (!rx->initialized) return BTH_ERR_STATE;
    if (acl->handle != rx->handle) return BTH_IGNORED;
    if (acl->bc) return BTH_UNSUPPORTED;
    if (!acl->payload.data && acl->payload.len) return reject(rx, BTH_ERR_PARAM);
    if (!acl->payload.len) return reject(rx, BTH_ERR_LENGTH);
    if (acl->pb > 2) return reject(rx, BTH_UNSUPPORTED);

    if (acl->pb != 1) {
        reset_frame(rx);
        rx->active = true;
        if (acl->payload.len >= 4) {
            const size_t expected = (size_t)bth_read_le16(acl->payload.data) + 4u;
            const uint16_t cid = bth_read_le16(acl->payload.data + 2);
            if (!cid) return reject(rx, BTH_ERR_PARAM);
            if (expected > rx->capacity) return reject(rx, BTH_ERR_CAPACITY);
            if (acl->payload.len > expected) return reject(rx, BTH_ERR_LENGTH);
            if (acl->payload.len == expected) {
                out->handle = rx->handle;
                out->cid = cid;
                out->payload = (bth_span_t){acl->payload.data + 4, expected - 4};
                reset_frame(rx);
                return BTH_OK;
            }
        }
    } else if (!rx->active) {
        return BTH_ERR_STATE;
    }

    if (rx->used > rx->capacity || acl->payload.len > rx->capacity - rx->used) {
        return reject(rx, BTH_ERR_CAPACITY);
    }
    memcpy(rx->storage + rx->used, acl->payload.data, acl->payload.len);
    rx->used += acl->payload.len;
    if (rx->used < 4) return BTH_MORE;
    if (!rx->expected) {
        rx->expected = (size_t)bth_read_le16(rx->storage) + 4u;
        if (!bth_read_le16(rx->storage + 2)) return reject(rx, BTH_ERR_PARAM);
        if (rx->expected > rx->capacity) return reject(rx, BTH_ERR_CAPACITY);
    }
    if (rx->used > rx->expected) return reject(rx, BTH_ERR_LENGTH);
    if (rx->used < rx->expected) return BTH_MORE;

    out->handle = rx->handle;
    out->cid = bth_read_le16(rx->storage + 2);
    out->payload = (bth_span_t){rx->storage + 4, rx->expected - 4};
    out->reassembled = true;
    reset_frame(rx);
    return BTH_OK;
}
