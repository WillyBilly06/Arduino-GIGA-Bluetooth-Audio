// Public GigaBredr HID API over the real facade and real OwnedHost actor:
// defaults, validation before allocation, lazy storage, nonblocking contention,
// report delivery, output-report callback from poll(), disconnect and end().
#include <chrono>
#include <future>
#include <thread>
#define private public
#include "GigaBredr.h"
#include "owned/OwnedHost.h"
#undef private
#include "GigaBredr.cpp"
#include "GigaHidDescriptors.h"
#define main old_fixture_main
#include "test_owned_host.cpp"
#undef main

static Fixture* actor=nullptr;
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig& ble){
 actor=new Fixture(ble.enabled);actor->host.end();
 actor->config.voice_control=c.voice_control;if(c.voice_config)actor->config.voice_config=*c.voice_config;actor->config.hid=c.hid;
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
struct Output {unsigned calls=0;HidReportType type=HidReportType::Input;uint8_t id=0xff;Bytes data;};
uint16_t control=0,interrupt=0;
void tick(GigaBredr& r){r.poll();unsigned budget=8;while(budget--&&!actor->tx.empty())actor->acknowledge(actor->pop());}
void ticks(GigaBredr& r,unsigned n=40){for(unsigned i=0;i<n;++i)tick(r);}
std::vector<Bytes> reports(){std::vector<Bytes> out;for(const auto& p:actor->output)if(p.cid==0x71)out.push_back(p.bytes);return out;}
void connect(GigaBredr& r){actor->boot();actor->classicConnect();control=actor->channel(0x11,0x70);interrupt=actor->channel(0x13,0x71);actor->output.clear();ticks(r,2);CHECK(r.hidConnected());}
template<class F> auto contended(GigaBredr& r,F call){
 std::promise<void> entered;auto ready=entered.get_future();r.impl_->radio_mutex.lock();
 auto result=std::async(std::launch::async,[&]{entered.set_value();return call();});
 ready.wait();const bool nonblocking=result.wait_for(std::chrono::seconds(1))==std::future_status::ready;
 r.impl_->radio_mutex.unlock();CHECK(nonblocking);return result.get();
}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);GigaBredr r;ReceiverConfig config;config.enable_i2s=false;config.ble.enabled=false;
 const uint8_t key[8]={0,0,0x04};HidStats stats;Output seen;
 const ReceiverConfig defaults;
 CHECK(!defaults.hid.enabled&&!defaults.hid.report_descriptor&&defaults.hid.subclass==HidSubclassKeyboard&&!strcmp(defaults.hid.service_name,"GIGA HID")&&
       defaults.hid.virtual_cable&&!defaults.hid.reconnect_initiate&&defaults.hid.normally_connectable&&!defaults.hid.class_of_device&&defaults.hid.device_id);
 CHECK(r.hidSend(1,key,8)==Status::Unsupported&&!r.hidConnected()&&r.hidStats(stats)==Status::Unsupported);
 // Default disabled: no HID storage, records, UUID or listener.
 CHECK(r.begin(config)==Status::Ok);const auto disabled_blocks=detail::memoryStats().current_blocks;
 CHECK(!r.impl_->hid&&!actor->config.hid&&r.hidSend(1,key,8)==Status::Unsupported&&r.hidStats(stats)==Status::Unsupported);
 actor->boot();CHECK(actor->host.classic_.record_count_==2&&actor->host.gap_.config.uuid_count==1&&actor->host.gap_.config.class_of_device==0x240414);r.end();
 // Validation happens before any allocation or radio start.
 config.hid.report_descriptor=hid_descriptors::KeyboardMouseCombo;config.hid.report_descriptor_bytes=sizeof hid_descriptors::KeyboardMouseCombo;
 config.hid.subclass=HidSubclassCombo;config.hid.boot_keyboard=config.hid.boot_mouse=true;
 auto bad=[&](void(*edit)(ReceiverConfig&),Status expected){ReceiverConfig c=config;c.hid.enabled=true;edit(c);CHECK(r.begin(c)==expected&&!actor&&detail::memoryStats().current_blocks==0);};
 bad([](ReceiverConfig& c){c.enable_a2dp=false;c.ble.enabled=true;},Status::Unsupported);
 bad([](ReceiverConfig& c){c.hid.report_descriptor=nullptr;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.hid.report_descriptor_bytes=0;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.hid.service_name="";},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.hid.description="123456789012345678901234567890123";},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.hid.subclass=0xc1;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.hid.report_descriptor=hid_descriptors::Mouse;c.hid.report_descriptor_bytes=sizeof hid_descriptors::Mouse;},Status::InvalidArgument);
 // Enabled: one lazily allocated endpoint block, wired into the actor.
 config.hid.enabled=true;config.hid.class_of_device=HidClassOfDeviceCombo;config.hid.user=&seen;
 config.hid.output_report=[](HidReportType type,uint8_t id,const uint8_t* data,size_t n,void* user){auto& o=*static_cast<Output*>(user);++o.calls;o.type=type;o.id=id;o.data.assign(data,data+n);};
 CHECK(r.begin(config)==Status::Ok&&r.impl_->hid&&actor->config.hid==r.impl_->hid&&detail::memoryStats().current_blocks==disabled_blocks+1);
 CHECK(r.hidStats(stats)==Status::Ok&&stats.enabled&&!stats.connected&&r.hidSend(1,key,8)==Status::InvalidArgument);
 connect(r);
 CHECK(actor->host.classic_.record_count_==4&&actor->host.uuids_[1]==0x1124&&actor->host.gap_.config.class_of_device==0x2405c0);
 CHECK(r.hidStats(stats)==Status::Ok&&stats.connected&&stats.control_open&&stats.interrupt_open&&stats.connections==1&&stats.generation==1);
 // Nonblocking contention: Busy without waiting; the callback waits for the lock.
 CHECK(contended(r,[&]{return r.hidSend(1,key,8);})==Status::Busy&&contended(r,[&]{return r.hidConnected();}));
 CHECK(r.hidSend(1,key,7)==Status::InvalidArgument&&r.hidSend(7,key,8)==Status::InvalidArgument&&r.hidSend(1,nullptr,8)==Status::InvalidArgument);
 CHECK(r.hidSend(1,key,8)==Status::Ok);const uint8_t up[8]={};CHECK(r.hidSend(1,up,8)==Status::Ok);ticks(r);
 CHECK(reports()==std::vector<Bytes>({{0xa1,0x01,0,0,0x04,0,0,0,0,0},{0xa1,0x01,0,0,0,0,0,0,0,0}}));
 // Queue bound: eight admitted, then OutputFull until the actor drains.
 for(unsigned i=0;i<8;++i)CHECK(r.hidSend(1,key,8)==Status::Ok);
 CHECK(r.hidSend(1,key,8)==Status::OutputFull);ticks(r);CHECK(r.hidSend(1,key,8)==Status::Ok);ticks(r);
 CHECK(r.hidStats(stats)==Status::Ok&&stats.reports_sent==11&&stats.reports_rejected==1&&stats.tx_pending==0);
 // Output report (Caps Lock): delivered from poll() only, outside the lock.
 actor->acl(0x25,control,{0x52,0x01,0x02});
 actor->settle();CHECK(seen.calls==0);
 r.impl_->radio_mutex.lock();auto blocked=std::async(std::launch::async,[&]{r.poll();});
 CHECK(blocked.wait_for(std::chrono::seconds(1))==std::future_status::ready);r.impl_->radio_mutex.unlock();
 CHECK(seen.calls==0); // TryLock failed: retained for the next poll().
 tick(r);CHECK(seen.calls==1&&seen.type==HidReportType::Output&&seen.id==1&&seen.data==Bytes({0x02}));
 tick(r);CHECK(seen.calls==1);
 bool handshake=false;for(const auto& p:actor->output)handshake|=p.cid==0x70&&p.bytes==Bytes({0x00});CHECK(handshake);
 // Peer disconnect: connected flips without the radio lock; data rejected.
 actor->event(5,{0,0x25,0,0x13});actor->settle();CHECK(!r.hidConnected()&&r.hidSend(1,key,8)==Status::InvalidArgument);
 r.end();CHECK(!r.hidConnected()&&r.hidStats(stats)==Status::Unsupported&&r.hidSend(1,key,8)==Status::Unsupported);
 CHECK(detail::memoryStats().current_blocks==0&&detail::memoryStats().current_bytes==0);
 printf("HID_PUBLIC_FACADE_REAL_ACTOR_TRYLOCK_LAZY_PASS checks=%u\n",checks);
}
