#include "bth_init.h"
void bth_boot_timer_start(bth_boot_timer_t *t,uint32_t now) {
    if(t)*t=(bth_boot_timer_t){.deadline=now+5000,.phase=BTH_BOOT_CONTROLLER,.initialized=true};
}
bool bth_boot_timer_expired(const bth_boot_timer_t *t,uint32_t now) {
    return !t || !t->initialized || (t->phase!=BTH_BOOT_READY && (int32_t)(now-t->deadline)>=0);
}
bool bth_boot_timer_enter(bth_boot_timer_t *t,bth_boot_phase_t phase,uint32_t now) {
    if(!t || !t->initialized || phase>BTH_BOOT_READY || phase<t->phase || bth_boot_timer_expired(t,now))return false;
    if(phase==t->phase)return true;
    t->phase=phase;t->deadline=now+(phase==BTH_BOOT_CRYPTO?35000:5000);return true;
}
#include <string.h>

static const uint16_t commands[] = {0x0c03, 0x1001, 0x1009, 0x1005, 0x2002};
static const uint8_t complete_lengths[] = {4, 12, 10, 11, 7};

void bth_init_start(bth_init_t *init) {
    if (!init) return;
    memset(init, 0, sizeof *init);
    bth_hci_flow_init(&init->flow);
}

bth_status_t bth_init_next_command(bth_init_t *init, uint8_t *out, size_t capacity, size_t *written) {
    if (!written) return BTH_ERR_PARAM;
    *written = 0;
    if (!init) return BTH_ERR_PARAM;
    if (init->stage == BTH_INIT_READY) return BTH_IGNORED;
    if (init->stage >= BTH_INIT_FAILED) return BTH_ERR_STATE;
    if (init->pending || !init->flow.command_credits) return BTH_MORE;
    const uint16_t opcode = commands[init->stage];
    const bth_status_t status = bth_hci_command_encode(out, capacity, opcode, NULL, 0, written);
    if (status != BTH_OK) return status;
    if (!bth_hci_flow_take_command(&init->flow)) { *written = 0; return BTH_ERR_STATE; }
    init->pending_opcode = opcode;
    init->pending = true;
    return BTH_OK;
}

static bth_status_t fail(bth_init_t *init, uint8_t error, bth_status_t status) {
    init->error = error;
    init->stage = BTH_INIT_FAILED;
    init->pending = false;
    return status;
}

bth_status_t bth_init_on_event(bth_init_t *init, const bth_h4_frame_t *event) {
    if (!init || !event) return BTH_ERR_PARAM;
    const bth_status_t flow_status = bth_hci_flow_on_event(&init->flow, event);
    if (flow_status < 0) return fail(init, 0xff, flow_status);
    if (event->type == BTH_H4_EVENT && event->event.code == 0x10) {
        if (event->event.params.len != 1) return fail(init, 0xff, BTH_ERR_LENGTH);
        return fail(init, event->event.params.data[0], BTH_ERR_STATE);
    }
    if (event->type != BTH_H4_EVENT || !init->pending) return BTH_IGNORED;
    const bth_span_t p = event->event.params;
    if (event->event.code == 0x0f) {
        if (bth_read_le16(p.data + 2) != init->pending_opcode) return BTH_IGNORED;
        return p.data[0] ? fail(init, p.data[0], BTH_ERR_STATE) : BTH_MORE;
    }
    if (event->event.code != 0x0e || bth_read_le16(p.data + 1) != init->pending_opcode) return BTH_IGNORED;
    if (p.len < 4) return fail(init, 0xff, BTH_ERR_LENGTH);
    if (p.data[3]) return fail(init, p.data[3], BTH_ERR_STATE);
    if (init->stage >= BTH_INIT_READY || p.len != complete_lengths[init->stage]) {
        return fail(init, 0xff, BTH_ERR_LENGTH);
    }

    switch (init->stage) {
    case BTH_INIT_RESET:
        break;
    case BTH_INIT_VERSION:
        init->info.hci_version = p.data[4];
        init->info.manufacturer = bth_read_le16(p.data + 8);
        break;
    case BTH_INIT_ADDRESS:
        memcpy(init->info.address, p.data + 4, 6);
        break;
    case BTH_INIT_BUFFERS:
        init->info.classic_length = bth_read_le16(p.data + 4);
        init->info.classic_packets = bth_read_le16(p.data + 7);
        break;
    case BTH_INIT_LE_BUFFERS:
        init->info.le_length = bth_read_le16(p.data + 4);
        init->info.le_packets = p.data[6];
        if (!bth_hci_flow_set_buffers(&init->flow, init->info.classic_length,
                                     init->info.classic_packets, init->info.le_length,
                                     init->info.le_packets)) return fail(init, 0xff, BTH_ERR_PARAM);
        break;
    default:
        return fail(init, 0xff, BTH_ERR_STATE);
    }
    init->pending = false;
    init->stage = (bth_init_stage_t)(init->stage + 1);
    return BTH_OK;
}
