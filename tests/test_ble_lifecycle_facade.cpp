#include <chrono>
#include <future>
#include <thread>
#define private public
#include "GigaBredr.h"
#include "owned/OwnedHost.h"
#undef private
#include "GigaBredr.cpp"
#define main old_fixture_main
#include "test_owned_host.cpp"
#undef main

static Fixture* actor=nullptr;
static unsigned disconnectCalls=0,resumeCalls=0;
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks&,const BleConfig& ble){actor=new Fixture(ble.enabled);return Status::Ok;}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){if(actor)actor->poll();}
void radioEnd(){if(actor){actor->host.end();delete actor;actor=nullptr;}}
RadioStats radioStats(){return actor?actor->host.radioStats():RadioStats{};}
BleStats radioBleStats(){return actor?actor->host.bleStats():BleStats{};}
Status radioBleNotify(const uint8_t* p,size_t n){return actor?actor->host.bleNotify(p,n):Status::Unsupported;}
bool radioBleReadWrite(BleWrite& out){return actor&&actor->host.bleReadWrite(out);}
Status radioBleDisconnect(uint32_t expected,bool advertise){++disconnectCalls;return actor?actor->host.bleDisconnect(expected,advertise):Status::Unsupported;}
Status radioBleResumeAdvertising(){++resumeCalls;return actor?actor->host.bleResumeAdvertising():Status::Unsupported;}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Unsupported;}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}
PcmRecoverySnapshot i2sRecovery(){return{};}
}}
static void owner_busy(GigaBredr& receiver,uint32_t owner,bool resume){
 std::promise<void> entered;std::promise<Status> result;auto ready=entered.get_future();auto finished=result.get_future();
 unsigned d=disconnectCalls,a=resumeCalls;receiver.impl_->radio_mutex.lock();
 std::thread caller([&]{entered.set_value();result.set_value(resume?receiver.bleResumeAdvertising():receiver.bleDisconnect(owner,false));});
 ready.wait();bool nonblocking=finished.wait_for(std::chrono::seconds(1))==std::future_status::ready;
 receiver.impl_->radio_mutex.unlock();auto status=finished.get();caller.join();
 CHECK(nonblocking&&status==Status::Busy&&disconnectCalls==d&&resumeCalls==a);
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);GigaBredr receiver;
 CHECK(receiver.bleDisconnect(1,false)==Status::Unsupported&&receiver.bleResumeAdvertising()==Status::Unsupported);
 CHECK(!disconnectCalls&&!resumeCalls);ReceiverConfig config;config.enable_i2s=false;
 CHECK(receiver.begin(config)==Status::Ok);actor->boot();actor->leConnect();auto owner=receiver.bleStats().generation;
 owner_busy(receiver,owner,false);owner_busy(receiver,owner,true);
 CHECK(receiver.bleDisconnect(0,false)==Status::InvalidArgument&&receiver.bleDisconnect(owner+1,false)==Status::InvalidArgument);
 CHECK(!actor->host.le_advertising_held_);
 CHECK(receiver.bleDisconnect(owner,false)==Status::Ok&&actor->host.links_.le.disconnecting);
 auto deadline=actor->host.peers_[1].disconnect_deadline;
 CHECK(receiver.bleDisconnect(owner,false)==Status::Ok&&actor->host.peers_[1].disconnect_deadline==deadline);
 CHECK(receiver.bleResumeAdvertising()==Status::Busy);actor->settle();CHECK(receiver.bleStats().generation==owner);
 actor->event(5,{0,0x26,0,0x13});actor->settle();CHECK(!receiver.bleStats().generation&&!receiver.bleStats().advertising);
 CHECK(receiver.bleDisconnect(owner,false)==Status::InvalidArgument&&receiver.bleResumeAdvertising()==Status::Ok);
 actor->settle();CHECK(receiver.bleStats().advertising);actor->leConnect();CHECK(receiver.bleStats().generation>owner);
 CHECK(receiver.bleDisconnect(owner,false)==Status::InvalidArgument);receiver.end();
 CHECK(receiver.bleDisconnect(owner,false)==Status::Unsupported&&receiver.bleResumeAdvertising()==Status::Unsupported);
 config.ble.enabled=false;CHECK(receiver.begin(config)==Status::Ok);actor->boot();
 CHECK(receiver.bleDisconnect(1,false)==Status::Unsupported&&receiver.bleResumeAdvertising()==Status::Unsupported);receiver.end();
 CHECK(detail::memoryStats().current_blocks==0&&detail::memoryStats().current_bytes==0);
 printf("PUBLIC_LIFECYCLE_FACADE_REAL_ACTOR_TRYLOCK_STOPPED_DISABLED_STALE_PASS checks=%u\n",checks);
}
