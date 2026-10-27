#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main old_fixture_main
#include "test_owned_host.cpp"
#undef main
static const Bytes expected = {3, 4, 8, 0, 0x40, 0, 0x46, 0, 0, 0, 0,    0,
                               4, 1, 8, 0, 0x46, 0, 0,    0, 1, 2, 0xa0, 2};
static void verify(Fixture &f) {
  CHECK(f.output.size() == 2 && f.output[0].cid == 1 && f.output[1].cid == 1 &&
        f.output[0].bytes.size() == 12 && f.output[1].bytes.size() == 12);
  Bytes joined = f.output[0].bytes;
  joined.insert(joined.end(), f.output[1].bytes.begin(),
                f.output[1].bytes.end());
  CHECK(joined == expected);
}
int main() {
  {
    Fixture f(false);
    f.boot();
    f.classicConnect();
    f.output.clear();
    f.acl(0x25, 1, {2, 4, 4, 0, 1, 0, 0x46, 0});
    f.settle();
    verify(f);
    f.acl(0x25, 1, {5, 1, 6, 0, 0x40, 0, 0, 0, 0, 0});
    f.acl(0x25, 1, {4, 5, 8, 0, 0x40, 0, 0, 0, 1, 2, 0, 4});
    f.settle();
    CHECK(f.output.back().bytes == Bytes({5, 5, 6, 0, 0x46, 0, 0, 0, 0, 0}));
    CHECK(f.host.signaling_.channels[0].state == BTH_CHANNEL_OPEN &&
          !f.host.radioStats().transport_errors);
  }
  {
    Fixture f(false);
    f.boot();
    f.event(4, {1, 2, 3, 4, 5, 6, 0, 0, 0, 1});
    f.settle();
    for (unsigned i = 0; i < 7; ++i)
      CHECK(bth_cmdq_push(&f.host.commands_, 0x0c16, nullptr, 0, false) ==
            BTH_OK);
    f.event(3, {0, 0x25, 0, 1, 2, 3, 4, 5, 6, 1, 0});
    CHECK(f.host.deferred_count_ == 1);
    f.acl(0x25, 1, {2, 4, 4, 0, 1, 0, 0x46, 0});
    f.ready = false;
    f.poll();
    CHECK(f.host.control_.used == 0 && f.host.rx_.outstanding == 0);
    f.ready = true;
    f.settle();
    verify(f);
    CHECK(!f.host.radioStats().transport_errors && f.host.deferred_count_ == 0);
  }
  {
    Fixture f(false);
    f.boot();
    f.classicConnect();
    bth_control_reservation_t held[3];
    for (auto &s : held)
      CHECK(bth_control_tx_reserve(&f.host.control_, 0, &s) == BTH_OK);
    f.ready = false;
    f.acl(0x25, 1, {2, 4, 4, 0, 1, 0, 0x46, 0});
    f.poll();
    CHECK(f.host.peers_[0].has_pending &&
          f.host.signaling_.channels[0].state == BTH_CHANNEL_CLOSED &&
          f.host.rx_.outstanding == 1);
    CHECK(bth_control_tx_cancel(&f.host.control_, held[0].token) == BTH_OK);
    f.output.clear();
    f.ready = true;
    f.settle();
    verify(f);
    CHECK(!f.host.peers_[0].has_pending && f.host.control_.used == 2);
    for (unsigned i = 1; i < 3; ++i)
      CHECK(bth_control_tx_cancel(&f.host.control_, held[i].token) == BTH_OK);
  }
  printf("Actual actor one-command CID1/atomicpressure/deferred/channel "
         "lifecycle %u checks passed\n",
         checks);
}
