// Actual production adapter + actor, with native hardware declarations only.
// The shared fixture binds to Context.host; no adapter source is extracted.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_fixture_main
#include "test_owned_host.cpp"
#undef main
#include "../src/platform/OwnedRadio.cpp"
// These link-only symbols are unused: the fixture supplies synthetic HCD to
// the portable actor; board startup and exact HCD are verified separately.
extern "C" {
extern const uint8_t brcm_patchram_buf[35182]={};
extern const uint32_t brcm_patch_ram_length=35182;
extern const char brcm_patch_version[]="BCM4343A1_NATIVE_UNUSED";
extern const uint8_t brcm_patchram_format=1;
}
namespace radio=giga_bredr::detail;
struct Adapter {
 radio::Context* state;
 Fixture app;
 static radio::Context* make(){assert(!radio::context);auto* p=static_cast<radio::Context*>(radio::allocate(sizeof(radio::Context)));assert(p);new(p)radio::Context();radio::context=p;radio::stopped_stats={};radio::stopped_ble={};p->uart=new(p->uart_storage)mbed::UnbufferedSerial(PF_7,PA_8,115200);return p;}
 explicit Adapter(bool ble=true):state(make()),app(ble,&state->host){state->callbacks=app.config.callbacks;}
 ~Adapter(){if(radio::context)radio::radioEnd();assert(!owned_radio_test::critical_depth);}
 void poll(unsigned add=1){app.now+=add;owned_radio_test::now_ms=app.now;radio::radioPoll();}
 void queue(uint8_t type,const Bytes& bytes){auto put=[&](uint8_t b){state->rx[state->rx_write]=b;state->rx_write=(state->rx_write+1)&(sizeof(state->rx)-1);};put(type);for(auto b:bytes)put(b);}
};
static void first(Fixture& f){Bytes data={0,2,4,0};data.resize(27,0xaa);f.rawAcl(0x26,2,data);}
static void continuation(Fixture& f){f.rawAcl(0x26,1,Bytes(27,0xaa));}
static void control(Fixture& f){bth_control_reservation_t slot;CHECK(bth_control_tx_reserve(&f.host.control_,1,&slot)==BTH_OK);slot.data[0]=0x13;CHECK(bth_control_tx_commit(&f.host.control_,slot.token,4,1,517)==BTH_OK);}
static void queuedContinuation(Adapter& a){Bytes data={0x26,0x10,27,0};data.resize(31,0xaa);a.queue(2,data);}
static void adapterCreditBeforeRx(){Adapter a;auto& f=a.app;f.boot();f.leConnect();first(f);f.poll();CHECK(Fixture::op(f.pop())==0x0c35&&f.host.last_ack_);f.ready=false;for(unsigned i=0;i<4;++i){continuation(f);f.poll();}CHECK(f.host.rx_.outstanding==4&&f.host.rx_.links[1].pending==4);control(f);f.ready=true;queuedContinuation(a);a.poll();CHECK(radio::radioStats().transport_errors==0&&radio::context==a.state&&a.state->rx_read==a.state->rx_write);CHECK(!f.tx.empty()&&Fixture::op(f.tx[0])==0x0c35&&bth_read_le16(f.tx[0].data()+7)==4);}

static void queuedCompletionBeforeDeadline(){
 Adapter a;auto& f=a.app;f.boot();f.classicConnect();f.leConnect();
 CHECK(bth_cmdq_push_owned(&f.host.commands_,0x0c16,nullptr,0,false,f.host.peers_[0].owner)==BTH_OK);f.poll();CHECK(Fixture::op(f.pop())==0x0c16&&f.host.commands_.sent_count==1);
 f.ready=false;f.acl(0x26,4,{0x12,9,0,1});f.poll();CHECK(f.host.rx_.links[1].pending==1);f.ready=true;
 a.queue(4,{0x0e,4,1,0x16,0x0c,0});a.poll(5001);
 if(!radio::context||radio::radioStats().transport_errors){for(const auto& line:f.messages)fprintf(stderr,"diagnostic: %s\n",line.c_str());}
 CHECK(radio::context==a.state&&radio::radioStats().transport_errors==0&&f.host.commands_.sent_count==0&&a.state->rx_read==a.state->rx_write);
 CHECK(!f.tx.empty()&&Fixture::op(f.tx[0])==0x0c35&&bth_read_le16(f.tx[0].data()+7)==1);
}
static void urgentInsideRxIteration(){
 Adapter a;auto& f=a.app;f.boot();f.leConnect();first(f);f.poll();CHECK(Fixture::op(f.pop())==0x0c35&&f.host.last_ack_);f.ready=false;
 for(unsigned i=0;i<3;++i){continuation(f);f.poll();}control(f);f.ready=true;
 // Models TX becoming available during a burst, after the pre-RX opportunity.
 f.poll();CHECK(f.tx.size()==1&&f.tx[0][0]==1&&Fixture::op(f.tx[0])==0x0c35&&bth_read_le16(f.tx[0].data()+7)==3);
}
static void threeBorrowedSlotsAreNotCredits(){
 Adapter a;auto& f=a.app;f.boot();f.leConnect();
 // ATT now owns delayed requests independently. Keep this negative on a real
 // non-ATT borrowed SDU: a full LE reply queue blocks an SMP rejection.
 for(unsigned i=0;i<3;++i)control(f);
 f.ready=false;
 for(unsigned i=0;i<3;++i){f.acl(0x26,6,{1});f.poll(0);}
 CHECK(f.host.rx_.outstanding==3&&f.host.rx_.links[1].pending==0&&f.host.peers_[1].has_pending&&f.tx.empty());f.ready=true;a.poll();
 CHECK(f.tx.size()==1&&f.tx[0][0]==2&&f.host.rx_.outstanding==3&&f.host.rx_.links[1].pending==0&&radio::radioStats().transport_errors==0);
}
static void creditServiceGates(){
 {Fixture f(false);f.host.serviceReceiveCredits(f.now+60001);CHECK(f.tx.empty()&&!f.host.radioStats().controller_ready&&!f.host.radioStats().transport_errors);f.host.end();f.host.serviceReceiveCredits(f.now+70000);CHECK(f.tx.empty());}
 {Adapter a;auto& f=a.app;f.boot();f.classicConnect();f.leConnect();f.ready=false;first(f);f.poll();auto stopped=f.stops;
 f.host.serviceReceiveCredits(f.now+60001);CHECK(f.tx.empty()&&f.host.rx_.links[1].pending==1&&f.stops==stopped&&!f.host.radioStats().transport_errors);
 for(unsigned i=0;i<7;++i)CHECK(bth_cmdq_push(&f.host.commands_,0x0c16,nullptr,0,false)==BTH_OK);f.event(8,{0,0x25,0,0});CHECK(f.host.deferred_count_==1);f.ready=true;
 f.host.serviceReceiveCredits(f.now+1);CHECK(f.tx.empty()&&f.host.deferred_count_==1&&f.host.rx_.links[1].pending==1&&f.stops==stopped);
 }
}
static void generationsAndNoCommandGrant(){
 Adapter a;auto& f=a.app;f.boot();f.leConnect();auto owner=f.host.peers_[1].owner;f.ready=false;first(f);f.poll();f.event(5,{0,0x26,0,0x13});f.ready=true;
 f.host.serviceReceiveCredits(f.now+1);CHECK(f.tx.empty()&&f.host.rx_.outstanding==0);f.settle();f.leConnect();CHECK(f.host.peers_[1].owner!=owner);
 f.ready=false;first(f);f.poll();f.ready=true;f.host.flow_.command_credits=0;f.host.serviceReceiveCredits(f.now+1);
 CHECK(f.tx.size()==1&&Fixture::op(f.tx[0])==0x0c35&&bth_read_le16(f.tx[0].data()+5)==0x26&&bth_read_le16(f.tx[0].data()+7)==1&&f.host.flow_.command_credits==0);
}
static void terminalSubmissionAndAdapterGuards(){
 {Adapter a;auto& f=a.app;f.boot();f.leConnect();f.ready=false;first(f);f.poll();f.ready=true;f.fail_send=true;a.queue(4,{0x0e,4,1,0,0,0});a.poll();CHECK(!radio::context&&radio::radioStats().transport_errors==1);}
 for(bool timeout:{false,true}){Adapter a;auto& f=a.app;f.boot();f.leConnect();f.ready=false;first(f);f.poll();f.ready=true;
  if(timeout){const uint8_t packet[]={1,0,0,0};CHECK(a.state->tx.submit(packet,sizeof packet));a.state->tx_started=f.now-3001;}else a.state->tx_failed=1;
  a.poll();CHECK(!radio::context&&radio::radioStats().transport_errors==1&&f.tx.empty());
 }
}
#ifndef OWNED_RADIO_SERVICE_MAIN
#define OWNED_RADIO_SERVICE_MAIN main
#endif
int OWNED_RADIO_SERVICE_MAIN(int argc,char** argv){if(argc==2){if(!strcmp(argv[1],"pre-rx"))adapterCreditBeforeRx();else if(!strcmp(argv[1],"urgent"))urgentInsideRxIteration();else if(!strcmp(argv[1],"deadline"))queuedCompletionBeforeDeadline();else CHECK(false);return 0;}adapterCreditBeforeRx();queuedCompletionBeforeDeadline();urgentInsideRxIteration();threeBorrowedSlotsAreNotCredits();creditServiceGates();generationsAndNoCommandGrant();terminalSubmissionAndAdapterGuards();printf("Owned actual adapter/actor receive-credit service %u checks passed\n",checks);return 0;}
