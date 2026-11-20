/* Connectionless Peripheral Broadcast kernel: exact HCI encodings, both
 * Synchronization Train Received layouts, fragmentation/reassembly, timeout
 * resynchronization and stop races. Synthetic events only. */
#include "bth_cpb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static bth_cpb_t cpb;
static uint8_t tx_store[600], rx_a[600], rx_b[600];
static const uint8_t peer[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t lt1[1] = {1};
static const uint8_t granted[3] = {1, 0x64, 0};

static bth_status_t event(uint8_t code, const uint8_t *bytes, size_t count, uint32_t now) {
    bth_h4_frame_t f;
    memset(&f, 0, sizeof f);
    f.type = BTH_H4_EVENT; f.event.code = code; f.event.params.data = bytes; f.event.params.len = count;
    return bth_cpb_on_event(&cpb, &f, now);
}
static void complete(uint16_t op, uint8_t status, const uint8_t *ret, size_t n) {
    uint8_t b[64] = {1, (uint8_t)op, (uint8_t)(op >> 8), status};
    if (n) memcpy(b + 4, ret, n);
    CHECK(event(0x0e, b, 4 + n, 0) == BTH_OK);
}
static void cstatus(uint16_t op, uint8_t status, uint32_t now) {
    const uint8_t b[4] = {status, 1, (uint8_t)op, (uint8_t)(op >> 8)};
    CHECK(event(0x0f, b, 4, now) == BTH_OK);
}
static bth_cpb_command_t next(uint16_t expected, uint32_t now) {
    bth_cpb_command_t c, again;
    memset(&c, 0, sizeof c);
    CHECK(bth_cpb_next(&cpb, now, &c) == BTH_OK);
    if (c.opcode != expected) fprintf(stderr, "opcode %04x expected %04x\n", c.opcode, expected);
    CHECK(c.opcode == expected);
    CHECK(bth_cpb_next(&cpb, now, &again) == BTH_IGNORED); /* one in flight */
    return c;
}
static void idle(uint32_t now) { bth_cpb_command_t c; CHECK(bth_cpb_next(&cpb, now, &c) == BTH_IGNORED); }

static void tx_setup(const bth_cpb_tx_config_t *c) {
    CHECK(bth_cpb_start_transmit(&cpb, c) == BTH_OK);
    CHECK(bth_cpb_start_transmit(&cpb, c) == BTH_ERR_STATE);
    bth_cpb_command_t m = next(0x0c63, 0);
    const uint8_t mask[8] = {0, 0x80, 0x37, 0, 0, 0, 0, 0};
    CHECK(m.length == 8 && !memcmp(m.params, mask, 8));
    complete(0x0c63, 0, NULL, 0);
    m = next(0x0c74, 0); CHECK(m.length == 1 && m.params[0] == c->lt_addr);
    complete(0x0c74, 0, lt1, 1);
    m = next(0x0c78, 0);
    const uint8_t train[9] = {0x80, 0, 0, 1, 0, 0xa0, 2, 0, 0};
    CHECK(m.length == 9 && !memcmp(m.params, train, 9));
    const uint8_t ti[2] = {0xc0, 0}; complete(0x0c78, 0, ti, 2);
    CHECK(cpb.train_interval == 0xc0);
    m = next(0x0441, 0);
    const uint8_t set[11] = {1, 1, 0, 0x18, 0, 0x50, 0, 0xa0, 0, 0, 0x20};
    CHECK(m.length == 11 && !memcmp(m.params, set, 11));
    complete(0x0441, 0, granted, 3);
    CHECK(cpb.granted_interval == 0x64);
}

static void test_transmitter(void) {
    bth_cpb_tx_config_t c; bth_cpb_tx_defaults(&c);
    CHECK(bth_cpb_tx_valid(&c));
    CHECK(bth_cpb_init(&cpb, 15, tx_store, sizeof tx_store, NULL, NULL, 0));
    /* Payload before start is sent during setup, before the train. */
    CHECK(bth_cpb_set_data(&cpb, (const uint8_t *)"GIGA!", 5) == BTH_OK);
    tx_setup(&c);
    bth_cpb_command_t m = next(0x0c76, 0);
    CHECK(m.length == 8 && m.params[0] == 1 && m.params[1] == 0x03 && m.params[2] == 5 && !memcmp(m.params + 3, "GIGA!", 5));
    complete(0x0c76, 0, lt1, 1);
    CHECK(cpb.counters.data_updates == 1);
    m = next(0x0443, 0); CHECK(m.length == 0);
    /* A Command Complete for 0x0443 is not its completion. */
    const uint8_t cc[4] = {1, 0x43, 0x04, 0};
    CHECK(event(0x0e, cc, 4, 0) == BTH_IGNORED && cpb.pending);
    cstatus(0x0443, 0, 0);
    CHECK(cpb.state == BTH_CPB_TX_ACTIVE && cpb.train_running && !cpb.pending);
    idle(0);
    /* Continuous train restarts after completion. */
    const uint8_t done[1] = {0};
    CHECK(event(0x4f, done, 1, 0) == BTH_OK);
    CHECK(!cpb.train_running && cpb.counters.trains_completed == 1);
    next(0x0443, 0); cstatus(0x0443, 0, 0);
    CHECK(cpb.counters.trains_started == 2);
    /* Channel-map change restarts a train once the current one finishes. */
    const uint8_t map[10] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
    CHECK(event(0x55, map, 10, 0) == BTH_OK && cpb.counters.channel_map_changes == 1);
    idle(0);
    CHECK(event(0x4f, done, 1, 0) == BTH_OK);
    next(0x0443, 0);
    /* A train restart failure while active is counted, not fatal. */
    cstatus(0x0443, 0x0c, 0);
    CHECK(cpb.state == BTH_CPB_TX_ACTIVE && cpb.counters.train_errors == 1);
    idle(0);
    /* Fragmented payload: DM1|DH1 -> 27-byte chunks. 60 bytes = 27+27+6. */
    uint8_t big[60]; for (unsigned i = 0; i < sizeof big; ++i) big[i] = (uint8_t)i;
    CHECK(bth_cpb_set_data(&cpb, big, sizeof big) == BTH_OK);
    m = next(0x0c76, 0); CHECK(m.params[1] == 0x01 && m.params[2] == 27 && m.params[3] == 0);
    /* An update mid-sequence restarts from the first fragment. */
    CHECK(bth_cpb_set_data(&cpb, big, sizeof big) == BTH_OK);
    complete(0x0c76, 0, lt1, 1);
    m = next(0x0c76, 0); CHECK(m.params[1] == 0x01 && m.params[2] == 27);
    complete(0x0c76, 0, lt1, 1);
    m = next(0x0c76, 0); CHECK(m.params[1] == 0x00 && m.params[2] == 27 && m.params[3] == 27);
    complete(0x0c76, 0, lt1, 1);
    m = next(0x0c76, 0); CHECK(m.params[1] == 0x02 && m.params[2] == 6 && m.params[3] == 54 && m.length == 9);
    complete(0x0c76, 0, lt1, 1);
    CHECK(cpb.counters.data_updates == 2 && !cpb.tx_dirty);
    CHECK(bth_cpb_set_data(&cpb, big, sizeof tx_store + 1) == BTH_ERR_LENGTH);
    /* Stop: disable then delete the reserved LT_ADDR. */
    bth_cpb_stop(&cpb);
    CHECK(bth_cpb_set_data(&cpb, big, 4) == BTH_ERR_STATE);
    m = next(0x0441, 0); CHECK(m.length == 11 && m.params[0] == 0 && m.params[1] == 1);
    complete(0x0441, 0, granted, 3);
    m = next(0x0c75, 0); CHECK(m.length == 1 && m.params[0] == 1);
    complete(0x0c75, 0, lt1, 1);
    idle(0);
    CHECK(cpb.state == BTH_CPB_OFF && !cpb.cpb_enabled && !cpb.reserved);
    /* Restart reuses the page-2 mask written once per init and resends the payload. */
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_OK);
    next(0x0c74, 0); complete(0x0c74, 0, lt1, 1);
    next(0x0c78, 0); complete(0x0c78, 0, NULL, 0);
    next(0x0441, 0); complete(0x0441, 0, granted, 3);
    m = next(0x0c76, 0); CHECK(m.params[1] == 0x01 && m.params[2] == 27);
}

static void test_tx_validation_and_failure(void) {
    bth_cpb_tx_config_t c; bth_cpb_tx_defaults(&c);
    c.lt_addr = 0; CHECK(!bth_cpb_tx_valid(&c)); c.lt_addr = 8; CHECK(!bth_cpb_tx_valid(&c)); c.lt_addr = 1;
    c.packet_types = 0x0002; CHECK(!bth_cpb_tx_valid(&c)); c.packet_types = 0; CHECK(!bth_cpb_tx_valid(&c)); c.packet_types = 0x0018;
    c.interval_min = 0x51; CHECK(!bth_cpb_tx_valid(&c)); c.interval_min = 0xa2; CHECK(!bth_cpb_tx_valid(&c)); c.interval_min = 0x50;
    c.train_interval_min = 0x1e; CHECK(!bth_cpb_tx_valid(&c)); c.train_interval_min = 0x80;
    c.train_timeout = 0x08000000; CHECK(!bth_cpb_tx_valid(&c)); c.train_timeout = 0x2a000;
    c.fragment_bytes = 253; CHECK(!bth_cpb_tx_valid(&c)); c.fragment_bytes = 0;
    CHECK(bth_cpb_packet_payload(0x8000 | 0x0008) == 339 && bth_cpb_packet_payload(0x0008) == 17);
    /* No transmit storage: transmitter rejected. */
    CHECK(bth_cpb_init(&cpb, 15, NULL, 0, rx_a, rx_b, sizeof rx_a));
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_ERR_PARAM);
    /* Setup failure: FAILED keeps what it reserved; stop releases it. */
    CHECK(bth_cpb_init(&cpb, 15, tx_store, sizeof tx_store, NULL, NULL, 0));
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_OK);
    next(0x0c63, 0); complete(0x0c63, 0, NULL, 0);
    next(0x0c74, 0); complete(0x0c74, 0, lt1, 1);
    next(0x0c78, 0); complete(0x0c78, 0x12, NULL, 0);
    CHECK(cpb.state == BTH_CPB_FAILED && cpb.last_opcode == 0x0c78 && cpb.last_status == 0x12);
    idle(0);
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_ERR_STATE);
    bth_cpb_stop(&cpb);
    next(0x0c75, 0); complete(0x0c75, 0, lt1, 1);
    CHECK(cpb.state == BTH_CPB_OFF);
    /* A Command Status failure completes a Command-Complete command. */
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_OK);
    next(0x0c74, 0); cstatus(0x0c74, 0x01, 0);
    CHECK(cpb.state == BTH_CPB_FAILED && cpb.last_status == 0x01 && !cpb.pending);
    /* A successful Command Status for it is not a completion. */
    bth_cpb_stop(&cpb);
    CHECK(cpb.state == BTH_CPB_OFF);
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_OK);
    next(0x0c74, 0);
    const uint8_t ok_cs[4] = {0, 1, 0x74, 0x0c};
    CHECK(event(0x0f, ok_cs, 4, 0) == BTH_IGNORED && cpb.pending);
    /* stop() during the reserve: the late success is undone. */
    bth_cpb_stop(&cpb);
    CHECK(cpb.state == BTH_CPB_STOPPING);
    complete(0x0c74, 0, lt1, 1);
    next(0x0c75, 0); complete(0x0c75, 0, lt1, 1);
    CHECK(cpb.state == BTH_CPB_OFF);
    /* stop() during the enable: a late successful enable is disabled. */
    CHECK(bth_cpb_start_transmit(&cpb, &c) == BTH_OK);
    next(0x0c74, 0); complete(0x0c74, 0, lt1, 1);
    next(0x0c78, 0); complete(0x0c78, 0, NULL, 0);
    next(0x0441, 0);
    bth_cpb_stop(&cpb);
    complete(0x0441, 0, granted, 3);
    CHECK(cpb.cpb_enabled);
    bth_cpb_command_t m = next(0x0441, 0); CHECK(m.params[0] == 0);
    complete(0x0441, 0x0c, NULL, 0); /* a failed disable is not retried */
    next(0x0c75, 0); complete(0x0c75, 0, lt1, 1);
    CHECK(cpb.state == BTH_CPB_OFF);
}

static void train_event(uint8_t *e, bool broadcom, uint8_t lt, uint8_t status) {
    memset(e, 0, 29);
    e[0] = status;
    memcpy(e + 1, peer, 6);
    e[7] = 0x78; e[8] = 0x56; e[9] = 0x34; e[10] = 0x02;     /* clock offset 0x02345678 */
    for (unsigned i = 0; i < 10; ++i) e[11 + i] = (uint8_t)(0xf0 + i);
    if (broadcom) { e[21] = 0x5a; e[22] = lt; e[23] = 0x21; e[24] = 0x43; e[25] = 0x65; e[26] = 0x07; e[27] = 0x64; e[28] = 0; }
    else { e[21] = lt; e[22] = 0x21; e[23] = 0x43; e[24] = 0x65; e[25] = 0x07; e[26] = 0x64; e[27] = 0; e[28] = 0x5a; }
}
static void rx_start(uint16_t manufacturer, uint8_t lt, bth_cpb_layout_t layout, uint32_t now) {
    bth_cpb_rx_config_t c; bth_cpb_rx_defaults(&c);
    memcpy(c.transmitter, peer, 6); c.lt_addr = lt; c.layout = layout;
    CHECK(bth_cpb_rx_valid(&c));
    CHECK(bth_cpb_init(&cpb, manufacturer, NULL, 0, rx_a, rx_b, 100));
    CHECK(bth_cpb_start_receive(&cpb, &c) == BTH_OK);
    next(0x0c63, now); complete(0x0c63, 0, NULL, 0);
    bth_cpb_command_t m = next(0x0444, now);
    const uint8_t scan[12] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0, 0x40, 0, 1, 0, 2};
    CHECK(m.length == 12 && !memcmp(m.params, scan, 12));
    cstatus(0x0444, 0, now);
    CHECK(cpb.state == BTH_CPB_RX_SCANNING && cpb.scan_active);
}
static void rx_sync(bool broadcom_event, uint32_t now) {
    uint8_t e[29]; train_event(e, broadcom_event, 1, 0);
    CHECK(event(0x50, e, 29, now) == BTH_OK);
    CHECK(cpb.state == BTH_CPB_RX_SYNCING);
    CHECK(cpb.sync.lt_addr == 1 && cpb.sync.interval == 0x64 && cpb.sync.next_instant == 0x07654321 &&
          cpb.sync.clock_offset == 0x02345678 && cpb.sync.service_data == 0x5a && cpb.sync.map[9] == 0xf9);
    bth_cpb_command_t m = next(0x0442, now);
    const uint8_t expect[34] = {1, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 1, 0x64, 0, 0x78, 0x56, 0x34, 0x02,
                                0x21, 0x43, 0x65, 0x07, 0, 0x20, 250, 0, 0x18, 0,
                                0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    CHECK(m.length == 34 && !memcmp(m.params, expect, 34));
    const uint8_t ret[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 1};
    complete(0x0442, 0, ret, 7);
    CHECK(cpb.state == BTH_CPB_RX_ACTIVE && cpb.rx_enabled && cpb.counters.syncs >= 1);
}
static void cpb_rx(uint8_t fragment, const uint8_t *data, uint8_t n, uint8_t rx_status, uint32_t clock) {
    uint8_t e[18 + 255];
    memcpy(e, peer, 6); e[6] = 1;
    e[7] = (uint8_t)clock; e[8] = (uint8_t)(clock >> 8); e[9] = (uint8_t)(clock >> 16); e[10] = (uint8_t)(clock >> 24);
    memset(e + 11, 0, 4); e[15] = rx_status; e[16] = fragment; e[17] = n;
    if (n) memcpy(e + 18, data, n);
    CHECK(event(0x51, e, 18u + n, 0) == BTH_OK);
}

static void test_receiver(void) {
    /* Broadcom controller, Broadcom layout. */
    rx_start(15, 0, BTH_CPB_LAYOUT_AUTO, 0);
    rx_sync(true, 0);
    uint8_t out[128]; size_t n = 0; uint32_t clock = 0, gen = 0;
    CHECK(!bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen));
    cpb_rx(0x03, (const uint8_t *)"GIGA!", 5, 0, 0x1000);
    CHECK(bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen) && n == 5 && !memcmp(out, "GIGA!", 5) && clock == 0x1000 && gen == 1);
    CHECK(!bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen));
    /* Unread messages are overwritten, latest value wins. */
    cpb_rx(0x03, (const uint8_t *)"one", 3, 0, 1);
    cpb_rx(0x03, (const uint8_t *)"two", 3, 0, 2);
    CHECK(cpb.counters.messages_overwritten == 1);
    CHECK(bth_cpb_take(&cpb, out, 2, &n, &clock, &gen) && n == 3 && !memcmp(out, "tw", 2) && gen == 3);
    /* Fragmented message. */
    uint8_t part[40]; for (unsigned i = 0; i < 40; ++i) part[i] = (uint8_t)(i + 1);
    cpb_rx(0x01, part, 27, 0, 3); cpb_rx(0x00, part + 27, 10, 0, 4); cpb_rx(0x02, part + 37, 3, 0, 5);
    CHECK(bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen) && n == 40 && !memcmp(out, part, 40) && clock == 5);
    /* A bad packet drops the partial message. */
    cpb_rx(0x01, part, 27, 0, 6); cpb_rx(0x00, part, 10, 1, 7); cpb_rx(0x02, part, 3, 0, 8);
    CHECK(!bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen) && cpb.counters.packet_errors == 1);
    /* Joining mid-message is not an error; overflow is. */
    const unsigned before = cpb.counters.reassembly_errors;
    cpb_rx(0x00, part, 10, 0, 9);
    CHECK(cpb.counters.reassembly_errors == before);
    uint8_t wide[90] = {0};
    cpb_rx(0x01, wide, 90, 0, 10); cpb_rx(0x02, wide, 20, 0, 11);
    CHECK(cpb.counters.reassembly_errors == before + 1);
    cpb_rx(0x03, wide, 0, 0, 12); /* empty single message */
    CHECK(bth_cpb_take(&cpb, out, sizeof out, &n, &clock, &gen) && n == 0);
    /* Other transmitters and malformed events are ignored. */
    uint8_t other[18 + 1]; memcpy(other, peer, 6); other[0] = 0x99; other[6] = 1; memset(other + 7, 0, 11); other[17] = 1;
    const unsigned packets = cpb.counters.packets;
    event(0x51, other, sizeof other, 0);
    event(0x51, other, sizeof other - 1, 0);
    CHECK(cpb.counters.packets == packets);
    /* CPB Timeout: rescan immediately and resynchronize. */
    uint8_t timeout[7]; memcpy(timeout, peer, 6); timeout[6] = 1;
    CHECK(event(0x52, timeout, 7, 100) == BTH_OK);
    CHECK(!cpb.rx_enabled && cpb.counters.sync_losses == 1 && cpb.state == BTH_CPB_RX_RETRY_WAIT);
    next(0x0444, 100); cstatus(0x0444, 0, 100);
    rx_sync(true, 100);
    CHECK(cpb.counters.syncs == 2);
    /* Stop while receiving: disable with BD_ADDR/LT_ADDR only. */
    bth_cpb_stop(&cpb);
    bth_cpb_command_t m = next(0x0442, 0);
    CHECK(m.length == 34 && m.params[0] == 0 && !memcmp(m.params + 1, peer, 6) && m.params[7] == 1 && m.params[8] == 0 && m.params[33] == 0);
    complete(0x0442, 0, NULL, 0);
    CHECK(cpb.state == BTH_CPB_OFF);
}

static void test_receiver_layouts_and_retry(void) {
    /* Standard layout from a non-Broadcom controller. */
    rx_start(2, 0, BTH_CPB_LAYOUT_AUTO, 0);
    rx_sync(false, 0);
    /* Broadcom controller that reports the standard order: a known LT_ADDR
     * selects the matching order. */
    rx_start(15, 1, BTH_CPB_LAYOUT_AUTO, 0);
    {
        uint8_t e[29]; train_event(e, false, 1, 0); e[22] = 0x21; /* standard: e[21]=1, e[22]!=1 */
        CHECK(event(0x50, e, 29, 0) == BTH_OK && cpb.state == BTH_CPB_RX_SYNCING && cpb.sync.next_instant == 0x07654321);
    }
    /* Explicit layout is honoured. */
    rx_start(15, 0, BTH_CPB_LAYOUT_STANDARD, 0);
    rx_sync(false, 0);
    /* Failed train scan retries after retry_ms. */
    rx_start(15, 0, BTH_CPB_LAYOUT_AUTO, 0);
    uint8_t e[29]; train_event(e, true, 1, 0x08);
    CHECK(event(0x50, e, 29, 50) == BTH_OK);
    CHECK(cpb.state == BTH_CPB_RX_RETRY_WAIT && cpb.counters.scan_failures == 1 && cpb.last_status == 0x08);
    idle(1049);
    next(0x0444, 1050);
    /* Command Status failure also retries. */
    cstatus(0x0444, 0x0c, 1050);
    CHECK(cpb.state == BTH_CPB_RX_RETRY_WAIT && cpb.counters.scan_failures == 2);
    next(0x0444, 2050); cstatus(0x0444, 0, 2050);
    /* A train for another LT_ADDR is rejected and rescanned. */
    rx_start(15, 2, BTH_CPB_LAYOUT_BROADCOM, 0);
    train_event(e, true, 1, 0);
    CHECK(event(0x50, e, 29, 0) == BTH_OK && cpb.counters.sync_rejects == 1);
    next(0x0444, 0); cstatus(0x0444, 0, 0);
    /* Lost train event: the scan guard expires and retries. */
    idle(10239 + 1999);
    idle(10240 + 2000);
    CHECK(cpb.state == BTH_CPB_RX_RETRY_WAIT && cpb.counters.scan_failures == 1);
    /* Stop while scanning: no cancel command exists; the train event ends it. */
    rx_start(15, 0, BTH_CPB_LAYOUT_AUTO, 0);
    bth_cpb_stop(&cpb);
    idle(0);
    CHECK(cpb.state == BTH_CPB_STOPPING);
    train_event(e, true, 1, 0);
    CHECK(event(0x50, e, 29, 0) == BTH_OK && cpb.state == BTH_CPB_OFF);
    /* Stop during Set CPB Receive: a late enable is disabled. */
    rx_start(15, 0, BTH_CPB_LAYOUT_AUTO, 0);
    train_event(e, true, 1, 0);
    CHECK(event(0x50, e, 29, 0) == BTH_OK);
    next(0x0442, 0);
    bth_cpb_stop(&cpb);
    complete(0x0442, 0, NULL, 0);
    bth_cpb_command_t m = next(0x0442, 0); CHECK(m.params[0] == 0);
    complete(0x0442, 0, NULL, 0);
    CHECK(cpb.state == BTH_CPB_OFF);
    /* Invalid receiver configurations. */
    bth_cpb_rx_config_t c; bth_cpb_rx_defaults(&c);
    c.scan_timeout = 0x21; CHECK(!bth_cpb_rx_valid(&c)); c.scan_timeout = 0x4000;
    c.scan_window = 0x300; CHECK(!bth_cpb_rx_valid(&c)); c.scan_window = 0x100;
    c.scan_interval = 0x201; CHECK(!bth_cpb_rx_valid(&c)); c.scan_interval = 0x200;
    c.lt_addr = 8; CHECK(!bth_cpb_rx_valid(&c)); c.lt_addr = 0;
    CHECK(bth_cpb_init(&cpb, 15, tx_store, sizeof tx_store, NULL, NULL, 0));
    CHECK(bth_cpb_start_receive(&cpb, &c) == BTH_ERR_PARAM);
    /* Transmitter events are ignored by a receiver and vice versa. */
    const uint8_t done[1] = {0};
    CHECK(event(0x4f, done, 1, 0) == BTH_IGNORED);
}

static void test_controller_reset(void) {
    bth_cpb_tx_config_t c; bth_cpb_tx_defaults(&c);
    CHECK(bth_cpb_init(&cpb, 15, tx_store, sizeof tx_store, NULL, NULL, 0));
    CHECK(bth_cpb_set_data(&cpb, (const uint8_t *)"abc", 3) == BTH_OK);
    tx_setup(&c);
    next(0x0c76, 0); complete(0x0c76, 0, lt1, 1);
    next(0x0443, 0); cstatus(0x0443, 0, 0);
    const unsigned updates = cpb.counters.data_updates;
    /* HCI Reset: everything controller-side is gone; the payload is kept. */
    bth_cpb_controller_reset(&cpb);
    CHECK(cpb.state == BTH_CPB_OFF && !cpb.reserved && !cpb.cpb_enabled && !cpb.mask_written && !cpb.pending);
    CHECK(cpb.counters.data_updates == updates && cpb.tx_dirty && cpb.tx_length == 3);
    tx_setup(&c);
    bth_cpb_command_t m = next(0x0c76, 0);
    CHECK(m.params[2] == 3 && !memcmp(m.params + 3, "abc", 3));
    /* Never-loaded payload: no data command after reset. */
    CHECK(bth_cpb_init(&cpb, 15, tx_store, sizeof tx_store, NULL, NULL, 0));
    bth_cpb_controller_reset(&cpb);
    tx_setup(&c);
    next(0x0443, 0);
}

unsigned cpbSelftest(void) {
    test_controller_reset();
    test_transmitter();
    test_tx_validation_and_failure();
    test_receiver();
    test_receiver_layouts_and_retry();
    return 0;
}
unsigned cpbChecks(void) { return checks; }
