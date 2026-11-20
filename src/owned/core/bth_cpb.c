#include "bth_cpb.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)bth_read_le16(p) | ((uint32_t)bth_read_le16(p + 2) << 16); }
static bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

unsigned bth_cpb_packet_payload(uint16_t t) {
    if (t & 0x8000) return 339; /* DH5 */
    if (t & 0x4000) return 224; /* DM5 */
    if (t & 0x0800) return 183; /* DH3 */
    if (t & 0x0400) return 121; /* DM3 */
    if (t & 0x0010) return 27;  /* DH1 */
    if (t & 0x0008) return 17;  /* DM1 */
    return 0;
}
static bool types_valid(uint16_t t) { return t && !(t & ~(uint16_t)BTH_CPB_PACKET_TYPES); }

void bth_cpb_tx_defaults(bth_cpb_tx_config_t *c) {
    memset(c, 0, sizeof *c);
    c->lt_addr = 1;
    c->packet_types = 0x0018;            /* DM1|DH1 */
    c->interval_min = 0x0050;            /* 50 ms */
    c->interval_max = 0x00a0;            /* 100 ms */
    c->supervision_timeout = 0x2000;     /* 5.12 s */
    c->train_interval_min = 0x0080;      /* 80 ms */
    c->train_interval_max = 0x0100;      /* 160 ms */
    c->train_timeout = 0x0002a000;       /* ~107.5 s */
    c->continuous_train = true;
}
void bth_cpb_rx_defaults(bth_cpb_rx_config_t *c) {
    memset(c, 0, sizeof *c);
    c->scan_timeout = 0x4000;            /* 10.24 s */
    c->scan_window = 0x0100;             /* 160 ms */
    c->scan_interval = 0x0200;           /* 320 ms */
    c->supervision_timeout = 0x2000;     /* 5.12 s */
    c->remote_timing_accuracy = 250;
    c->skip = 0;
    c->packet_types = 0x0018;
    c->retry_ms = 1000;
}
bool bth_cpb_tx_valid(const bth_cpb_tx_config_t *c) {
    if (!c || c->lt_addr < 1 || c->lt_addr > 7 || !types_valid(c->packet_types)) return false;
    if (c->interval_min < 2 || c->interval_max > 0xfffe || c->interval_min > c->interval_max) return false;
    if ((c->interval_min | c->interval_max) & 1) return false;
    if (c->supervision_timeout < 2 || c->supervision_timeout > 0xfffe) return false;
    if (c->train_interval_min < 0x20 || c->train_interval_max > 0xfffe || c->train_interval_min > c->train_interval_max) return false;
    if (c->train_timeout < 2 || c->train_timeout > 0x07fffffe) return false;
    return c->fragment_bytes <= BTH_CPB_FRAGMENT_LIMIT;
}
bool bth_cpb_rx_valid(const bth_cpb_rx_config_t *c) {
    if (!c || c->lt_addr > 7 || !types_valid(c->packet_types)) return false;
    if (c->scan_timeout < 0x22 || c->scan_timeout > 0xfffe) return false;
    if (c->scan_window < 0x11 || c->scan_window > 0xfffe) return false;
    if (c->scan_interval < 2 || c->scan_interval > 0xfffe || (c->scan_interval & 1) || c->scan_window > c->scan_interval) return false;
    if (c->supervision_timeout < 2 || c->supervision_timeout > 0xfffe) return false;
    return c->layout <= BTH_CPB_LAYOUT_BROADCOM;
}

bool bth_cpb_init(bth_cpb_t *s, uint16_t manufacturer, uint8_t *tx, size_t tx_capacity,
                  uint8_t *rx_work, uint8_t *rx_ready, size_t rx_capacity) {
    if (!s || (!tx && tx_capacity) || ((!rx_work || !rx_ready) && rx_capacity)) return false;
    memset(s, 0, sizeof *s);
    s->manufacturer = manufacturer;
    s->tx_data = tx; s->tx_capacity = tx_capacity;
    s->rx_work = rx_work; s->rx_ready = rx_ready; s->rx_capacity = rx_capacity;
    s->last_status = 0;
    s->initialized = true;
    return true;
}

bth_status_t bth_cpb_start_transmit(bth_cpb_t *s, const bth_cpb_tx_config_t *c) {
    if (!s || !s->initialized) return BTH_ERR_PARAM;
    if (s->state != BTH_CPB_OFF) return BTH_ERR_STATE;
    if (!bth_cpb_tx_valid(c) || !s->tx_capacity) return BTH_ERR_PARAM;
    s->tx = *c;
    const unsigned payload = bth_cpb_packet_payload(c->packet_types);
    unsigned chunk = c->fragment_bytes ? c->fragment_bytes : payload;
    if (chunk > BTH_CPB_FRAGMENT_LIMIT) chunk = BTH_CPB_FRAGMENT_LIMIT;
    s->chunk = (uint8_t)chunk;
    s->role = BTH_CPB_ROLE_TRANSMITTER;
    s->state = BTH_CPB_TX_SETUP;
    s->stop_requested = false;
    s->train_started = s->train_running = s->train_restart = false;
    s->tx_sent = 0;
    s->tx_restart = false;
    /* Deleting the reserved LT_ADDR discards controller-held data: a loaded
     * payload (also one set before start) is sent during every setup. */
    s->tx_dirty = s->tx_loaded;
    return BTH_OK;
}
bth_status_t bth_cpb_start_receive(bth_cpb_t *s, const bth_cpb_rx_config_t *c) {
    if (!s || !s->initialized) return BTH_ERR_PARAM;
    if (s->state != BTH_CPB_OFF) return BTH_ERR_STATE;
    if (!bth_cpb_rx_valid(c) || !s->rx_capacity) return BTH_ERR_PARAM;
    s->rx = *c;
    s->role = BTH_CPB_ROLE_RECEIVER;
    s->state = BTH_CPB_RX_SETUP;
    s->stop_requested = false;
    s->rx_fill = 0; s->rx_assembling = false;
    memset(&s->sync, 0, sizeof s->sync);
    return BTH_OK;
}
bth_status_t bth_cpb_set_data(bth_cpb_t *s, const uint8_t *data, size_t length) {
    if (!s || !s->initialized || (!data && length)) return BTH_ERR_PARAM;
    if (length > s->tx_capacity) return BTH_ERR_LENGTH;
    if (s->state != BTH_CPB_OFF && s->state != BTH_CPB_TX_SETUP && s->state != BTH_CPB_TX_ACTIVE) return BTH_ERR_STATE;
    if (length) memcpy(s->tx_data, data, length);
    s->tx_length = length;
    /* A fragment sequence in flight restarts from its first fragment. */
    if (s->pending && s->pending_opcode == 0x0c76) s->tx_restart = true;
    s->tx_sent = 0;
    s->tx_dirty = true;
    s->tx_loaded = true;
    return BTH_OK;
}
static void settle_off(bth_cpb_t *s) {
    if (s->cpb_enabled || s->reserved || s->rx_enabled || s->scan_active || s->pending) return;
    s->state = BTH_CPB_OFF;
    s->role = BTH_CPB_ROLE_NONE;
    s->stop_requested = false;
    s->train_running = s->train_restart = s->train_started = s->train_params = false;
}
void bth_cpb_stop(bth_cpb_t *s) {
    if (!s || !s->initialized || s->state == BTH_CPB_OFF) return;
    s->stop_requested = true;
    s->state = BTH_CPB_STOPPING;
    settle_off(s);
}
static void fail(bth_cpb_t *s, uint16_t opcode, uint8_t status) {
    s->last_opcode = opcode; s->last_status = status ? status : 0xff;
    ++s->counters.command_errors;
    s->state = BTH_CPB_FAILED;
}
static void retry_scan(bth_cpb_t *s, uint32_t now, uint32_t wait) {
    if (s->stop_requested) { s->state = BTH_CPB_STOPPING; settle_off(s); return; }
    s->state = BTH_CPB_RX_RETRY_WAIT;
    s->deadline_ms = now + wait;
}
static bth_status_t emit(bth_cpb_t *s, bth_cpb_command_t *out, uint16_t opcode, size_t length) {
    out->opcode = opcode; out->length = (uint8_t)length;
    s->pending = true; s->pending_opcode = opcode;
    ++s->counters.commands;
    return BTH_OK;
}
static bth_status_t emit_mask(bth_cpb_t *s, bth_cpb_command_t *out) {
    /* Page 2 bits 15..18, 20, 21: Synchronization Train Complete/Received,
     * CPB Receive, CPB Timeout, Peripheral Page Response Timeout, CPB Channel
     * Map Change. No other page-2 event is used by this host. */
    static const uint8_t mask[8] = {0x00, 0x80, 0x37, 0, 0, 0, 0, 0};
    memcpy(out->params, mask, sizeof mask);
    return emit(s, out, 0x0c63, sizeof mask);
}
static bth_status_t emit_set_cpb(bth_cpb_t *s, bth_cpb_command_t *out, bool enable) {
    uint8_t *p = out->params;
    s->pending_enable = enable;
    p[0] = enable; p[1] = s->tx.lt_addr; p[2] = s->tx.lpo_allowed;
    put16(p + 3, s->tx.packet_types); put16(p + 5, s->tx.interval_min);
    put16(p + 7, s->tx.interval_max); put16(p + 9, s->tx.supervision_timeout);
    return emit(s, out, 0x0441, 11);
}
static bth_status_t emit_fragment(bth_cpb_t *s, bth_cpb_command_t *out) {
    const size_t remaining = s->tx_length - s->tx_sent;
    const size_t n = remaining < s->chunk ? remaining : s->chunk;
    uint8_t fragment;
    if (!s->tx_sent) fragment = n == s->tx_length ? 0x03 : 0x01; /* single / start */
    else fragment = s->tx_sent + n == s->tx_length ? 0x02 : 0x00; /* end / continuation */
    out->params[0] = s->tx.lt_addr; out->params[1] = fragment; out->params[2] = (uint8_t)n;
    if (n) memcpy(out->params + 3, s->tx_data + s->tx_sent, n);
    return emit(s, out, 0x0c76, 3 + n);
}
static bth_status_t emit_receive(bth_cpb_t *s, bth_cpb_command_t *out, bool enable) {
    uint8_t *p = out->params;
    memset(p, 0, 34);
    s->pending_enable = enable;
    p[0] = enable;
    memcpy(p + 1, s->sync.transmitter, 6);
    p[7] = s->sync.lt_addr;
    if (enable) {
        put16(p + 8, s->sync.interval);
        put32(p + 10, s->sync.clock_offset);
        put32(p + 14, s->sync.next_instant);
        put16(p + 18, s->rx.supervision_timeout);
        p[20] = s->rx.remote_timing_accuracy;
        p[21] = s->rx.skip;
        put16(p + 22, s->rx.packet_types);
        memcpy(p + 24, s->sync.map, 10);
    }
    return emit(s, out, 0x0442, 34);
}
static bth_status_t emit_scan(bth_cpb_t *s, bth_cpb_command_t *out) {
    uint8_t *p = out->params;
    memcpy(p, s->rx.transmitter, 6);
    put16(p + 6, s->rx.scan_timeout); put16(p + 8, s->rx.scan_window); put16(p + 10, s->rx.scan_interval);
    ++s->counters.scans;
    s->state = BTH_CPB_RX_SETUP;
    return emit(s, out, 0x0444, 12);
}

bth_status_t bth_cpb_next(bth_cpb_t *s, uint32_t now, bth_cpb_command_t *out) {
    if (!s || !s->initialized || !out) return BTH_ERR_PARAM;
    if (s->pending) return BTH_IGNORED;
    switch (s->state) {
    case BTH_CPB_TX_SETUP:
    case BTH_CPB_TX_ACTIVE:
        if (!s->mask_written) return emit_mask(s, out);
        if (!s->reserved) { out->params[0] = s->tx.lt_addr; return emit(s, out, 0x0c74, 1); }
        if (!s->train_params) {
            uint8_t *p = out->params;
            put16(p, s->tx.train_interval_min); put16(p + 2, s->tx.train_interval_max);
            put32(p + 4, s->tx.train_timeout); p[8] = s->tx.service_data;
            return emit(s, out, 0x0c78, 9);
        }
        if (!s->cpb_enabled) return emit_set_cpb(s, out, true);
        if (s->tx_dirty) return emit_fragment(s, out);
        if (!s->train_started || (s->train_restart && !s->train_running)) {
            s->train_restart = false;
            return emit(s, out, 0x0443, 0);
        }
        return BTH_IGNORED;
    case BTH_CPB_RX_SETUP:
        if (!s->mask_written) return emit_mask(s, out);
        return emit_scan(s, out);
    case BTH_CPB_RX_RETRY_WAIT:
        if (!due(now, s->deadline_ms)) return BTH_IGNORED;
        return emit_scan(s, out);
    case BTH_CPB_RX_SCANNING:
        /* The train event must arrive within the scan timeout; guard a lost one. */
        if (s->scan_active && due(now, s->deadline_ms)) {
            s->scan_active = false; ++s->counters.scan_failures;
            retry_scan(s, now, s->rx.retry_ms);
        }
        return BTH_IGNORED;
    case BTH_CPB_RX_SYNCING:
        return emit_receive(s, out, true);
    case BTH_CPB_STOPPING:
        if (s->cpb_enabled) return emit_set_cpb(s, out, false);
        if (s->rx_enabled) return emit_receive(s, out, false);
        if (s->reserved) { out->params[0] = s->tx.lt_addr; return emit(s, out, 0x0c75, 1); }
        if (s->scan_active && due(now, s->deadline_ms)) s->scan_active = false;
        settle_off(s);
        return BTH_IGNORED;
    default:
        return BTH_IGNORED;
    }
}

static void completed(bth_cpb_t *s, uint16_t opcode, uint8_t status, const uint8_t *ret, size_t n, uint32_t now) {
    s->pending = false;
    const bool ok = status == 0;
    if (!ok) { s->last_opcode = opcode; s->last_status = status; }
    const bool stopping = s->state == BTH_CPB_STOPPING;
    switch (opcode) {
    case 0x0c63:
        if (ok) s->mask_written = true;
        else if (!stopping) fail(s, opcode, status);
        break;
    case 0x0c74:
        if (ok) s->reserved = true;
        else if (!stopping) fail(s, opcode, status);
        break;
    case 0x0c78:
        if (ok) { s->train_params = true; if (n >= 2) s->train_interval = bth_read_le16(ret); }
        else if (!stopping) fail(s, opcode, status);
        break;
    case 0x0441:
        /* An enable that completes after stop() still has to be undone; a
         * failed disable is not retried. */
        if (!s->pending_enable) { s->cpb_enabled = false; break; }
        if (ok) { s->cpb_enabled = true; if (n >= 3) s->granted_interval = bth_read_le16(ret + 1); }
        else if (!stopping) fail(s, opcode, status);
        break;
    case 0x0c75:
        s->reserved = false;
        break;
    case 0x0c76:
        if (stopping) break;
        if (!ok) { fail(s, opcode, status); break; }
        ++s->counters.fragments_sent;
        if (s->tx_restart) { s->tx_restart = false; s->tx_sent = 0; break; }
        {
            const size_t remaining = s->tx_length - s->tx_sent;
            s->tx_sent += remaining < s->chunk ? remaining : s->chunk;
        }
        if (s->tx_sent >= s->tx_length) { s->tx_dirty = false; s->tx_sent = 0; ++s->counters.data_updates; }
        break;
    case 0x0443:
        if (stopping) break;
        if (ok) {
            s->train_started = s->train_running = true; ++s->counters.trains_started;
            if (s->state == BTH_CPB_TX_SETUP) s->state = BTH_CPB_TX_ACTIVE;
        } else if (s->state == BTH_CPB_TX_SETUP) {
            fail(s, opcode, status);
        } else {
            ++s->counters.train_errors; ++s->counters.command_errors;
        }
        break;
    case 0x0444:
        if (ok) {
            s->scan_active = true;
            /* Scan timeout in slots plus a margin for the event itself. */
            s->deadline_ms = now + (uint32_t)s->rx.scan_timeout * 5u / 8u + 2000u;
            if (stopping) break;
            s->state = BTH_CPB_RX_SCANNING;
        } else if (!stopping) {
            ++s->counters.scan_failures; ++s->counters.command_errors;
            retry_scan(s, now, s->rx.retry_ms);
        }
        break;
    case 0x0442:
        if (!s->pending_enable) { s->rx_enabled = false; break; }
        if (ok) { s->rx_enabled = true; if (!stopping) { s->state = BTH_CPB_RX_ACTIVE; ++s->counters.syncs; } }
        else if (!stopping) { ++s->counters.command_errors; retry_scan(s, now, s->rx.retry_ms); }
        break;
    default:
        break;
    }
    if (stopping) settle_off(s);
}

static bool parse_train(const bth_cpb_t *s, const uint8_t *p, size_t n, bth_cpb_sync_t *out) {
    if (n != 29) return false;
    bth_cpb_layout_t layout = s->rx.layout;
    if (layout == BTH_CPB_LAYOUT_AUTO) {
        layout = s->manufacturer == 15 ? BTH_CPB_LAYOUT_BROADCOM : BTH_CPB_LAYOUT_STANDARD;
        /* A known LT_ADDR disambiguates the two orders. */
        if (s->rx.lt_addr) {
            const bool standard = p[21] == s->rx.lt_addr, broadcom = p[22] == s->rx.lt_addr;
            if (standard && !broadcom) layout = BTH_CPB_LAYOUT_STANDARD;
            else if (broadcom && !standard) layout = BTH_CPB_LAYOUT_BROADCOM;
        }
    }
    memcpy(out->transmitter, p + 1, 6);
    out->clock_offset = get32(p + 7);
    memcpy(out->map, p + 11, 10);
    if (layout == BTH_CPB_LAYOUT_BROADCOM) {
        out->service_data = p[21]; out->lt_addr = p[22];
        out->next_instant = get32(p + 23); out->interval = bth_read_le16(p + 27);
    } else {
        out->lt_addr = p[21]; out->next_instant = get32(p + 22);
        out->interval = bth_read_le16(p + 26); out->service_data = p[28];
    }
    return out->lt_addr >= 1 && out->lt_addr <= 7 && out->interval >= 2 && !(out->interval & 1);
}
static void deliver(bth_cpb_t *s, const uint8_t *data, size_t n, uint32_t clock) {
    if (s->rx_unread) ++s->counters.messages_overwritten;
    if (n) memcpy(s->rx_ready, data, n);
    s->rx_ready_length = n; s->rx_ready_clock = clock; s->rx_unread = true;
    ++s->message_generation; ++s->counters.messages;
}
static void receive(bth_cpb_t *s, const uint8_t *p, size_t n) {
    if (n < 18 || n != 18u + p[17]) return;
    if (!s->rx_enabled || memcmp(p, s->sync.transmitter, 6) || p[6] != s->sync.lt_addr) return;
    ++s->counters.packets;
    const uint32_t clock = get32(p + 7);
    const uint8_t rx_status = p[15], fragment = p[16], length = p[17];
    const uint8_t *data = p + 18;
    if (rx_status) { ++s->counters.packet_errors; s->rx_assembling = false; s->rx_fill = 0; return; }
    ++s->counters.fragments_received;
    switch (fragment) {
    case 0x03:
        if (s->rx_assembling) ++s->counters.reassembly_errors;
        s->rx_assembling = false; s->rx_fill = 0;
        if (length > s->rx_capacity) { ++s->counters.reassembly_errors; return; }
        deliver(s, data, length, clock);
        return;
    case 0x01:
        if (s->rx_assembling) ++s->counters.reassembly_errors;
        s->rx_fill = 0; s->rx_assembling = true;
        /* fall through */
    case 0x00:
    case 0x02:
        if (!s->rx_assembling || s->rx_fill + length > s->rx_capacity) {
            /* An orphan continuation/end just means reception joined mid-message. */
            if (s->rx_assembling) ++s->counters.reassembly_errors;
            s->rx_assembling = false; s->rx_fill = 0; return;
        }
        if (length) memcpy(s->rx_work + s->rx_fill, data, length);
        s->rx_fill += length;
        if (fragment == 0x02) { deliver(s, s->rx_work, s->rx_fill, clock); s->rx_assembling = false; s->rx_fill = 0; }
        return;
    default:
        ++s->counters.reassembly_errors; s->rx_assembling = false; s->rx_fill = 0;
        return;
    }
}

bth_status_t bth_cpb_on_event(bth_cpb_t *s, const bth_h4_frame_t *f, uint32_t now) {
    if (!s || !s->initialized || !f || f->type != BTH_H4_EVENT) return BTH_IGNORED;
    const uint8_t code = f->event.code;
    const uint8_t *p = f->event.params.data;
    const size_t n = f->event.params.len;
    if (code == 0x0e && n >= 4) {
        const uint16_t op = bth_read_le16(p + 1);
        if (!s->pending || op != s->pending_opcode || op == 0x0443 || op == 0x0444) return BTH_IGNORED;
        completed(s, op, p[3], p + 4, n - 4, now);
        return BTH_OK;
    }
    if (code == 0x0f && n == 4) {
        const uint16_t op = bth_read_le16(p + 2);
        if (!s->pending || op != s->pending_opcode) return BTH_IGNORED;
        /* Command Status completes 0x0443/0x0444; for others only a failure does. */
        if (op != 0x0443 && op != 0x0444 && !p[0]) return BTH_IGNORED;
        completed(s, op, p[0], NULL, 0, now);
        return BTH_OK;
    }
    switch (code) {
    case 0x4f: /* Synchronization Train Complete */
        if (s->role != BTH_CPB_ROLE_TRANSMITTER || n != 1) return BTH_IGNORED;
        s->train_running = false;
        ++s->counters.trains_completed;
        if (p[0]) ++s->counters.train_errors;
        if (s->state == BTH_CPB_TX_ACTIVE && s->tx.continuous_train) s->train_restart = true;
        return BTH_OK;
    case 0x50: /* Synchronization Train Received */
        if (s->role != BTH_CPB_ROLE_RECEIVER || !s->scan_active || n != 29) return BTH_IGNORED;
        s->scan_active = false;
        if (s->state == BTH_CPB_STOPPING) { settle_off(s); return BTH_OK; }
        if (p[0]) { ++s->counters.scan_failures; s->last_status = p[0]; s->last_opcode = 0x0444; retry_scan(s, now, s->rx.retry_ms); return BTH_OK; }
        {
            bth_cpb_sync_t sync;
            if (!parse_train(s, p, n, &sync) || memcmp(sync.transmitter, s->rx.transmitter, 6) ||
                (s->rx.lt_addr && sync.lt_addr != s->rx.lt_addr)) {
                ++s->counters.sync_rejects; retry_scan(s, now, 0); return BTH_OK;
            }
            s->sync = sync;
        }
        s->state = BTH_CPB_RX_SYNCING;
        return BTH_OK;
    case 0x51: /* CPB Receive */
        if (s->role != BTH_CPB_ROLE_RECEIVER) return BTH_IGNORED;
        receive(s, p, n);
        return BTH_OK;
    case 0x52: /* CPB Timeout: the controller has stopped receiving. */
        if (s->role != BTH_CPB_ROLE_RECEIVER || n != 7 || !s->rx_enabled ||
            memcmp(p, s->sync.transmitter, 6) || p[6] != s->sync.lt_addr) return BTH_IGNORED;
        s->rx_enabled = false; s->rx_assembling = false; s->rx_fill = 0;
        ++s->counters.sync_losses;
        if (s->state == BTH_CPB_STOPPING) settle_off(s);
        else retry_scan(s, now, 0);
        return BTH_OK;
    case 0x55: /* CPB Channel Map Change: receivers learn the new map from a train. */
        if (s->role != BTH_CPB_ROLE_TRANSMITTER || n != 10) return BTH_IGNORED;
        ++s->counters.channel_map_changes;
        if (s->state == BTH_CPB_TX_ACTIVE) s->train_restart = true;
        return BTH_OK;
    default:
        return BTH_IGNORED;
    }
}

bool bth_cpb_take(bth_cpb_t *s, uint8_t *out, size_t capacity, size_t *length, uint32_t *clock, uint32_t *generation) {
    if (!s || !s->initialized || !s->rx_unread) return false;
    const size_t n = s->rx_ready_length < capacity ? s->rx_ready_length : capacity;
    if (n && out) memcpy(out, s->rx_ready, n);
    if (length) *length = s->rx_ready_length;
    if (clock) *clock = s->rx_ready_clock;
    if (generation) *generation = s->message_generation;
    s->rx_unread = false;
    return true;
}

void bth_cpb_controller_reset(bth_cpb_t *s) {
    if (!s || !s->initialized) return;
    const bth_cpb_t keep = *s;
    memset(s, 0, sizeof *s);
    s->initialized = true;
    s->manufacturer = keep.manufacturer;
    s->tx = keep.tx; s->rx = keep.rx;
    s->tx_data = keep.tx_data; s->tx_capacity = keep.tx_capacity; s->tx_length = keep.tx_length;
    s->tx_loaded = keep.tx_loaded; s->tx_dirty = keep.tx_loaded;
    s->rx_work = keep.rx_work; s->rx_ready = keep.rx_ready; s->rx_capacity = keep.rx_capacity;
    s->rx_ready_length = keep.rx_ready_length; s->rx_ready_clock = keep.rx_ready_clock; s->rx_unread = keep.rx_unread;
    s->message_generation = keep.message_generation;
    s->counters = keep.counters;
    s->last_opcode = keep.last_opcode; s->last_status = keep.last_status;
}
