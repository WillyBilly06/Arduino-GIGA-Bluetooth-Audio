#include "bth_hci_flow.h"
#include <string.h>

static int link_index(const bth_hci_flow_t *f, uint16_t handle) {
    for (int i = 0; i < BTH_HCI_LINK_LIMIT; ++i) {
        if (f->links[i].active && f->links[i].handle == handle) return i;
    }
    return -1;
}

static bth_acl_pool_t *link_pool(bth_hci_flow_t *f, bth_link_type_t type) {
    // LE Read Buffer Size reports zero packets when both transports share the
    // BR/EDR pool. An LE connection must then consume that same pool's credits.
    return (type == BTH_LINK_LE && f->le.total) ? &f->le : &f->classic;
}

void bth_hci_flow_init(bth_hci_flow_t *f) {
    if (!f) return;
    memset(f, 0, sizeof *f);
    f->command_credits = 1;
}

bool bth_hci_flow_set_buffers(bth_hci_flow_t *f, uint16_t classic_len,
                              uint16_t classic_packets, uint16_t le_len, uint16_t le_packets) {
    if (!f || !classic_len || !classic_packets || (le_packets && !le_len)) return false;
    for (int i = 0; i < BTH_HCI_LINK_LIMIT; ++i) {
        if (f->links[i].outstanding) return false;
    }
    f->classic = (bth_acl_pool_t){classic_len, classic_packets, classic_packets};
    f->le = (bth_acl_pool_t){le_len, le_packets, le_packets};
    return true;
}

bool bth_hci_flow_attach(bth_hci_flow_t *f, uint16_t handle, bth_link_type_t type) {
    if (!f || handle > 0x0effu || (type != BTH_LINK_CLASSIC && type != BTH_LINK_LE) ||
        link_index(f, handle) >= 0) return false;
    for (int i = 0; i < BTH_HCI_LINK_LIMIT; ++i) {
        if (!f->links[i].active) {
            f->links[i] = (bth_hci_link_t){.handle = handle, .type = type, .active = true};
            return true;
        }
    }
    return false;
}

bool bth_hci_flow_detach(bth_hci_flow_t *f, uint16_t handle) {
    if (!f) return false;
    const int i = link_index(f, handle);
    if (i < 0) return false;
    bth_acl_pool_t *pool = link_pool(f, f->links[i].type);
    const uint32_t available = (uint32_t)pool->available + f->links[i].outstanding;
    if (available > pool->total) return false;
    pool->available = (uint16_t)available;
    memset(&f->links[i], 0, sizeof f->links[i]);
    return true;
}

bool bth_hci_flow_take_command(bth_hci_flow_t *f) {
    if (!f || !f->command_credits) return false;
    --f->command_credits;
    return true;
}

bool bth_hci_flow_take_acl(bth_hci_flow_t *f, uint16_t handle, uint16_t payload_len) {
    if (!f || !payload_len) return false;
    const int i = link_index(f, handle);
    if (i < 0) return false;
    bth_acl_pool_t *pool = link_pool(f, f->links[i].type);
    if (!pool->available || payload_len > pool->length) return false;
    --pool->available;
    ++f->links[i].outstanding;
    return true;
}

static bth_status_t completed_packets(bth_hci_flow_t *f, bth_span_t p) {
    if (!p.len || p.len != 1u + 4u * p.data[0]) return BTH_ERR_LENGTH;
    uint16_t completed[BTH_HCI_LINK_LIMIT] = {0};
    uint8_t seen = 0;
    // Validate all entries first, so a malformed last entry cannot replenish
    // credits for earlier entries and permit sends the controller cannot hold.
    for (unsigned n = 0; n < p.data[0]; ++n) {
        const uint8_t *entry = p.data + 1u + n * 4u;
        const uint16_t handle = bth_read_le16(entry);
        if (handle > 0x0effu) return BTH_ERR_PARAM;
        const int i = link_index(f, handle);
        if (i < 0) continue; // A disconnected handle has already been flushed.
        if (seen & (1u << i)) return BTH_ERR_PARAM;
        seen |= (uint8_t)(1u << i);
        completed[i] = bth_read_le16(entry + 2);
        if (completed[i] > f->links[i].outstanding) return BTH_ERR_STATE;
    }
    uint32_t classic = 0, le = 0;
    for (int i = 0; i < BTH_HCI_LINK_LIMIT; ++i) {
        if (f->links[i].type == BTH_LINK_LE && f->le.total) le += completed[i];
        else classic += completed[i];
    }
    if (classic + f->classic.available > f->classic.total ||
        le + f->le.available > f->le.total) return BTH_ERR_STATE;
    for (int i = 0; i < BTH_HCI_LINK_LIMIT; ++i) f->links[i].outstanding -= completed[i];
    f->classic.available += (uint16_t)classic;
    f->le.available += (uint16_t)le;
    return BTH_OK;
}

bth_status_t bth_hci_flow_on_event(bth_hci_flow_t *f, const bth_h4_frame_t *event) {
    if (!f || !event) return BTH_ERR_PARAM;
    if (event->type != BTH_H4_EVENT) return BTH_UNSUPPORTED;
    const bth_span_t p = event->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    switch (event->event.code) {
    case 0x0e: // Command Complete: grant, opcode, command-specific return data.
        if (p.len < 3) return BTH_ERR_LENGTH;
        f->command_credits = p.data[0];
        return BTH_OK;
    case 0x0f: // Command Status: status, grant, opcode.
        if (p.len != 4) return BTH_ERR_LENGTH;
        f->command_credits = p.data[1];
        return BTH_OK;
    case 0x13:
        return completed_packets(f, p);
    default:
        return BTH_UNSUPPORTED;
    }
}
