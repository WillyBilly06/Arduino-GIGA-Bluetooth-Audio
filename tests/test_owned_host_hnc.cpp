// Reuse the maintained actual-actor fixture; inspect credit/owner bookkeeping
// while driving all completions through the production HCI receive boundary.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_fixture_main
#include "test_owned_host.cpp"
#undef main
#undef CHECK
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);exit(1); } } while(0)

static void pendingReply(Fixture& f) {
    f.boot();
    f.leConnect();
    f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,0xf4,1});
    for(unsigned i=0;i<30;++i) {
        f.poll();
        while(!f.tx.empty()) {
            auto packet=f.pop();
            if(packet[0]==1 && Fixture::op(packet)==0x2020) {
                CHECK(f.host.flow_.command_credits==0);
                CHECK(f.host.commands_.sent_count==1);
                return;
            }
            f.acknowledge(packet);
        }
    }
    CHECK(false);
}
static unsigned receipts(const Fixture& f) {
    unsigned count=0;
    for(const auto& line:f.messages)
        if(line.find("Owned HNC completion ")==0) ++count;
    return count;
}
static void rejectedCompletionIsTerminal() {
    Fixture f;
    pendingReply(f);
    // Exact hardware-observed rejection shape, with an exaggerated grant to
    // expose corruption of the unrelated outstanding command's credits.
    f.complete(0x0c35,{0x12},7);
    CHECK(f.host.radioStats().transport_errors==1);
    CHECK(!f.host.radioStats().controller_ready);
    CHECK(f.host.flow_.command_credits==0);
    CHECK(receipts(f)==1);
}
static void nonzeroAndMalformed() {
    for(bool cs:{false,true}) {
        for(uint8_t status:{uint8_t(1),uint8_t(0x12),uint8_t(255)}) {
            Fixture f;
            pendingReply(f);
            if(cs) f.event(15,{status,255,0x35,0x0c});
            else f.complete(0x0c35,{status},255);
            CHECK(f.host.radioStats().transport_errors==1);
            CHECK(!f.host.radioStats().controller_ready);
            CHECK(f.host.flow_.command_credits==0 && receipts(f)==1);
            for(const auto& line:f.messages) {
                if(line.find("Owned HNC completion ")!=0) continue;
                CHECK(line.size()<128);
                CHECK(line.find("op=0c35")!=std::string::npos);
                CHECK(line.find("malformed=0")!=std::string::npos);
                CHECK(line.find("credit_kept=0")!=std::string::npos);
            }
            for(unsigned i=0;i<20;++i) {
                f.complete(0x0c35,{status},255);
                f.poll();
            }
            CHECK(receipts(f)==1 && f.host.radioStats().transport_errors==1);
        }
    }
    for(unsigned bytes:{0u,2u,3u,252u}) {
        Fixture f;
        pendingReply(f);
        f.complete(0x0c35,Bytes(bytes,0),17);
        CHECK(f.host.radioStats().transport_errors==1);
        CHECK(f.host.flow_.command_credits==0 && receipts(f)==1);
        bool malformed=false;
        for(const auto& line:f.messages)
            if(line.find("Owned HNC completion ")==0 && line.find("malformed=1")!=std::string::npos) malformed=true;
        CHECK(malformed);
    }
    {
        Fixture f;
        pendingReply(f);
        f.event(15,{0,7,0x35,0x0c,0});
        CHECK(f.host.radioStats().transport_errors==1 && f.host.flow_.command_credits==0);
    }
    // Too-short layouts cannot identify HNC, but must still fail existing
    // generic completion/H4 validation without applying a command grant.
    for(unsigned n=0;n<3;++n) {
        Fixture f;
        pendingReply(f);
        Bytes p={7,0x35};p.resize(n);
        f.event(14,p);
        CHECK(f.host.radioStats().transport_errors==1 && f.host.flow_.command_credits==0);
    }
    {
        Fixture f;
        pendingReply(f);
        uint8_t event[]={14,4,7,0x35,0x0c};
        f.host.receive(4,event,sizeof event,f.now);
        CHECK(f.host.radioStats().transport_errors==1 && f.host.flow_.command_credits==0);
    }
}
static void successPreservesNormalCreditsAndOwner() {
    for(uint8_t initial:{uint8_t(0),uint8_t(1),uint8_t(3)}) {
        for(uint8_t reported:{uint8_t(0),uint8_t(1),uint8_t(255)}) {
            for(bool cs:{false,true}) {
                Fixture f;
                pendingReply(f);
                f.host.flow_.command_credits=initial;
                const auto owner=f.host.commands_.sent[0].owner;
                if(cs) f.event(15,{0,reported,0x35,0x0c});
                else f.complete(0x0c35,{0},reported);
                CHECK(!f.host.radioStats().transport_errors && f.host.radioStats().controller_ready);
                CHECK(f.host.flow_.command_credits==initial && receipts(f)==0);
                CHECK(f.host.commands_.sent_count==1);
                CHECK(f.host.commands_.sent[0].opcode==0x2020 && f.host.commands_.sent[0].owner==owner);
                f.complete(0x2020,{0,0x26,0},2);
                CHECK(f.host.flow_.command_credits==2 && f.host.commands_.sent_count==0);
                f.settle();
                CHECK(f.host.bleStats().connected && !f.host.radioStats().transport_errors);
            }
        }
    }
}
static void bootAndDeferredPressure() {
    for(bool failure:{false,true}) {
        Fixture f;
        f.poll();
        CHECK(Fixture::op(f.pop())==0x0c03 && f.host.flow_.command_credits==0);
        f.complete(0x0c35,{uint8_t(failure?0x12:0)},255);
        CHECK(f.host.flow_.command_credits==0);
        if(failure) CHECK(f.host.radioStats().transport_errors==1);
        else {
            CHECK(f.host.pending_ && f.host.pending_opcode_==0x0c03);
            f.complete(0x0c03,{0},2);
            CHECK(f.host.flow_.command_credits==2);
        }
    }
    for(bool failure:{false,true}) {
        Fixture f;
        pendingReply(f);
        for(unsigned i=0;i<BTH_CMD_QUEUE_LIMIT;++i)
            CHECK(bth_cmdq_push(&f.host.commands_,0x0c16,nullptr,0,false)==BTH_OK);
        f.event(4,{1,2,3,4,5,6,0,0,0,1});
        CHECK(f.host.deferred_count_==1 && bth_cmdq_available(&f.host.commands_)==0);
        f.complete(0x0c35,{uint8_t(failure?0x12:0)},255);
        CHECK(f.host.flow_.command_credits==0);
        if(failure) CHECK(f.host.radioStats().transport_errors==1 && receipts(f)==1);
        else {
            CHECK(!f.host.radioStats().transport_errors && receipts(f)==0);
            CHECK(f.host.deferred_count_==1 && f.host.commands_.sent_count==1);
            CHECK(bth_cmdq_available(&f.host.commands_)==0);
        }
    }
}
int main() {
    rejectedCompletionIsTerminal();
    nonzeroAndMalformed();
    successPreservesNormalCreditsAndOwner();
    bootAndDeferredPressure();
    printf("Owned HNC error/normal-credit/owner/bounds/deferred regressions %u checks passed\n",checks);
}
