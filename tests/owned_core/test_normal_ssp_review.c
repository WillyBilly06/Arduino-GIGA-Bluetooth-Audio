/* Independent normal-link lifecycle regression. No transport or device access. */
#include "bth_links.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bth_hci_flow_t flow;
static bth_cmdq_t commands;
static bth_links_t links;
static unsigned checks, cases, saves;
static bool save_ok;
static uint8_t expected_type;
static const uint8_t peer[] = {1, 2, 3, 4, 5, 6};
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static bool save(void *context, const uint8_t address[6], const bth_bond_t *bond) {
    (void)context;
    CHECK(!memcmp(address, peer, 6));
    CHECK(bond->key_type == expected_type);
    ++saves;
    return save_ok;
}
static bth_confirmation_t confirm(void *context, const uint8_t address[6], uint32_t number) {
    (void)context;
    CHECK(!memcmp(address, peer, 6));
    CHECK(number == 1);
    return BTH_CONFIRM_ACCEPT;
}
static bth_status_t event(uint8_t code, const uint8_t *bytes, size_t count) {
    bth_h4_frame_t frame = {0};
    frame.type = BTH_H4_EVENT;
    frame.event.code = code;
    frame.event.params = (bth_span_t){bytes, count};
    return bth_links_on_event(&links, &frame);
}
static void next_command(uint16_t expected) {
    uint8_t bytes[264];
    size_t count = 0;
    flow.command_credits = 1;
    CHECK(bth_cmdq_next(&commands, &flow, bytes, sizeof bytes, &count) == BTH_OK);
    CHECK(bth_read_le16(bytes + 1) == expected);
}
static void address_reply(uint16_t opcode) {
    next_command(opcode);
    const uint8_t answer[] = {1, (uint8_t)opcode, (uint8_t)(opcode >> 8), 0, 1, 2, 3, 4, 5, 6};
    CHECK(event(14, answer, sizeof answer) == BTH_OK);
}
static void connect(void) {
    const uint8_t connected[] = {0, 0x25, 0, 1, 2, 3, 4, 5, 6, 1, 0};
    CHECK(event(3, connected, sizeof connected) == BTH_OK);
}
static void exchange(void) {
    CHECK(event(0x17, peer, sizeof peer) == BTH_OK);
    address_reply(0x040c);
    CHECK(event(0x31, peer, sizeof peer) == BTH_OK);
    address_reply(0x042b);
    const uint8_t comparison[] = {1, 2, 3, 4, 5, 6, 1, 0, 0, 0};
    CHECK(event(0x33, comparison, sizeof comparison) == BTH_OK);
    address_reply(0x042c);
}
static void begin(uint8_t auth_requirement, uint8_t type, bool local_auth) {
    ++cases;
    bth_hci_flow_init(&flow);
    CHECK(bth_hci_flow_set_buffers(&flow, 1021, 8, 0, 0));
    bth_cmdq_init(&commands);
    const bth_security_policy_t policy = {3, auth_requirement, 16, 0, {0}, 0, 0};
    const bth_security_callbacks_t callbacks = {NULL, save, NULL, confirm};
    CHECK(bth_links_init(&links, &flow, &commands, &policy, &callbacks, NULL));
    saves = 0;
    save_ok = true;
    expected_type = type;
    connect();
    if (local_auth) {
        next_command(0x0411);
        const uint8_t accepted[] = {0, 1, 0x11, 4};
        CHECK(event(15, accepted, sizeof accepted) == BTH_OK);
    }
    exchange();
}
static void fresh_key(uint8_t type) {
    uint8_t key[23];
    memcpy(key, peer, 6);
    memset(key + 6, 0xaa, 16); /* Synthetic fixture only. Never printed. */
    key[22] = type;
    CHECK(event(0x18, key, sizeof key) == BTH_OK);
}
static bth_status_t pairing(uint8_t status) {
    const uint8_t paired[] = {status, 1, 2, 3, 4, 5, 6};
    return event(0x36, paired, sizeof paired);
}
static void encryption(uint8_t value) {
    const uint8_t encrypted[] = {0, 0x25, 0, value};
    CHECK(event(8, encrypted, sizeof encrypted) == BTH_OK);
}
static bth_status_t size_result(uint8_t status, uint8_t size) {
    next_command(0x1408);
    const uint8_t answer[] = {1, 8, 0x14, status, 0x25, 0, size};
    return event(14, answer, sizeof answer);
}
static void fresh_ready(uint8_t type, uint8_t mode, unsigned ordering) {
    begin(4, type, false);
    if (ordering == 0) CHECK(pairing(0) == BTH_OK);
    fresh_key(type);
    if (ordering == 1) CHECK(pairing(0) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);
    encryption(mode);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);
    CHECK(size_result(0, 16) == BTH_OK);
    if (ordering == 2) {
        CHECK(!bth_links_secure(&links, 0x25) && saves == 0);
        CHECK(pairing(0) == BTH_OK);
    }
    CHECK(bth_links_secure(&links, 0x25) && saves == 1 && !links.classic.key_pending);
    CHECK(pairing(0) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 1);
}
static void normal_orders_and_key_types(void) {
    for (unsigned i = 0; i < 3; ++i) {
        fresh_ready(4, 1, i);
        fresh_ready(5, 1, i);
        fresh_ready(7, 2, i);
        fresh_ready(8, 2, i);
    }
}
static void required_facts_and_failure_paths(void) {
    begin(4, 4, false); /* No SSP completion. */
    fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);

    begin(4, 4, false); /* SSP and encryption have no fresh key. */
    CHECK(pairing(0) == BTH_OK); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);

    begin(4, 4, false); /* SSP and fresh key have no encryption. */
    CHECK(pairing(0) == BTH_OK); fresh_key(4);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);

    begin(4, 4, false); /* Encryption pending current size proof. */
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);

    begin(4, 4, false);
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1);
    CHECK(size_result(0, 15) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0 && links.classic.disconnecting);

    begin(4, 7, false); /* SC retains its existing encryption-mode requirement. */
    CHECK(pairing(0) == BTH_OK); fresh_key(7); encryption(1);
    CHECK(size_result(0, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0 && links.classic.disconnecting);

    begin(4, 4, false);
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1);
    CHECK(size_result(0x0c, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0 && links.classic.disconnecting);

    begin(4, 4, false);
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1); save_ok = false;
    CHECK(size_result(0, 16) == BTH_ERR_STATE);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 1 && links.classic.disconnecting);

    begin(4, 4, false);
    fresh_key(4); CHECK(pairing(5) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0 && links.classic.disconnecting);
}
static void lifetime_and_local_completion(void) {
    begin(4, 4, false);
    CHECK(pairing(0) == BTH_OK); fresh_key(4);
    const uint8_t disconnected[] = {0, 0x25, 0, 0x13};
    CHECK(event(5, disconnected, sizeof disconnected) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0);
    connect(); exchange(); fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0); /* Prior owner's SSP cannot carry over. */

    begin(4, 4, false);
    CHECK(pairing(0) == BTH_OK);
    CHECK(event(0x31, peer, sizeof peer) == BTH_OK); address_reply(0x042b);
    fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 0); /* New IO exchange invalidates prior completion. */

    begin(4, 4, true);
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 1);
    const uint8_t authenticated[] = {0, 0x25, 0};
    CHECK(event(6, authenticated, sizeof authenticated) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 1 && commands.used == 0);

    begin(4, 4, true); /* Existing authentication-initiator path needs no responder fallback. */
    fresh_key(4);
    CHECK(event(6, authenticated, sizeof authenticated) == BTH_OK);
    CHECK(saves == 1 && !bth_links_secure(&links, 0x25));
    next_command(0x0413);
    const uint8_t encrypt_accepted[] = {0, 1, 0x13, 4};
    CHECK(event(15, encrypt_accepted, sizeof encrypt_accepted) == BTH_OK);
    encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 1);

    begin(0, 4, false); /* Valid nonbonding policy keeps an encrypted session without storing a bond. */
    CHECK(pairing(0) == BTH_OK); fresh_key(4); encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 0 && !links.classic.bonded);

    fresh_ready(4, 1, 1);
    encryption(0);
    CHECK(!bth_links_secure(&links, 0x25) && saves == 1);
    encryption(1); CHECK(size_result(0, 16) == BTH_OK);
    CHECK(bth_links_secure(&links, 0x25) && saves == 1);
}
/* Accepted links defer local authentication so the initiator's own request
 * does not collide with ours (Linux/Realtek refuses it with Command Disallowed). */
static void accept_incoming(uint16_t delay) {
    ++cases;
    bth_hci_flow_init(&flow);
    CHECK(bth_hci_flow_set_buffers(&flow, 1021, 8, 0, 0));
    bth_cmdq_init(&commands);
    const bth_security_policy_t policy = {3, 4, 16, 0, {0}, delay, delay ? 5000u : 0u};
    const bth_security_callbacks_t callbacks = {NULL, save, NULL, confirm};
    CHECK(bth_links_init(&links, &flow, &commands, &policy, &callbacks, NULL));
    CHECK(bth_links_security_tick(&links, 5000) == BTH_IGNORED);
    const uint8_t request[] = {1, 2, 3, 4, 5, 6, 0x14, 0x04, 0x24, 1};
    CHECK(event(4, request, sizeof request) == BTH_OK);
    next_command(0x0409);
    connect();
}
static bool queued(uint16_t opcode) {
    for (unsigned i = 0; i < BTH_CMD_QUEUE_LIMIT; ++i)
        if (!(commands.free_mask & (1u << i)) && commands.slots[i].opcode == opcode) return true;
    return false;
}
static void deferred_acceptor_authentication(void) {
    accept_incoming(0); /* Zero keeps the immediate request and requests no QoS. */
    CHECK(queued(0x0411));
    CHECK(!queued(0x0807));
    accept_incoming(1000); /* Initiator never authenticates: local request after the grace. */
    CHECK(!queued(0x0411));
    CHECK(queued(0x0807)); /* Low-latency poll request follows Connection Complete. */
    CHECK(bth_links_security_tick(&links, 5999) == BTH_MORE);
    CHECK(!queued(0x0411));
    CHECK(bth_links_security_tick(&links, 6000) == BTH_OK);
    CHECK(queued(0x0411));
    CHECK(links.classic.collision_retries == 0);
    accept_incoming(1000); /* Initiator authenticates first: no local request at all. */
    CHECK(event(0x17, peer, sizeof peer) == BTH_OK);
    CHECK(bth_links_security_tick(&links, 9000) == BTH_IGNORED);
    CHECK(!queued(0x0411));
    accept_incoming(1000); /* Initiator pairs first (IO capability request). */
    CHECK(event(0x31, peer, sizeof peer) == BTH_OK);
    CHECK(bth_links_security_tick(&links, 9000) == BTH_IGNORED);
    CHECK(!queued(0x0411));
    accept_incoming(300); /* Peer waits on a secured channel: authenticate at the next tick. */
    CHECK(bth_links_expedite_authentication(&links, 0x26) == BTH_IGNORED);
    CHECK(bth_links_expedite_authentication(&links, 0x25) == BTH_OK);
    CHECK(bth_links_expedite_authentication(&links, 0x25) == BTH_IGNORED);
    CHECK(bth_links_security_tick(&links, 5000) == BTH_OK);
    CHECK(queued(0x0411));
    CHECK(bth_links_expedite_authentication(&links, 0x25) == BTH_IGNORED);
    puts("ACCEPTOR_DEFERRED_AUTHENTICATION_PASS");
}
int main(void) {
    deferred_acceptor_authentication();
    normal_orders_and_key_types();
    required_facts_and_failure_paths();
    lifetime_and_local_completion();
    printf("normal_ssp_independent cases=%u checks=%u PASS; own-core CPU only\n", cases, checks);
    return 0;
}
