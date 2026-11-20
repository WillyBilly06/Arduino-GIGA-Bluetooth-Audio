// Real OwnedHost actor + Connectionless Peripheral Broadcast endpoint: default
// off identity, the transmitter/receiver HCI sequences after boot, event
// routing, a Classic link alongside, host restart after HCI Reset and the
// BLE-only rejection. Synthetic HCI fixture; no radio.
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#include "../src/owned/OwnedHost.cpp"
#define main original_host_fixture_main
#include "test_owned_host.cpp"
#undef main

static bool cpbOpcode(uint16_t op) {
 return op==0x0c63||op==0x0c74||op==0x0c75||op==0x0c76||op==0x0c78||op==0x0441||op==0x0442||op==0x0443||op==0x0444;
}
struct Sent { uint16_t opcode; Bytes params; };
// Controller model for CPB commands: success with the specified return
// parameters; every other packet takes the fixture's normal acknowledgement.
static uint16_t fail_once=0;
static bool answerCpb(Fixture& f,const Bytes& p,std::vector<Sent>& sent){
 if(p[0]!=1||!cpbOpcode(Fixture::op(p)))return false;
 const uint16_t op=Fixture::op(p);CHECK(p.size()==size_t(4+p[3]));Bytes q(p.begin()+4,p.end());sent.push_back({op,q});
 if(op==fail_once){fail_once=0;f.complete(op,{0x12});return true;}
 if(op==0x0443||op==0x0444)f.status(op,0);
 else if(op==0x0441)f.complete(op,{0,q[1],0x64,0});
 else if(op==0x0c74||op==0x0c75||op==0x0c76)f.complete(op,{0,q[0]});
 else if(op==0x0c78)f.complete(op,{0,0xc0,0});
 else if(op==0x0442){Bytes r={0};r.insert(r.end(),q.begin()+1,q.begin()+8);f.complete(op,r);}
 else f.complete(op,{0});
 return true;
}
static void pump(Fixture& f,std::vector<Sent>& sent,unsigned turns=40){
 for(unsigned i=0;i<turns;++i){f.poll();
  while(!f.tx.empty()){Bytes p=f.pop();if(!answerCpb(f,p,sent))f.acknowledge(p);}
 }
}
// Fixture::boot() answers every command generically, including the first
// CPB command once GAP is ready; route those to the CPB model instead.
static void boot(Fixture& f,std::vector<Sent>& sent){
 for(unsigned count=0;!f.host.radioStats().controller_ready;++count){CHECK(count<1400);f.poll();
  if(!f.tx.empty()){Bytes p=f.pop();if(!answerCpb(f,p,sent)){const uint16_t op=Fixture::op(p);f.complete(op,f.response(op));}}}
 CHECK(f.host.radioStats().manufacturer==15);pump(f,sent);
}
static std::vector<uint16_t> opcodes(const std::vector<Sent>& s){std::vector<uint16_t> o;for(const auto& x:s)o.push_back(x.opcode);return o;}
static bool same(const std::vector<Sent>& s,std::vector<uint16_t> want){const auto got=opcodes(s);if(got==want)return true;std::fprintf(stderr,"CPB opcodes:");for(auto o:got)std::fprintf(stderr," %04x",o);std::fprintf(stderr," (want");for(auto o:want)std::fprintf(stderr," %04x",o);std::fprintf(stderr,")\n");return false;}
struct Rig {
 CpbPort port;uint8_t storage[256]={};Fixture f;std::vector<Sent> sent;
 explicit Rig(const CpbConfig& c,bool start=true){CHECK(port.begin(c,storage));f.host.end();f.config.cpb=&port;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);if(start)boot(f,sent);}
 ~Rig(){f.host.end();}
};
static CpbConfig transmitter(){CpbConfig c;c.role=CpbRole::Transmitter;c.max_payload_bytes=64;return c;}
static const uint8_t display_peer[6]={0x66,0x55,0x44,0x33,0x22,0x11}; // HCI order 11..66
static CpbConfig receiver(){CpbConfig c;c.role=CpbRole::Receiver;c.max_payload_bytes=64;c.lt_addr=0;memcpy(c.transmitter_address,display_peer,6);return c;}

static void default_off_sends_nothing(){
 Fixture f;f.boot();std::vector<Sent> sent;pump(f,sent);f.classicConnect();pump(f,sent);
 CHECK(sent.empty()&&!f.host.config_.cpb);
}
static void transmitter_sequence_and_events(){
 Rig r(transmitter(),false);
 CHECK(r.port.setData(reinterpret_cast<const uint8_t*>("GIGA!"),5)==Status::Ok); // Loaded before boot: sent before the train.
 boot(r.f,r.sent);
 CHECK(same(r.sent,{0x0c63,0x0c74,0x0c78,0x0441,0x0c76,0x0443}));
 CHECK((r.sent[0].params==Bytes{0,0x80,0x37,0,0,0,0,0}));
 CHECK((r.sent[3].params==Bytes{1,1,0,0x18,0,0x50,0,0xa0,0,0,0x20}));
 CHECK((r.sent[4].params==Bytes{1,3,5,'G','I','G','A','!'}));
 auto s=r.port.stats();
 CHECK(s.state==CpbState::Transmitting&&s.interval_slots==0x64&&s.train_interval_slots==0xc0&&s.train_running&&s.trains_started==1);
 // A Classic peer connects and secures while broadcasting.
 r.f.classicConnect();CHECK(r.f.host.radioStats().connected);
 // Payload update and the continuous train restart.
 r.sent.clear();CHECK(r.port.setData(reinterpret_cast<const uint8_t*>("next"),4)==Status::Ok);pump(r.f,r.sent);
 CHECK(r.sent.size()==1&&r.sent[0].opcode==0x0c76&&(r.sent[0].params==Bytes{1,3,4,'n','e','x','t'}));
 r.sent.clear();r.f.event(0x4f,{0});pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0443}));s=r.port.stats();CHECK(s.trains_completed==1&&s.trains_started==2);
 // Stop releases the controller resources.
 r.sent.clear();CHECK(r.port.stop()==Status::Ok);pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0441,0x0c75}));CHECK(r.sent[0].params[0]==0);
 CHECK(r.port.stats().state==CpbState::Off);
 // Restart without reconfiguring: page-2 mask is already written.
 r.sent.clear();CHECK(r.port.start()==Status::Ok);pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0c74,0x0c78,0x0441,0x0c76,0x0443}));
 // Host restart: boot issues HCI Reset, so the whole sequence and the
 // retained payload are sent again.
 r.f.host.end();r.sent.clear();CHECK(r.f.host.begin(r.f.config,r.f.hooks)==Status::Ok);boot(r.f,r.sent);
 CHECK(same(r.sent,{0x0c63,0x0c74,0x0c78,0x0441,0x0c76,0x0443}));
 CHECK((r.sent[4].params==Bytes{1,3,4,'n','e','x','t'}));
}
static void transmitter_failure_and_restart(){
 fail_once=0x0441; // Set CPB returns Invalid HCI Command Parameters once.
 Rig r(transmitter());r.sent.clear();
 auto s=r.port.stats();CHECK(s.state==CpbState::Failed&&s.last_opcode==0x0441&&s.last_status==0x12&&s.command_errors==1);
 // cpbStart() after Failed releases the reserved LT_ADDR first, then retries.
 CHECK(r.port.start()==Status::Ok);pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0c75,0x0c74,0x0c78,0x0441,0x0443}));
 CHECK(r.port.stats().state==CpbState::Transmitting);
}
static Bytes broadcomTrain(uint8_t lt){
 Bytes e={0,0x11,0x22,0x33,0x44,0x55,0x66,0x78,0x56,0x34,0x02};
 for(unsigned i=0;i<10;++i)e.push_back(uint8_t(0xf0+i));
 const Bytes tail={0x5a,lt,0x21,0x43,0x65,0x07,0x64,0};e.insert(e.end(),tail.begin(),tail.end());CHECK(e.size()==29);return e;
}
static Bytes cpbReceive(uint8_t fragment,const Bytes& data,uint8_t status=0){
 Bytes e={0x11,0x22,0x33,0x44,0x55,0x66,1,0x10,0,0,0,0,0,0,0,status,fragment,uint8_t(data.size())};e.insert(e.end(),data.begin(),data.end());return e;
}
static void receiver_sequence_and_events(){
 Rig r(receiver());pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0c63,0x0444}));
 CHECK((r.sent[1].params==Bytes{0x11,0x22,0x33,0x44,0x55,0x66,0,0x40,0,1,0,2}));
 CHECK(r.port.stats().state==CpbState::Scanning);
 // GIGA reports manufacturer 15: the Broadcom train layout is selected.
 r.sent.clear();r.f.event(0x50,broadcomTrain(1));pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0442}));
 CHECK((r.sent[0].params==Bytes{1,0x11,0x22,0x33,0x44,0x55,0x66,1,0x64,0,0x78,0x56,0x34,0x02,0x21,0x43,0x65,0x07,0,0x20,250,0,0x18,0,
                                 0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9}));
 auto s=r.port.stats();CHECK(s.state==CpbState::Receiving&&s.lt_addr==1&&s.interval_slots==0x64&&s.service_data==0x5a&&s.syncs==1);
 uint8_t out[64];size_t n=0;uint32_t clock=0;
 CHECK(!r.port.read(out,sizeof out,n,&clock));
 r.f.event(0x51,cpbReceive(3,{'h','i'}));pump(r.f,r.sent,2);
 CHECK(r.port.read(out,sizeof out,n,&clock)&&n==2&&out[0]=='h'&&clock==0x10);
 // Fragments through the actor.
 r.f.event(0x51,cpbReceive(1,Bytes(27,7)));r.f.event(0x51,cpbReceive(2,Bytes(5,9)));pump(r.f,r.sent,2);
 CHECK(r.port.read(out,sizeof out,n,&clock)&&n==32&&out[26]==7&&out[27]==9);
 // Classic peer alongside reception.
 r.f.classicConnect();CHECK(r.f.host.radioStats().connected);
 // Sync loss: rescan, resynchronize.
 r.sent.clear();r.f.event(0x52,{0x11,0x22,0x33,0x44,0x55,0x66,1});pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0444}));CHECK(r.port.stats().sync_losses==1);
 r.sent.clear();r.f.event(0x50,broadcomTrain(1));pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0442}));CHECK(r.port.stats().syncs==2);
 // Stop: disable reception.
 r.sent.clear();CHECK(r.port.stop()==Status::Ok);pump(r.f,r.sent);
 CHECK(same(r.sent,{0x0442}));CHECK(r.sent[0].params[0]==0&&r.port.stats().state==CpbState::Off);
}
static void deferred_start_and_config_rejection(){
 CpbConfig c=transmitter();c.start_at_begin=false;
 {Rig r(c);pump(r.f,r.sent);CHECK(r.sent.empty());CHECK(r.port.start()==Status::Ok);pump(r.f,r.sent);CHECK(r.sent.size()==5);}
 // Validation.
 CpbConfig bad=transmitter();bad.lt_addr=0;CHECK(!CpbPort::validate(bad));
 bad=transmitter();bad.max_payload_bytes=0;CHECK(!CpbPort::validate(bad));
 bad=transmitter();bad.max_payload_bytes=1025;CHECK(!CpbPort::validate(bad));
 bad=receiver();memset(bad.transmitter_address,0,6);CHECK(!CpbPort::validate(bad));
 CHECK(CpbPort::validate(CpbConfig())&&CpbPort::storageBytes(receiver())==128&&CpbPort::storageBytes(transmitter())==64);
 // Payload bounds and role checks at the port.
 CpbPort port;uint8_t storage[64];CHECK(port.begin(transmitter(),storage));uint8_t big[65]={};
 CHECK(port.setData(big,65)==Status::InvalidArgument);size_t n=0;CHECK(!port.read(big,sizeof big,n,nullptr));
 // BLE-only actor: Classic-only feature is unsupported.
 Fixture f;f.host.end();CpbPort p2;uint8_t s2[64];CHECK(p2.begin(transmitter(),s2));
 f.config.cpb=&p2;f.config.callbacks.enable_a2dp=false;CHECK(f.host.begin(f.config,f.hooks)==Status::Unsupported);
}
int main(){
 default_off_sends_nothing();
 transmitter_sequence_and_events();
 transmitter_failure_and_restart();
 receiver_sequence_and_events();
 deferred_start_and_config_rejection();
 std::printf("PASS CPB actor: transmitter/receiver HCI sequences, events, Classic coexistence, restart (%u checks)\n",checks);
 return 0;
}
