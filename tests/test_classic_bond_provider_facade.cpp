#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#define private public
#include "GigaBredr.h"
#include "owned/OwnedHost.h"
#undef private
#include "GigaBredr.cpp"
#define main old_actor_fixture_main
#include "test_owned_host.cpp"
#undef main
static Fixture* actor=nullptr;
static const uint8_t LocalDisplay[6]={0x42,0xd3,0x50,0x12,0x1e,0x65},LocalHci[6]={0x65,0x1e,0x12,0x50,0xd3,0x42};
namespace giga_bredr {namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig& b){actor=new Fixture(b.enabled);actor->host.end();memcpy(actor->config.address,LocalDisplay,6);actor->config.classic_bonds=c.classic_bonds;return actor->host.begin(actor->config,actor->hooks);}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){if(actor)actor->poll();}void radioEnd(){if(actor){actor->host.end();delete actor;actor=nullptr;}}
RadioStats radioStats(){return actor?actor->host.radioStats():RadioStats{};}BleStats radioBleStats(){return actor?actor->host.bleStats():BleStats{};}
Status radioBleNotify(const uint8_t* p,size_t n){return actor?actor->host.bleNotify(p,n):Status::Unsupported;}bool radioBleReadWrite(BleWrite& w){return actor&&actor->host.bleReadWrite(w);}
Status radioBleDisconnect(uint32_t g,bool a){return actor?actor->host.bleDisconnect(g,a):Status::Unsupported;}Status radioBleResumeAdvertising(){return actor?actor->host.bleResumeAdvertising():Status::Unsupported;}
AvrcpStatus radioAvrcpStatus(){return actor?actor->host.avrcpStatus():AvrcpStatus{};}AvrcpMetadata radioAvrcpMetadata(){return actor?actor->host.avrcpMetadata():AvrcpMetadata{};}
Status radioAvrcpCommand(AvrcpCommand c,uint32_t g,uint32_t& id){return actor?actor->host.avrcpCommand(c,g,id):Status::Unsupported;}Status radioAvrcpCancel(uint32_t g,uint32_t id){return actor?actor->host.avrcpCancel(g,id):Status::Unsupported;}
Status i2sBegin(uint32_t){return Status::Unsupported;}Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Unsupported;}Status i2sBegin(uint32_t,uint16_t,const I2sPins&,uint16_t){return Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t){return 0;}void i2sEnd(){}OutputStats i2sStats(){return{};}OutputTimingSnapshot i2sTiming(){return{};}PcmRecoverySnapshot i2sRecovery(){return{};}
}}
struct App {
 GigaBredr* receiver=nullptr;ReceiverConfig config;ClassicBondRecord record;bool present=false,reenter=false,throws=false,reenterSave=false,reenterRemove=false;unsigned loads=0,saves=0,removes=0;
 ClassicBondStorage provider(){return {this,load,save,remove};}
 static BondStorageResult load(void* p,const ClassicBondIdentity& id,ClassicBondRecord& out){auto& a=*static_cast<App*>(p);++a.loads;CHECK(!memcmp(id.local_address,LocalHci,6));
  if(a.reenter){auto& r=*a.receiver;uint32_t request=99;VoiceInputToken token{55,77};VoiceStatus voice;BleWrite write;
   CHECK(r.begin(a.config)==Status::Busy);r.poll();r.end();CHECK(r.impl_&&r.impl_->running.load());CHECK(r.stats().decoded_frames==0);CHECK(!r.avrcpStatus().connected&&!r.avrcpMetadata().generation&&!r.bleStats().connected);(void)r.audioDiagnostics();
   CHECK(r.avrcpCommand(AvrcpCommand::Play,1,request)==Status::Busy&&request==99);CHECK(r.avrcpCancel(1,1)==Status::Busy);CHECK(r.bleNotify(nullptr,0)==Status::Busy&&r.bleNotify(write)==Status::Busy);CHECK(!r.bleReadWrite(write));CHECK(r.bleDisconnect(1)==Status::Busy&&r.bleResumeAdvertising()==Status::Busy);
   CHECK(r.setVoiceInput({},token)==Status::Busy&&token.lifetime==55&&token.epoch==77);CHECK(r.detachVoiceInput(token)==Status::Busy&&r.voiceStatus(voice)==Status::Busy);CHECK(r.voiceCommand(HfpCommand::Answer,1)==Status::Busy);CHECK(r.headsetCommand(HspCommand::Button,{1,1})==Status::Busy);CHECK(!GigaBredr::setExternalAllocator(nullptr,nullptr));
  }
  if(a.throws)throw std::runtime_error("synthetic callback exception");if(!a.present)return BondStorageResult::NotFound;out=a.record;return BondStorageResult::Ok;
 }
 static BondStorageResult save(void* p,const ClassicBondRecord& r){auto& a=*static_cast<App*>(p);++a.saves;if(a.reenterSave){a.receiver->end();CHECK(a.receiver->impl_);(void)a.receiver->stats();}a.record=r;a.present=true;return BondStorageResult::Ok;}
 static BondStorageResult remove(void* p,const ClassicBondIdentity&){auto& a=*static_cast<App*>(p);++a.removes;if(a.reenterRemove){a.receiver->end();CHECK(a.receiver->impl_);(void)a.receiver->stats();}a.present=false;return BondStorageResult::Ok;}
};
static void link(){actor->event(4,{1,2,3,4,5,6,0,0,0,1});actor->settle();actor->event(3,{0,0x25,0,1,2,3,4,5,6,1,0});actor->settle();}
int main(){App app;GigaBredr r;app.receiver=&r;app.config.enable_i2s=false;app.config.classic_bonds=app.provider();CHECK(r.begin(app.config)==Status::Ok);CHECK(actor->config.classic_bonds.context==r.impl_&&actor->config.classic_bonds.context!=&app);actor->boot();actor->classicConnect();CHECK(app.saves==1&&app.present&&bth_links_secure(&actor->host.links_,0x25));r.end();CHECK(!actor&&app.present&&!app.removes);const unsigned loads=app.loads;
 CHECK(r.begin(app.config)==Status::Ok);actor->boot();link();actor->event(0x17,{1,2,3,4,5,6});actor->settle();CHECK(app.loads==loads+1&&!bth_links_secure(&actor->host.links_,0x25));actor->event(8,{0,0x25,0,1});actor->settle();CHECK(bth_links_secure(&actor->host.links_,0x25));r.end();
 for(unsigned variant=0;variant<2;++variant){app.reenter=variant==0;app.throws=variant==1;CHECK(r.begin(app.config)==Status::Ok);actor->boot();link();r.impl_->radio_mutex.lock();actor->event(0x17,{1,2,3,4,5,6});r.impl_->radio_mutex.unlock();CHECK(!actor->host.active_&&actor->host.radioStats().transport_errors==1);CHECK(bond_callback_thread.load()==BondThread{});r.end();CHECK(!actor);}
 app.reenter=app.throws=false;app.reenterSave=true;CHECK(r.begin(app.config)==Status::Ok);actor->boot();link();Bytes key={1,2,3,4,5,6};key.insert(key.end(),16,0xbb);key.push_back(4);actor->event(0x18,key);r.impl_->radio_mutex.lock();actor->event(6,{0,0x25,0});r.impl_->radio_mutex.unlock();CHECK(!actor->host.active_&&actor->host.radioStats().transport_errors==1);r.end();app.reenterSave=false;
 app.reenterRemove=true;CHECK(r.begin(app.config)==Status::Ok);actor->boot();link();actor->event(0x17,{1,2,3,4,5,6});actor->settle();r.impl_->radio_mutex.lock();actor->event(6,{5,0x25,0});r.impl_->radio_mutex.unlock();CHECK(!actor->host.active_&&actor->host.radioStats().transport_errors==1);r.end();app.reenterRemove=false;
 app.config.classic_bonds.remove=nullptr;CHECK(r.begin(app.config)==Status::InvalidArgument);app.config.classic_bonds=app.provider();app.config.enable_a2dp=false;app.config.ble.enabled=true;CHECK(r.begin(app.config)==Status::Unsupported);CHECK(detail::memoryStats().current_bytes==0);
 printf("APPLICATION_BOND_FACADE_ACTUAL_HOST_PASS checks=%u public_callback_reentry_fenced=1 no_hardware_or_durability_claim=1\n",checks);return 0;
}
