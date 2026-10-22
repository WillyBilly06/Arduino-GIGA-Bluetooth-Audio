#include "owned/LeProfiles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace giga_bredr;
using namespace giga_bredr::detail;
static unsigned checks;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); std::abort(); } ++checks; } while (0)

struct Fixture {
    bth_cmdq_t commands;
    bth_control_tx_t replies;
    LeProfiles ble;
    Fixture() {
        bth_cmdq_init(&commands);
        bth_control_tx_init(&replies);
    }
    void begin(uint16_t mtu=517) { BleConfig c;c.maximum_mtu=mtu;CHECK(ble.begin(c,"GIGA owned BLE",commands,replies)==Status::Ok); }
    void connect(uint32_t generation=7,uint16_t handle=0x42) {
        CHECK(bth_control_tx_attach(&replies,1,handle,generation));
        CHECK(ble.attach(generation,handle));
    }
    const bth_control_slot_t& nextReply() const {
        CHECK(replies.peers[1].count!=0);
        return replies.slots[replies.peers[1].order[replies.peers[1].read]];
    }
    void clearReplies() {
        const uint32_t owner=ble.stats().generation;
        const uint16_t handle=ble.stats().connection_handle;
        CHECK(bth_control_tx_detach(&replies,1,owner));
        CHECK(bth_control_tx_attach(&replies,1,handle,owner));
    }
    void fillReplies() {
        while (bth_control_tx_available(&replies,1)) {
            bth_control_reservation_t slot;
            CHECK(bth_control_tx_reserve(&replies,1,&slot)==BTH_OK);
            slot.data[0]=0x13;
            CHECK(bth_control_tx_commit(&replies,slot.token,4,1,23)==BTH_OK);
        }
    }
};

static void initialization_and_generation() {
    Fixture f; f.begin();
    CHECK(f.ble.stats().enabled && !f.ble.stats().connected);
    CHECK(f.ble.stats().att_mtu==23 && !f.ble.stats().security_available);
    CHECK(!f.ble.attach(0,0x42));
    CHECK(!f.ble.attach(7,0x42,0)); // Peripheral-only attachment is explicit.
    f.connect();
    CHECK(f.ble.stats().connected && f.ble.stats().generation==7);
    const uint8_t readName[]={0x0a,3,0};
    CHECK(f.ble.receive(6,4,readName,sizeof readName,0));
    CHECK(f.replies.used==0); // An old generation cannot mutate or answer.
    CHECK(f.ble.receive(7,4,readName,sizeof readName,0));
    const auto& reply=f.nextReply();
    CHECK(reply.cid==4 && reply.length==15 && reply.data[0]==0x0b);
    CHECK(std::memcmp(reply.data+1,"GIGA owned BLE",14)==0);
    f.ble.detach(6); CHECK(f.ble.stats().connected);
    f.ble.detach(7); CHECK(!f.ble.stats().connected);
}

static void copied_write_and_capacity_retry() {
    Fixture f; f.begin(); f.connect();
    uint8_t write[]={0x12,9,0,1,2,3};
    CHECK(f.ble.receive(7,4,write,sizeof write,10));
    CHECK(f.nextReply().length==1 && f.nextReply().data[0]==0x13);
    write[3]=99;
    const uint8_t second[]={0x12,9,0,4};
    CHECK(f.ble.receive(7,4,second,sizeof second,11) && f.ble.pendingAtt());
    CHECK(f.replies.used==1); // Retry has no second response or write effect.
    BleWrite value;
    CHECK(f.ble.readWrite(value));
    CHECK(value.generation==7 && value.length==3 && value.data[0]==1 && value.data[2]==3);
    CHECK(!f.ble.readWrite(value));
    f.ble.servicePendingAtt(7,12); CHECK(!f.ble.pendingAtt());
    CHECK(f.ble.readWrite(value) && value.length==1 && value.data[0]==4);
}

static void notification_read_subscription_and_mtu() {
    Fixture f; f.begin(247); f.connect();
    uint8_t value[]={1,2,3};
    CHECK(f.ble.notify(value,sizeof value)==Status::Ok);
    value[0]=99; f.ble.poll(0,false); CHECK(f.replies.used==0);
    const uint8_t read[]={0x0a,11,0};
    CHECK(f.ble.receive(7,4,read,sizeof read,0));
    CHECK(f.nextReply().length==4 && f.nextReply().data[0]==0x0b && f.nextReply().data[1]==1);
    f.clearReplies();
    const uint8_t subscribe[]={0x12,12,0,1,0};
    CHECK(f.ble.receive(7,4,subscribe,sizeof subscribe,0));
    CHECK(f.ble.stats().notify_enabled); f.clearReplies();
    CHECK(f.ble.notify(value,sizeof value)==Status::Ok);
    value[0]=42;
    CHECK(f.ble.notify(value,sizeof value)==Status::Busy);
    f.ble.poll(0,false);
    CHECK(f.nextReply().cid==4 && f.nextReply().length==6 && f.nextReply().data[0]==0x1b);
    CHECK(f.nextReply().data[1]==11 && f.nextReply().data[3]==99);
    CHECK(f.ble.stats().notifications_queued==1); f.clearReplies();
    uint8_t large[244]={};
    CHECK(f.ble.notify(large,sizeof large)==Status::InvalidArgument); // Initial MTU23.
    const uint8_t mtu[]={2,5,2}; // Peer offers517; local bound247 wins.
    CHECK(f.ble.receive(7,4,mtu,sizeof mtu,1));
    CHECK(f.nextReply().length==3 && f.nextReply().data[0]==3 && f.nextReply().data[1]==247 && f.nextReply().data[2]==0);
    CHECK(f.ble.stats().att_mtu==247); f.clearReplies();
    CHECK(f.ble.notify(large,sizeof large)==Status::Ok);
    f.fillReplies(); f.ble.poll(1,false);
    CHECK(f.ble.stats().notifications_queued==1); // Full shared queue keeps copied notification.
    f.clearReplies(); f.ble.poll(2,false);
    CHECK(f.nextReply().length==247 && f.ble.stats().notifications_queued==2);
}

static void tx_capacity_and_reconnect_reset() {
    Fixture f; f.begin(); f.connect(); f.fillReplies();
    const uint8_t request[]={0x12,9,0,9};
    CHECK(f.ble.receive(7,4,request,sizeof request,0) && f.ble.pendingAtt());
    BleWrite value; CHECK(!f.ble.readWrite(value));
    CHECK(f.ble.stats().write_command_drops==0); // Requests still wait for a reply slot.
    const uint8_t write[]={0x52,9,0,7},newer[]={0x52,9,0,8};
    CHECK(f.ble.receive(7,4,write,sizeof write,0));
    CHECK(f.replies.used==3); // No-response commands require no TX reservation.
    CHECK(f.ble.receive(7,4,request,sizeof request,0)); // Pipelined request cannot replace the older copy.
    CHECK(f.ble.receive(7,4,newer,sizeof newer,0));
    CHECK(f.ble.stats().writes==0 && f.ble.stats().write_command_drops==2);
    CHECK(!f.ble.readWrite(value)); // Older TX-blocked request wins even with empty app slot.
    f.clearReplies(); f.ble.servicePendingAtt(7,1);
    CHECK(!f.ble.pendingAtt() && f.ble.readWrite(value) && value.length==1 && value.data[0]==9);
    f.clearReplies(); CHECK(f.ble.receive(7,4,write,sizeof write,1));
    CHECK(f.replies.used==0); // Write commands have no ATT response.
    const uint8_t subscribe[]={0x12,12,0,1,0};
    CHECK(f.ble.receive(7,4,subscribe,sizeof subscribe,0));
    f.ble.detach(7); CHECK(!f.ble.readWrite(value));
    CHECK(bth_control_tx_detach(&f.replies,1,7));
    f.connect(8,0x42);
    CHECK(!f.ble.stats().notify_enabled && f.ble.stats().att_mtu==23);
    CHECK(f.ble.receive(7,4,write,sizeof write,0)); CHECK(!f.ble.readWrite(value));
    CHECK(f.ble.stats().write_command_drops==2); // Cumulative host-lifetime counter.
}

static void explicit_pairing_rejection_and_signaling() {
    Fixture f; f.begin(); f.connect(); f.fillReplies();
    const uint8_t pair[]={1,3,0,1,16,7,7};
    CHECK(!f.ble.receive(7,6,pair,sizeof pair,0));
    f.clearReplies(); CHECK(f.ble.receive(7,6,pair,sizeof pair,0));
    CHECK(f.nextReply().cid==6 && f.nextReply().length==2 && f.nextReply().data[0]==5 && f.nextReply().data[1]==5);
    CHECK(!f.ble.stats().security_available && !f.ble.stats().encrypted); f.clearReplies();
    const uint8_t failed[]={5,5};
    CHECK(f.ble.receive(7,6,failed,sizeof failed,0) && f.replies.used==0);
    const uint8_t unknown[]={0x14,3,0,0};
    CHECK(f.ble.receive(7,5,unknown,sizeof unknown,0));
    const uint8_t reject[]={1,3,2,0,0,0};
    CHECK(f.nextReply().cid==5 && f.nextReply().length==6 && std::memcmp(f.nextReply().data,reject,6)==0);
    f.clearReplies(); const uint8_t malformed[]={0x14,3,1,0};
    CHECK(f.ble.receive(7,5,malformed,sizeof malformed,0) && f.replies.used==0);
    CHECK(f.ble.receive(7,4,nullptr,0,0) && f.ble.stats().malformed_pdus!=0);
}

static void advertising_setup_and_owner_retirement() {
    Fixture f; f.begin(); bth_hci_flow_t flow; bth_hci_flow_init(&flow);
    const uint16_t opcodes[]={0x200a,0x2006,0x2008,0x2009,0x200a};
    uint8_t wire[260]; size_t length=0;
    for (size_t i=0;i<5;++i) {
        f.ble.poll(static_cast<uint32_t>(i*10),true);
        CHECK(bth_cmdq_next(&f.commands,&flow,wire,sizeof wire,&length)==BTH_OK);
        CHECK(bth_read_le16(wire+1)==opcodes[i]);
        if (i==0) CHECK(wire[4]==0);
        if (i==1) CHECK(length==19 && wire[4]==128 && wire[6]==0 && wire[7]==1 && wire[17]==7);
        if (i==2) CHECK(length==36 && wire[4]==21 && wire[5]==2 && wire[6]==1 && wire[7]==2 && wire[9]==7);
        if (i==3) CHECK(length==36 && wire[4]==16 && wire[6]==9);
        if (i==4) CHECK(wire[4]==1);
        const uint8_t complete[]={1,static_cast<uint8_t>(opcodes[i]),static_cast<uint8_t>(opcodes[i]>>8),0};
        bth_h4_frame_t event={}; event.type=BTH_H4_EVENT; event.event={0x0e,{complete,sizeof complete}};
        const uint8_t sentBefore=f.commands.sent_count;
        f.ble.event(event); CHECK(f.commands.sent_count==sentBefore && flow.command_credits==0);
        // Global advertising commands use owner0 and create no per-link tombstone.
        uint32_t owner=99; CHECK(bth_cmdq_complete(&f.commands,&event,&owner)==BTH_IGNORED && owner==0);
        CHECK(bth_hci_flow_on_event(&flow,&event)==BTH_OK);
    }
    CHECK(f.ble.stats().advertising);
    f.ble.poll(60,true); CHECK(f.commands.used==0);
    f.connect(); CHECK(!f.ble.stats().advertising);
    f.ble.poll(70,true); CHECK(f.commands.used==0);
}

static void deferred_ltk_reply_and_bad_parameters() {
    Fixture f; f.begin(); f.connect();
    const uint8_t ltk[]={5,0x42,0,0,0,0,0,0,0,0,0,0,0};
    bth_h4_frame_t event={}; event.type=BTH_H4_EVENT; event.event={0x3e,{ltk,sizeof ltk}};
    while (bth_cmdq_available(&f.commands)) CHECK(bth_cmdq_push(&f.commands,0xfc01,nullptr,0,false)==BTH_OK);
    f.ble.event(event); f.ble.poll(0,false); // Deferred; cannot drop security reply.
    bth_hci_flow_t flow; bth_hci_flow_init(&flow); uint8_t wire[260]; size_t length=0;
    for(unsigned i=0;i<3;++i){
        CHECK(bth_cmdq_next(&f.commands,&flow,wire,sizeof wire,&length)==BTH_OK);
        const uint8_t complete[]={1,1,0xfc,0};bth_h4_frame_t ack={};ack.type=BTH_H4_EVENT;ack.event={0x0e,{complete,sizeof complete}};
        CHECK(bth_hci_flow_on_event(&flow,&ack)==BTH_OK);
    }
    f.ble.poll(1,false);
    bool found=false;
    for (const auto& command:f.commands.slots) {
        if (command.owner==7 && command.opcode==0x201b) {
            CHECK(command.len==2 && command.params[0]==0x42 && command.params[1]==0); found=true;
        }
    }
    CHECK(found && !f.ble.stats().security_available);
    // A malformed LE request never gains a reply by reusing stale bytes.
    event.event.params.len=12; f.ble.event(event);
    CHECK(f.ble.stats().malformed_pdus==1);
    const uint8_t encrypted[]={0,0x42,0,1}; event.event={8,{encrypted,sizeof encrypted}};
    f.ble.event(event); CHECK(!f.ble.stats().encrypted);
}

static void configuration_validation_and_disabled() {
    Fixture f; BleConfig config; config.maximum_mtu=518;
    CHECK(f.ble.begin(config,"valid",f.commands,f.replies)==Status::InvalidArgument);
    config.maximum_mtu=22; CHECK(f.ble.begin(config,"valid",f.commands,f.replies)==Status::InvalidArgument);
    config.maximum_mtu=247; config.advertising_interval_min=1;
    CHECK(f.ble.begin(config,"valid",f.commands,f.replies)==Status::InvalidArgument);
    config.advertising_interval_min=128; config.enabled=false;
    CHECK(f.ble.begin(config,"valid",f.commands,f.replies)==Status::Ok);
    f.ble.poll(0,true); CHECK(f.commands.used==0 && !f.ble.stats().enabled);
    CHECK(!f.ble.attach(7,0x42));
    CHECK(f.ble.notify(nullptr,0)==Status::Unsupported);
}

static void encryption_policy_fails_closed() {
    Fixture f; BleConfig config;
    // A future security policy must never silently publish the public service.
    config.security=BleSecurityPolicy::EncryptedPairing;
    CHECK(f.ble.begin(config,"secure",f.commands,f.replies)==Status::Unsupported);
    f.ble.poll(0,true); CHECK(f.commands.used==0 && !f.ble.stats().enabled);
}

static void prepared_write_and_permission_errors() {
    Fixture f; f.begin(); f.connect();
    const uint8_t prepare1[]={0x16,9,0,0,0,10,20};
    CHECK(f.ble.receive(7,4,prepare1,sizeof prepare1,0));
    CHECK(f.nextReply().data[0]==0x17); f.clearReplies();
    const uint8_t prepare2[]={0x16,9,0,2,0,30,40};
    CHECK(f.ble.receive(7,4,prepare2,sizeof prepare2,0)); f.clearReplies();
    const uint8_t execute[]={0x18,1};
    CHECK(f.ble.receive(7,4,execute,sizeof execute,0));
    CHECK(f.nextReply().length==1 && f.nextReply().data[0]==0x19); f.clearReplies();
    BleWrite value; CHECK(f.ble.readWrite(value));
    CHECK(value.length==4 && value.data[0]==10 && value.data[3]==40);
    CHECK(f.ble.receive(7,4,execute,sizeof execute,0));
    CHECK(!f.ble.readWrite(value)); f.clearReplies(); // Prepared action was released.
    const uint8_t noIndications[]={0x12,12,0,2,0};
    CHECK(f.ble.receive(7,4,noIndications,sizeof noIndications,0));
    CHECK(f.nextReply().length==5 && f.nextReply().data[0]==1 && f.nextReply().data[4]==19);
    CHECK(!f.ble.stats().notify_enabled); f.clearReplies();
    const uint8_t writeTx[]={0x12,11,0,1};
    CHECK(f.ble.receive(7,4,writeTx,sizeof writeTx,0));
    CHECK(f.nextReply().data[4]==3); // TX is read/notify only.
}

static void shortened_name_preserves_utf8_boundary() {
    Fixture f; char name[32]; std::memset(name,'a',28);
    name[28]=char(0xe2);name[29]=char(0x82);name[30]=char(0xac);name[31]=0;
    CHECK(f.ble.begin(BleConfig{},name,f.commands,f.replies)==Status::Ok);
    bth_hci_flow_t flow;bth_hci_flow_init(&flow);
    for(unsigned i=0;i<4;++i) {
        f.ble.poll(i*10,true); uint8_t wire[260];size_t n=0;
        CHECK(bth_cmdq_next(&f.commands,&flow,wire,sizeof wire,&n)==BTH_OK);
        uint16_t opcode=bth_read_le16(wire+1);
        if(opcode==0x2009) {
            CHECK(wire[4]==30 && wire[5]==29 && wire[6]==8);
            CHECK(std::memcmp(wire+7,name,28)==0);return;
        }
        uint8_t complete[]={1,uint8_t(opcode),uint8_t(opcode>>8),0};
        bth_h4_frame_t event={};event.type=BTH_H4_EVENT;event.event={0x0e,{complete,sizeof complete}};
        f.ble.event(event);uint32_t owner=0;bth_cmdq_complete(&f.commands,&event,&owner);bth_hci_flow_on_event(&flow,&event);
    }
    CHECK(false);
}
int main() {
    shortened_name_preserves_utf8_boundary();
    initialization_and_generation();
    copied_write_and_capacity_retry();
    notification_read_subscription_and_mtu();
    tx_capacity_and_reconnect_reset();
    explicit_pairing_rejection_and_signaling();
    advertising_setup_and_owner_retirement();
    deferred_ltk_reply_and_bad_parameters();
    configuration_validation_and_disabled();
    encryption_policy_fails_closed();
    prepared_write_and_permission_errors();
    std::printf("PASS owned BLE: %u checks\n",checks);
}
