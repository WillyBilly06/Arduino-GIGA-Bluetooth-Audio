// White-box admission pressure is deliberate: fill the actual command/control
// kernels, then drive the production actor through its public HCI/ACL boundary.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_fixture_main
#include "test_owned_host.cpp"
#undef main
static uint16_t start(Fixture& f){f.boot();f.classicConnect();auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();auto media=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing);return media;}
static void fill(Fixture& f){for(unsigned i=0;i<7;++i)CHECK(bth_cmdq_push(&f.host.commands_,0x0c16,nullptr,0,false)==BTH_OK);}
int main(){
 {Fixture f(false);auto media=start(f);auto signal=f.host.classic_.signalCid();for(unsigned i=0;i<2;++i){bth_control_reservation_t r;CHECK(bth_control_tx_reserve(&f.host.control_,0,&r)==BTH_OK);r.data[0]=0xaa;CHECK(bth_control_tx_commit(&f.host.control_,r.token,0x81,1,672)==BTH_OK);}
 f.acl(0x25,signal,{0x40,8,4});f.poll();f.settle();CHECK(f.host.signaling_.channels[media-0x40].state==BTH_CHANNEL_DISCONNECTING);CHECK(!f.playing&&f.host.close_media_==0);}
 {Fixture f(false);f.boot();f.event(4,{1,2,3,4,5,6,0,0,0,1});f.settle();fill(f);f.event(3,{0,0x25,0,1,2,3,4,5,6,1,0});CHECK(f.host.deferred_count_==1);
 for(unsigned i=0;i<4;++i)f.acl(0x25,1,{8,uint8_t(i+1),0,0});unsigned retained=0;for(const auto& p:f.host.packets_)if(p.used)++retained;CHECK(retained==4&&f.host.radioStats().dropped_packets==0);f.settle();CHECK(f.host.radioStats().connected&&f.host.radioStats().dropped_packets==0&&f.host.rx_.outstanding==0);}
 {Fixture f(false);auto media=start(f);fill(f);f.ready=false;unsigned before=f.media;f.event(8,{0,0x25,0,0});CHECK(f.host.deferred_count_==1);f.acl(0x25,media,{0x80,0x60,0,1,0,0,0,0,0,0,0,0,1,0xaa});f.poll();CHECK(f.media==before);f.ready=true;f.settle();CHECK(!f.host.radioStats().streaming&&f.media==before);}
 {Fixture f(false);f.boot();f.event(4,{1,2,3,4,5,6,0,0,0,1});f.settle();fill(f);f.event(3,{0,0x25,0,1,2,3,4,5,6,1,0});f.acl(0x25,1,{8,1,0,0});f.event(5,{0,0x25,0,0x13});f.settle();CHECK(!f.host.radioStats().connected&&f.host.radioStats().dropped_packets==0&&f.host.rx_.outstanding==0);}
 {Fixture f(false);f.boot();f.classicConnect();f.host.flow_.command_credits=8;auto owner=f.host.links_.classic.owner;for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push_owned(&f.host.commands_,uint16_t(0x0c16+i),nullptr,0,false,owner)==BTH_OK);for(unsigned i=0;i<8;++i){f.poll();while(!f.tx.empty())f.pop();}CHECK(f.host.commands_.sent_count==8);for(unsigned i=0;i<7;++i)CHECK(bth_cmdq_push_owned(&f.host.commands_,uint16_t(0x0c26+i),nullptr,0,false,owner)==BTH_OK);f.complete(0x0c16);CHECK(f.host.deferred_count_==0&&f.host.commands_.sent_count==7);f.poll();CHECK(!f.tx.empty()&&f.host.radioStats().transport_errors==0);}
 {Fixture f(false);f.boot();f.classicConnect();auto owner=f.host.links_.classic.owner;CHECK(bth_cmdq_push_owned(&f.host.commands_,0x0411,nullptr,0,false,owner)==BTH_OK);f.poll();while(!f.tx.empty())f.pop();f.now+=2000;f.status(0x0411,0x23);CHECK(int32_t(f.host.links_.classic.retry_at_ms-f.now)>=1000);}
 {Fixture f(false);auto media=start(f);unsigned configured=f.configured;f.event(0x30,{0,0x25,0});f.poll();CHECK(!f.host.radioStats().streaming);f.settle();CHECK(f.host.classic_.configured()&&f.host.radioStats().streaming&&f.playing&&f.configured==configured+1);CHECK(f.host.signaling_.channels[media-0x40].state==BTH_CHANNEL_OPEN);}
 {Fixture f(false);start(f);f.poll(40000);f.fail_decoder=true;f.event(0x30,{0,0x25,0});f.settle();CHECK(!f.host.radioStats().streaming&&!f.playing&&f.host.links_.classic.disconnecting);}
 {Fixture f(false);auto media=start(f);f.event(0x30,{0,0x25,0});f.poll();while(!f.tx.empty())f.pop();for(unsigned i=0;i<3;++i){bth_control_reservation_t r;CHECK(bth_control_tx_reserve(&f.host.control_,0,&r)==BTH_OK);r.data[0]=0xaa;CHECK(bth_control_tx_commit(&f.host.control_,r.token,0x81,1,672)==BTH_OK);}f.complete(0x1408,{0,0x25,0,16});unsigned before=f.media;f.acl(0x25,media,{0x80,0x60,0,1,0,0,0,0,0,0,0,0,1,0xaa});f.poll();CHECK(f.media==before&&!f.playing);f.settle();CHECK(f.media==before+1&&f.playing&&f.host.radioStats().streaming);}
 printf("Owned actor admission/authorization/close pressure regressions passed %u checks\n",checks);return 0;
}
