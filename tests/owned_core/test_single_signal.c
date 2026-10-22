#include "bth_control_tx.h"
#include "bth_l2cap_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "line%d: %s\n", __LINE__, #x);                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
// Exact semantic bytes produced by the existing signaling writer for Connect.
static const uint8_t bundle[] = {3, 4, 8, 0, 0x40, 0, 0x46, 0, 0, 0, 0,    0,
                                 4, 1, 8, 0, 0x46, 0, 0,    0, 1, 2, 0xa0, 2};
static void setup(bth_control_tx_t *q, bth_hci_flow_t *f, unsigned chunk,
                  unsigned packets) {
  bth_control_tx_init(q);
  bth_hci_flow_init(f);
  CHECK(bth_hci_flow_set_buffers(f, chunk, packets, chunk, packets));
  CHECK(bth_hci_flow_attach(f, 1, BTH_LINK_CLASSIC));
  CHECK(bth_hci_flow_attach(f, 2, BTH_LINK_LE));
  CHECK(bth_control_tx_attach(q, 0, 1, 100));
  CHECK(bth_control_tx_attach(q, 1, 2, 101));
}
static void commit(bth_control_tx_t *q, uint8_t peer, uint16_t cid,
                   const uint8_t *p, size_t n, bth_control_token_t *t) {
  bth_control_reservation_t r;
  CHECK(bth_control_tx_reserve(q, peer, &r) == BTH_OK);
  if (n)
    memcpy(r.data, p, n);
  CHECK(bth_control_tx_commit(q, r.token, cid, n, 672) == BTH_OK);
  if (t)
    *t = r.token;
}
static void complete(bth_hci_flow_t *f, uint16_t handle, unsigned count) {
  uint8_t event[] = {4,
                     0x13,
                     5,
                     1,
                     (uint8_t)handle,
                     (uint8_t)(handle >> 8),
                     (uint8_t)count,
                     (uint8_t)(count >> 8)};
  bth_h4_frame_t frame;
  CHECK(bth_h4_decode(event, sizeof event, &frame) == BTH_OK);
  CHECK(bth_hci_flow_on_event(f, &frame) == BTH_OK);
}
static void split(unsigned chunk) {
  bth_control_tx_t q;
  bth_hci_flow_t f;
  setup(&q, &f, chunk, 2);
  bth_control_token_t token;
  commit(&q, 0, 1, bundle, sizeof bundle, &token);
  CHECK(q.used == 1 && q.peers[0].count == 1);
  uint8_t out[1030], storage[672];
  size_t n;
  bth_l2cap_rx_t rx;
  CHECK(bth_l2cap_rx_init(&rx, 1, storage, sizeof storage));
  uint32_t owners[] = {100, 101};
  unsigned sdus = 0, fragments = 0;
  uint8_t joined[24];
  size_t joined_size = 0;
  while (q.used) {
    unsigned available = f.classic.available;
    CHECK(bth_control_tx_next(&q, owners, &f, out, 5, &n) == BTH_ERR_CAPACITY &&
          n == 0 && f.classic.available == available);
    CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK);
    ++fragments;
    bth_h4_frame_t frame;
    CHECK(bth_h4_decode(out, n, &frame) == BTH_OK);
    bth_l2cap_pdu_t pdu;
    bth_status_t status = bth_l2cap_rx_feed(&rx, &frame.acl, &pdu);
    CHECK(status == BTH_OK || status == BTH_MORE);
    if (status == BTH_OK) {
      CHECK(pdu.cid == 1 && pdu.payload.len == 12 &&
            pdu.payload.data[0] == (sdus ? 4 : 3));
      memcpy(joined + joined_size, pdu.payload.data, pdu.payload.len);
      joined_size += pdu.payload.len;
      ++sdus;
      if (sdus == 1)
        CHECK(q.used == 1 && q.peers[0].count == 1);
    }
    complete(&f, 1, 1);
  }
  CHECK(sdus == 2 && joined_size == sizeof bundle &&
        !memcmp(joined, bundle, sizeof bundle));
  CHECK(f.classic.available == 2 && f.links[0].outstanding == 0);
  CHECK(fragments == 2 * ((16 + chunk - 1) / chunk));
  CHECK(bth_control_tx_cancel(&q, token) == BTH_ERR_STATE);
}

static void pressureAndOtherCid(void) {
  bth_control_tx_t q;
  bth_hci_flow_t f;
  setup(&q, &f, 1021, 1);
  bth_control_token_t token;
  commit(&q, 0, 1, bundle, sizeof bundle, &token);
  bth_control_reservation_t held[3], extra;
  CHECK(bth_control_tx_reserve(&q, 0, &held[0]) == BTH_OK);
  CHECK(bth_control_tx_reserve(&q, 0, &held[1]) == BTH_OK);
  CHECK(bth_control_tx_reserve(&q, 1, &held[2]) == BTH_OK);
  CHECK(q.used == 4 &&
        bth_control_tx_reserve(&q, 0, &extra) == BTH_ERR_CAPACITY);
  uint32_t owners[] = {100, 101};
  uint8_t out[1030];
  size_t n;
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK &&
        q.used == 4 && q.peers[0].cursor == 12);
  unsigned cursor = q.peers[0].cursor;
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_MORE &&
        n == 0 && q.peers[0].cursor == cursor && q.used == 4);
  complete(&f, 1, 1);
  CHECK(bth_control_tx_next(&q, owners, &f, out, 10, &n) == BTH_ERR_CAPACITY &&
        f.classic.available == 1 && q.peers[0].cursor == cursor);
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK &&
        q.used == 3 && q.peers[0].cursor == 0);
  complete(&f, 1, 1);
  for (unsigned i = 0; i < 3; ++i)
    CHECK(bth_control_tx_cancel(&q, held[i].token) == BTH_OK);
  CHECK(q.used == 0);
  commit(&q, 0, 0x40, bundle, sizeof bundle, NULL);
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK &&
        n == 33 && bth_read_le16(out + 5) == 24 &&
        bth_read_le16(out + 7) == 0x40 && !memcmp(out + 9, bundle, 24) &&
        q.used == 0);
}
static void invalidAndGeneration(void) {
  bth_control_tx_t q;
  bth_hci_flow_t f;
  setup(&q, &f, 5, 2);
  bth_control_reservation_t r;
  CHECK(bth_control_tx_reserve(&q, 0, &r) == BTH_OK);
  memcpy(r.data, bundle, 24);
  bth_control_tx_t before = q;
  CHECK(bth_control_tx_commit(&q, r.token, 1, 23, 672) == BTH_ERR_LENGTH &&
        !memcmp(&q, &before, sizeof q));
  CHECK(bth_control_tx_commit(&q, r.token, 1, 3, 672) == BTH_ERR_LENGTH &&
        !memcmp(&q, &before, sizeof q));
  CHECK(bth_control_tx_commit(&q, r.token, 1, 24, 672) == BTH_OK);
  uint8_t out[32];
  size_t n;
  uint32_t owners[] = {100, 101};
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK &&
        q.peers[0].tx.active && q.peers[0].cursor == 0);
  CHECK(!bth_control_tx_detach(&q, 0, 999));
  CHECK(bth_control_tx_detach(&q, 0, 100) && q.used == 0 &&
        q.peers[0].cursor == 0 && !q.peers[0].tx.active);
  CHECK(bth_hci_flow_detach(&f, 1));
  CHECK(bth_hci_flow_attach(&f, 1, BTH_LINK_CLASSIC));
  CHECK(bth_control_tx_attach(&q, 0, 1, 102));
  CHECK(bth_control_tx_commit(&q, r.token, 1, 24, 672) == BTH_ERR_STATE);
  commit(&q, 0, 1, bundle, 24, NULL);
  unsigned credits = f.classic.available;
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) ==
            BTH_ERR_STATE &&
        n == 0 && f.classic.available == credits);
  owners[0] = 102;
  CHECK(bth_control_tx_next(&q, owners, &f, out, sizeof out, &n) == BTH_OK &&
        q.peers[0].cursor == 0 && out[2] == 0);
}
int main() {
  split(1021);
  split(27);
  split(5);
  pressureAndOtherCid();
  invalidAndGeneration();
  puts("Owned one CID1 command per SDU "
       "bytes/segmentation/retry/pressure/generation/credits PASS");
  printf("checks=%u\n", checks);
}
