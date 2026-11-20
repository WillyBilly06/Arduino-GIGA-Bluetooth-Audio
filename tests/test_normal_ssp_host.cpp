#include <cstddef>
#define private public
#include "owned/OwnedHost.h"
#undef private
#define main original_owned_host_main
#include "test_owned_host.cpp"
#undef main

static std::vector<uint16_t>* submitted_commands;
static void normal_responder_reaches_profiles(unsigned ordering) {
    Fixture f;
    f.boot();
    f.leConnect();
    std::vector<uint16_t> submitted;
    submitted_commands = &submitted;
    f.host.hooks_.sendH4 = [](const uint8_t* p, size_t n, void* user) {
        auto& fixture = *static_cast<Fixture*>(user);
        fixture.tx.emplace_back(p, p + n);
        if (n >= 4 && p[0] == 1) submitted_commands->push_back(bth_read_le16(p + 1));
        return true;
    };
    f.event(4, {1,2,3,4,5,6,0,0,0,1}); f.settle();
    /* Valid remote milestones can arrive while the local low-priority
       Authentication Requested command is still queued. */
    f.event(3, {0,0x25,0,1,2,3,4,5,6,1,0});
    f.event(0x17, {1,2,3,4,5,6});
    f.event(0x31, {1,2,3,4,5,6});
    f.event(0x33, {1,2,3,4,5,6,1,0,0,0});
    if (!ordering) f.event(0x36, {0,1,2,3,4,5,6});
    Bytes key = {1,2,3,4,5,6}; key.insert(key.end(), 16, 0xaa); key.push_back(4);
    f.event(0x18, key);
    if (ordering) f.event(0x36, {0,1,2,3,4,5,6});
    CHECK(!bth_links_secure(&f.host.links_, 0x25));
    f.event(8, {0,0x25,0,1});
    CHECK(!bth_links_secure(&f.host.links_, 0x25));
    f.settle();
    CHECK(f.host.active_ && f.host.radioStats().transport_errors == 0);
    CHECK(bth_links_secure(&f.host.links_, 0x25) && f.host.bonds_[0].used);
    CHECK(f.host.bleStats().connected);
    unsigned auth_requests = 0, size_reads = 0;
    for (uint16_t op : submitted) { auth_requests += op == 0x0411; size_reads += op == 0x1408; }
    CHECK(auth_requests == 0 && size_reads == 1);

    const auto signal = f.channel(0x19, 0x81);
    f.output.clear();
    f.acl(0x25, signal, {0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53}); f.settle();
    CHECK(f.configured == 1 && !f.output.empty() && f.output.back().bytes == Bytes({0x12,3}));
    f.acl(0x25, signal, {0x20,6,4}); f.settle();
    const auto media = f.channel(0x19, 0x82);
    f.acl(0x25, signal, {0x30,7,4}); f.settle();
    CHECK(f.playing && f.host.radioStats().streaming && f.configured == 2);
    const Bytes packet = {0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa};
    f.acl(0x25, media, packet); f.settle();
    CHECK(f.media == 1 && f.last_media == packet);
    f.acl(0x26, 4, {0x12,9,0,0x6b}); f.settle();
    BleWrite write;
    CHECK(f.host.bleReadWrite(write) && write.length == 1 && write.data[0] == 0x6b);
    CHECK(f.host.radioStats().streaming && f.host.bleStats().connected);
    f.host.end();
    submitted_commands = nullptr;
}
int main() {
    normal_responder_reaches_profiles(0);
    normal_responder_reaches_profiles(1);
    printf("NORMAL_SSP_HOST_PASS checks=%u synthetic_only=1 physical_pairing_unproven=1\n", checks);
    return 0;
}
