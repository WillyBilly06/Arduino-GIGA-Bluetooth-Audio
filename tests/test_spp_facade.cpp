// Public GigaBredr SPP API over the real facade, real OwnedHost actor and real
// RFCOMM owner: validation, lazy storage, nonblocking contention, data, end().
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
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig& ble){
 actor=new Fixture(ble.enabled);actor->host.end();
 actor->config.voice_control=c.voice_control;if(c.voice_config)actor->config.voice_config=*c.voice_config;
 const auto status=actor->host.begin(actor->config,actor->hooks);if(status!=Status::Ok){delete actor;actor=nullptr;}return status;
}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){if(actor)actor->poll();}
void radioEnd(){if(actor){actor->host.end();delete actor;actor=nullptr;}}
RadioStats radioStats(){return actor?actor->host.radioStats():RadioStats{};}
BleStats radioBleStats(){return actor?actor->host.bleStats():BleStats{};}
Status radioBleNotify(const uint8_t* p,size_t n){return actor?actor->host.bleNotify(p,n):Status::Unsupported;}
bool radioBleReadWrite(BleWrite& out){return actor&&actor->host.bleReadWrite(out);}
Status radioBleDisconnect(uint32_t expected,bool advertise){return actor?actor->host.bleDisconnect(expected,advertise):Status::Unsupported;}
Status radioBleResumeAdvertising(){return actor?actor->host.bleResumeAdvertising():Status::Unsupported;}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Unsupported;}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}
PcmRecoverySnapshot i2sRecovery(){return{};}
AvrcpStatus radioAvrcpStatus(){return{};}
AvrcpMetadata radioAvrcpMetadata(){return{};}
Status radioAvrcpCommand(AvrcpCommand,uint32_t,uint32_t&){return Status::Unsupported;}
Status radioAvrcpCancel(uint32_t,uint32_t){return Status::Unsupported;}
}}
namespace {
bth_rfcomm_t peer{};uint16_t cid=0;uint8_t reply[600];size_t seen=0;std::string got;
void send(const uint8_t* p,size_t n){if(n)actor->acl(0x25,cid,Bytes(p,p+n));}
void tick(GigaBredr& r){
 r.poll();unsigned budget=8;while(budget--&&!actor->tx.empty())actor->acknowledge(actor->pop());
 while(seen<actor->output.size()){const auto p=actor->output[seen++];if(p.cid!=0x90)continue;size_t n=0;bth_rfcomm_event_t e{};
  CHECK(bth_rfcomm_receive(&peer,19,actor->now,p.bytes.data(),p.bytes.size(),reply,sizeof reply,&n,&e)>=BTH_OK);
  if(e.type==BTH_RFCOMM_DATA){got.append(reinterpret_cast<const char*>(e.payload.data),e.payload.len);CHECK(bth_rfcomm_release(&peer,19,6,1)==BTH_OK);}send(reply,n);}
 size_t n=0;CHECK(bth_rfcomm_next(&peer,19,actor->now,reply,sizeof reply,&n)>=BTH_OK);send(reply,n);
}
void connect(GigaBredr& r){
 actor->boot();actor->classicConnect();cid=actor->channel(3,0x90);actor->output.clear();seen=0;got.clear();CHECK(bth_rfcomm_init(&peer,19,true,0,512,7));
 size_t n=0;CHECK(bth_rfcomm_start(&peer,19,actor->now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);for(unsigned i=0;i<5;++i)tick(r);
 CHECK(bth_rfcomm_open(&peer,19,3,true,actor->now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
 for(unsigned i=0;i<60&&!(r.sppConnected()&&bth_rfcomm_ready(&peer,19,6));++i)tick(r);CHECK(r.sppConnected());
}
void peerSend(const std::string& s){size_t n=0;CHECK(bth_rfcomm_send(&peer,19,6,reinterpret_cast<const uint8_t*>(s.data()),s.size(),reply,sizeof reply,&n)==BTH_OK);send(reply,n);}
template<class F> auto contended(GigaBredr& r,F call){
 std::promise<void> entered;auto ready=entered.get_future();r.impl_->radio_mutex.lock();
 auto result=std::async(std::launch::async,[&]{entered.set_value();return call();});
 ready.wait();const bool nonblocking=result.wait_for(std::chrono::seconds(1))==std::future_status::ready;
 r.impl_->radio_mutex.unlock();CHECK(nonblocking);return result.get();
}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);GigaBredr r;ReceiverConfig config;config.enable_i2s=false;config.ble.enabled=false;
 size_t accepted=9;uint8_t buffer[64];SppStats stats;
 CHECK(!ReceiverConfig().spp.enabled&&ReceiverConfig().spp.channel==3&&!strcmp(ReceiverConfig().spp.service_name,"GIGA Serial"));
 CHECK(r.sppWrite(buffer,1,accepted)==Status::Unsupported&&accepted==0&&r.sppRead(buffer,sizeof buffer)==0&&!r.sppConnected()&&r.sppStats(stats)==Status::Unsupported);
 // Default disabled: no SPP storage, no PSM3 listener, Unsupported data calls.
 CHECK(r.begin(config)==Status::Ok);const auto disabled_blocks=detail::memoryStats().current_blocks;
 CHECK(!r.impl_->serial&&!actor->config.voice_control&&r.sppWrite(buffer,1,accepted)==Status::Unsupported&&r.sppStats(stats)==Status::Unsupported);
 actor->boot();CHECK(actor->host.classic_.record_count_==2&&actor->host.gap_.config.uuid_count==1);r.end();
 // Validation happens before any allocation or radio start.
 auto bad=[&](void(*edit)(ReceiverConfig&),Status expected){ReceiverConfig c=config;c.spp.enabled=true;edit(c);CHECK(r.begin(c)==expected&&!actor&&detail::memoryStats().current_blocks==0);};
 bad([](ReceiverConfig& c){c.enable_a2dp=false;c.ble.enabled=true;},Status::Unsupported);
 bad([](ReceiverConfig& c){c.spp.service_name=nullptr;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.service_name="";},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.service_name="123456789012345678901234567890123";},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.channel=0;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.channel=31;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.rx_buffer_bytes=511;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.tx_buffer_bytes=16385;},Status::InvalidArgument);
 // Enabled: three lazily allocated blocks (session, rings, RFCOMM owner).
 config.spp.enabled=true;config.spp.rx_buffer_bytes=1024;config.spp.tx_buffer_bytes=512;
 CHECK(r.begin(config)==Status::Ok&&r.impl_->serial&&r.impl_->serial->profiles&&actor->config.voice_control==r.impl_->serial->profiles);
 CHECK(detail::memoryStats().current_blocks==disabled_blocks+3);
 CHECK(r.sppStats(stats)==Status::Ok&&stats.enabled&&!stats.connected&&r.sppWrite(buffer,1,accepted)==Status::InvalidArgument);
 connect(r);CHECK(actor->host.classic_.record_count_==3&&actor->host.uuids_[1]==0x1101);
 CHECK(r.sppStats(stats)==Status::Ok&&stats.connected&&stats.frame_bytes==127&&stats.connections==1);
 CHECK(r.impl_->serial->profiles->mux().dlc[0].window==7||r.impl_->serial->profiles->mux().dlc[1].window==7);
 // Nonblocking contention: Busy / 0 bytes without waiting for the owner.
 CHECK(contended(r,[&]{size_t a=5;const auto s=r.sppWrite(buffer,4,a);return s==Status::Busy&&a==0;}));
 peerSend("abc");tick(r);CHECK(contended(r,[&]{return r.sppRead(buffer,sizeof buffer);})==0);
 CHECK(r.sppRead(buffer,2)==2&&!memcmp(buffer,"ab",2)&&r.sppRead(buffer,sizeof buffer)==1&&buffer[0]=='c');
 // Partial public write into the 512-byte TX ring, then drained in order.
 std::string data(700,'\0');for(size_t i=0;i<data.size();++i)data[i]=char('A'+i%23);
 CHECK(r.sppWrite(reinterpret_cast<const uint8_t*>(data.data()),data.size(),accepted)==Status::Ok&&accepted==512);
 CHECK(r.sppWrite(reinterpret_cast<const uint8_t*>(data.data())+512,188,accepted)==Status::OutputFull&&accepted==0);
 for(unsigned i=0;i<40;++i)tick(r);
 CHECK(r.sppWrite(reinterpret_cast<const uint8_t*>(data.data())+512,188,accepted)==Status::Ok&&accepted==188);
 for(unsigned i=0;i<40;++i)tick(r);CHECK(got==data);
 CHECK(r.sppStats(stats)==Status::Ok&&stats.tx_bytes==700&&stats.tx_rejected_bytes==2*188&&stats.rx_bytes==3&&stats.tx_pending==0);
 // Peer disconnect: connected flips without the radio lock; data rejected.
 actor->event(5,{0,0x25,0,0x13});actor->settle();CHECK(!r.sppConnected()&&r.sppWrite(buffer,1,accepted)==Status::InvalidArgument);
 r.end();CHECK(!r.sppConnected()&&r.sppStats(stats)==Status::Unsupported);
 CHECK(detail::memoryStats().current_blocks==0&&detail::memoryStats().current_bytes==0);
 printf("SPP_PUBLIC_FACADE_REAL_ACTOR_TRYLOCK_LAZY_PASS checks=%u\n",checks);
}
