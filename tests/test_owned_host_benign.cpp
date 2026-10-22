#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main original_fixture_main
#include "test_owned_host.cpp"
#undef main
struct Pressure {
 Fixture f;uint32_t owner=0;unsigned stops=0,prepared=0;
 Pressure(){f.boot();f.classicConnect();auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing&&f.host.radioStats().streaming);f.leConnect();f.acl(0x26,4,{0x12,12,0,1,0});f.settle();uint8_t value=7;CHECK(f.host.bleNotify(&value,1)==Status::Ok);f.poll();auto pending_acl=f.pop();CHECK(pending_acl[0]==2&&f.host.flow_.le.available+1==f.host.flow_.le.total);owner=f.host.peers_[0].owner;stops=f.stops;prepared=f.configured;
 CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1001,nullptr,0,false,owner)==BTH_OK);f.poll();CHECK(Fixture::op(f.pop())==0x1001&&f.host.flow_.command_credits==0&&f.host.commands_.sent_count==1);
 const uint16_t ops[]={0x1002,0x1003,0x1005,0x1009,0x100b,0x1001};for(auto op:ops)CHECK(bth_cmdq_push_owned(&f.host.commands_,op,nullptr,0,false,owner)==BTH_OK);uint8_t page=0;CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1004,&page,1,false,owner)==BTH_OK);CHECK(f.host.commands_.used==7&&f.tx.empty());}
 void unchanged(){CHECK(f.playing&&f.host.radioStats().streaming&&f.host.classic_.streaming());CHECK(f.stops==stops&&f.configured==prepared);CHECK(f.host.peers_[0].owner==owner&&bth_links_secure(&f.host.links_,0x25));CHECK(f.host.commands_.used==7&&f.host.commands_.sent_count==1&&f.host.flow_.command_credits==0);CHECK(f.host.deferred_count_==0&&f.tx.empty()&&f.host.radioStats().transport_errors==0);}
};
static Bytes body(uint8_t code,bool failure=false){switch(code){case 0x1b:return{0x25,0,5};case 0x1d:return{uint8_t(failure?0x0c:0),0x25,0,0x18,0xcc};case 0x14:return{uint8_t(failure?0x0c:0),0x25,0,0,0,0};default:return{uint8_t(failure?0x0c:0),1,2,3,4,5,6,1};}}
static void benign(){for(auto code:{0x1bu,0x1du,0x14u,0x12u})for(bool failure:{false,true}){Pressure p;p.f.event(uint8_t(code),body(uint8_t(code),failure));p.f.poll();p.unchanged();p.f.poll(141);p.unchanged();}}
static void fifo(){for(bool disconnect:{false,true})for(auto code:{0x1bu,0x1du,0x14u,0x12u}){Pressure p;auto&f=p.f;f.event(disconnect?5:8,disconnect?Bytes{0,0x25,0,0x13}:Bytes{0,0x25,0,0});CHECK(f.host.deferred_count_==1);auto first=f.host.deferred_[f.host.deferred_read_].serial;f.event(uint8_t(code),body(uint8_t(code)));CHECK(f.host.deferred_count_==2&&f.host.deferred_[f.host.deferred_read_].serial==first);f.poll();CHECK(!f.playing&&f.host.deferred_count_==2&&f.host.peers_[0].owner==p.owner);f.complete(0x1001,f.response(0x1001));f.poll();f.poll();CHECK(f.host.deferred_count_==0&&!f.playing&&!f.host.radioStats().streaming);if(disconnect)CHECK(!f.host.peers_[0].attached);else CHECK(!bth_links_secure(&f.host.links_,0x25));CHECK(f.host.radioStats().transport_errors==0);}}
static void bounds(){for(auto code:{0x1bu,0x1du,0x14u,0x12u}){auto valid=body(uint8_t(code));for(size_t n=0;n<=valid.size()+1;++n){Pressure p;Bytes params(n,0);p.f.event(uint8_t(code),params);p.f.poll();p.unchanged();}Pressure p;Bytes broken={uint8_t(code),uint8_t(valid.size())};if(!valid.empty())broken.insert(broken.end(),valid.begin(),valid.end()-1);p.f.host.receive(4,broken.data(),broken.size(),p.f.now);CHECK(p.f.host.radioStats().transport_errors==1);}}
static void normal_completion_and_ncp(){Pressure p;p.f.event(0x13,{1,0x26,0,1,0});p.unchanged();CHECK(p.f.host.flow_.le.available==p.f.host.flow_.le.total);p.f.complete(0x1001,p.f.response(0x1001));CHECK(p.f.host.flow_.command_credits==1&&p.f.host.commands_.sent_count==0&&p.f.host.commands_.used==7);p.f.poll();CHECK(p.f.host.commands_.used==6&&p.f.playing&&p.f.configured==p.prepared);}
static Bytes le3(uint8_t status=0){return{3,status,0x26,0,24,0,0,0,0xf4,1};}
static void le3_unchanged(){
 for(uint8_t status:{uint8_t(0),uint8_t(0x0c),uint8_t(0xff)})for(bool full:{false,true}){
  Pressure p;auto&f=p.f;const auto le_owner=f.host.peers_[1].owner;auto event=le3(status);if(status)for(unsigned i=2;i<event.size();++i)event[i]=0xff;
  if(full)CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1002,nullptr,0,false,p.owner)==BTH_OK);
  f.event(0x3e,event);f.poll();CHECK(f.stops==p.stops&&f.playing&&f.host.radioStats().streaming);CHECK(f.configured==p.prepared&&f.host.deferred_count_==0&&f.host.classic_.streaming());
  CHECK(f.host.commands_.used==(full?8:7)&&f.host.commands_.sent_count==1&&f.host.flow_.command_credits==0&&f.tx.empty());CHECK(f.host.peers_[0].owner==p.owner&&f.host.peers_[1].owner==le_owner&&bth_links_secure(&f.host.links_,0x25));CHECK(f.host.radioStats().transport_errors==0);
  f.poll(141);CHECK(f.stops==p.stops&&f.configured==p.prepared&&f.playing);
 }
}
static void le3_bounds_and_other_events(){
 for(size_t n=0;n<=11;++n){Pressure p;auto event=le3();event.resize(n,0);p.f.event(0x3e,event);CHECK(p.f.host.deferred_count_==(n==10?0:1));CHECK(p.f.host.commands_.used==7&&p.f.host.commands_.sent_count==1&&p.f.host.flow_.command_credits==0);CHECK(p.f.host.radioStats().transport_errors==0);}
 {Pressure p;const uint8_t broken[]={0x3e,10,3,0,0x26,0,24,0,0,0,0xf4};p.f.host.receive(4,broken,sizeof broken,p.f.now);CHECK(p.f.host.radioStats().transport_errors==1);CHECK(p.f.host.flow_.command_credits==0);}
 // These valid command-producing LE events keep the original preflight path.
 for(auto event:{Bytes{5,0x26,0,0,0,0,0,0,0,0,0,0,0},Bytes{6,0x26,0,6,0,6,0,0,0,0xf4,1}}){Pressure p;p.f.event(0x3e,event);CHECK(p.f.host.deferred_count_==1&&p.f.host.commands_.used==7&&p.f.host.flow_.command_credits==0);}
 {Pressure p;auto event=le3();event[0]=7;p.f.event(0x3e,event);CHECK(p.f.host.deferred_count_==1);}
}
static void le3_credits(){
 Pressure p;auto&f=p.f;f.event(0x3e,le3());f.poll();p.unchanged();const auto outstanding=f.host.flow_.links[1].outstanding;CHECK(outstanding==1);f.event(0x13,{1,0x26,0,1,0});p.unchanged();CHECK(f.host.flow_.le.available==f.host.flow_.le.total&&f.host.flow_.links[1].outstanding==0);
 f.complete(0x0c35,{0},7);p.unchanged();f.complete(0x1001,f.response(0x1001));CHECK(f.host.flow_.command_credits==1&&f.host.commands_.sent_count==0&&f.host.commands_.used==7);f.poll();CHECK(f.host.commands_.used==6&&f.playing&&f.stops==p.stops&&f.configured==p.prepared);
 {Pressure bad;bad.f.event(0x3e,le3());bad.f.complete(0x0c35,{0x12},7);CHECK(bad.f.host.radioStats().transport_errors==1&&bad.f.host.flow_.command_credits==0);}
}
static void le3_fifo(){
 for(bool disconnect:{false,true})for(bool le_disconnect:{false,true}){
  if(le_disconnect&&!disconnect)continue;
  Pressure p;auto&f=p.f;f.event(disconnect?5:8,disconnect?Bytes{0,uint8_t(le_disconnect?0x26:0x25),0,0x13}:Bytes{0,0x25,0,0});CHECK(f.host.deferred_count_==1);const auto serial=f.host.deferred_[f.host.deferred_read_].serial;
  f.event(0x3e,le3());CHECK(f.host.deferred_count_==2&&f.host.deferred_[f.host.deferred_read_].serial==serial);f.poll();CHECK(!f.playing&&f.host.deferred_count_==2&&f.host.peers_[0].owner==p.owner);
  f.complete(0x1001,f.response(0x1001));f.poll();f.poll();CHECK(f.host.deferred_count_==0&&f.host.radioStats().transport_errors==0);
  if(!disconnect)CHECK(!f.playing&&!bth_links_secure(&f.host.links_,0x25));
  else if(!le_disconnect)CHECK(!f.playing&&!f.host.peers_[0].attached);
  else CHECK(!f.host.peers_[1].attached&&f.host.peers_[0].owner==p.owner&&bth_links_secure(&f.host.links_,0x25));
 }
}
int main(int argc,char**argv){if(argc==2&&!strcmp(argv[1],"fifo"))fifo();else if(argc==2&&!strcmp(argv[1],"benign"))benign();else{le3_unchanged();le3_bounds_and_other_events();le3_credits();le3_fifo();benign();fifo();bounds();normal_completion_and_ncp();}printf("PASS actualpublicreceive benignpreflight/FIFO/bounds/owners/credits %uchecks\n",checks);}
