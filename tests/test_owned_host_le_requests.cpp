#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main original_fixture_main
#include "test_owned_host.cpp"
#undef main
struct RequestPressure {
 Fixture f;uint32_t owner;uint16_t media;unsigned stops,configured,queued;
 explicit RequestPressure(bool full=false){f.boot();f.classicConnect();auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();media=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();f.leConnect();CHECK(f.playing&&f.host.radioStats().streaming);owner=f.host.peers_[0].owner;stops=f.stops;configured=f.configured;queued=full?8:7;
 CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1001,nullptr,0,false,owner)==BTH_OK);f.poll();CHECK(Fixture::op(f.pop())==0x1001&&f.host.flow_.command_credits==0);
 for(unsigned i=0;i<queued;++i)CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1001,nullptr,0,false,owner)==BTH_OK);
 }
 void unchanged(){CHECK(f.playing&&f.host.radioStats().streaming&&f.host.classic_.streaming());CHECK(f.stops==stops&&f.configured==configured&&f.host.deferred_count_==0);CHECK(f.host.peers_[0].owner==owner&&bth_links_secure(&f.host.links_,0x25));CHECK(f.host.commands_.used==queued&&f.host.commands_.sent_count==1&&f.host.flow_.command_credits==0);CHECK(f.host.radioStats().transport_errors==0);}
};
static Bytes request(uint8_t code){return code==5?Bytes{5,0x26,0,1,2,3,4,5,6,7,8,9,10}:Bytes{6,0x26,0,64,0,96,0,2,0,0xf4,1};}
static void copy_and_progress(){for(uint8_t code:{uint8_t(5),uint8_t(6)})for(bool full:{false,true}){
 RequestPressure p(full);auto& f=p.f;auto event=request(code);f.event(0x3e,event);f.poll();p.unchanged();
 CHECK(code==5?f.host.le_.ltk_pending_:f.host.le_.parameters_pending_);CHECK(!memcmp(code==5?f.host.le_.pending_ltk_:f.host.le_.pending_parameters_,event.data(),event.size()));
 event.assign(event.size(),0xee);f.poll(141);p.unchanged();CHECK((code==5?f.host.le_.pending_ltk_[1]:f.host.le_.pending_parameters_[1])==0x26);
 const unsigned before=f.media;f.acl(0x25,p.media,{0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa});CHECK(f.host.rx_.outstanding==1);f.poll();CHECK(f.media==before+1);auto ack=f.pop();CHECK(Fixture::op(ack)==0x0c35&&ack.size()==9&&bth_read_le16(ack.data()+5)==0x25&&bth_read_le16(ack.data()+7)==1);CHECK(f.host.rx_.outstanding==0);p.unchanged();
 unsigned replies=0,reads=0;f.complete(0x1001,f.response(0x1001));
 for(unsigned i=0;i<40;++i){f.poll();while(!f.tx.empty()){auto packet=f.pop();CHECK(packet[0]==1);auto op=Fixture::op(packet);if(op==(code==5?0x201b:0x2020)){++replies;CHECK(bth_read_le16(packet.data()+4)==0x26);if(code==6){CHECK(packet.size()==18&&!memcmp(packet.data()+4,request(6).data()+1,10));CHECK(!packet[14]&&!packet[15]&&!packet[16]&&!packet[17]);}}else{CHECK(op==0x1001);++reads;}f.complete(op,(op==0x201b||op==0x2020)?Bytes{0,0x26,0}:f.response(op));}}
 CHECK(replies==1&&reads==p.queued&&f.playing&&f.stops==p.stops&&f.configured==p.configured);CHECK(!f.host.le_.ltk_pending_&&!f.host.le_.parameters_pending_&&f.host.radioStats().transport_errors==0);
}}
static void bounds(){for(uint8_t code:{uint8_t(5),uint8_t(6)}){auto good=request(code);for(size_t n=0;n<=good.size()+1;++n){RequestPressure p;auto e=good;e.resize(n,0);p.f.event(0x3e,e);CHECK(p.f.host.deferred_count_==(n==good.size()?0:1));CHECK(p.f.host.commands_.used==7&&p.f.host.flow_.command_credits==0);}
 RequestPressure p;auto e=good;e[1]=0x27;p.f.event(0x3e,e);p.f.poll();p.unchanged();CHECK(!p.f.host.le_.ltk_pending_&&!p.f.host.le_.parameters_pending_);
 RequestPressure broken;Bytes wire={0x3e,uint8_t(good.size())};wire.insert(wire.end(),good.begin(),good.end()-1);broken.f.host.receive(4,wire.data(),wire.size(),broken.f.now);CHECK(broken.f.host.radioStats().transport_errors==1);
}}
static void lifecycle_fifo(){for(uint8_t code:{uint8_t(5),uint8_t(6)})for(unsigned kind=0;kind<3;++kind){RequestPressure p;auto& f=p.f;f.event(kind<2?5:8,kind<2?Bytes{0,uint8_t(kind?0x26:0x25),0,0x13}:Bytes{0,0x25,0,0});CHECK(f.host.deferred_count_==1);auto serial=f.host.deferred_[f.host.deferred_read_].serial;f.event(0x3e,request(code));CHECK(f.host.deferred_count_==2&&f.host.deferred_[f.host.deferred_read_].serial==serial);CHECK(!f.host.le_.ltk_pending_&&!f.host.le_.parameters_pending_);f.poll();CHECK(!f.playing&&f.host.deferred_count_==2);f.complete(0x1001,f.response(0x1001));f.poll();f.poll();CHECK(f.host.deferred_count_==0&&f.host.radioStats().transport_errors==0);if(kind==1)CHECK(!f.host.peers_[1].attached&&!f.host.le_.ltk_pending_&&!f.host.le_.parameters_pending_);else CHECK(!f.playing);}}
int main(){copy_and_progress();bounds();lifecycle_fifo();printf("PASS public LE05/06 copied-admission/pressure/media/HNC/FIFO %u checks\n",checks);}
