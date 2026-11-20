// Public GigaBredr OBEX API over the real facade, real OwnedHost actor and real
// RFCOMM owner: defaults, validation before allocation, lazy storage, shared
// RFCOMM owner, nonblocking contention and callbacks delivered from poll().
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
struct Seen {std::string name,data;uint32_t length=0;unsigned begins=0,ends=0,pb_ends=0;bool ok=false;PbapResult result;std::thread::id thread;};
GigaBredr* current=nullptr;Seen seen;bth_rfcomm_t peer{};uint16_t cid=0;uint8_t reply[600];size_t read=0;Bytes inbox;
void send(const uint8_t* p,size_t n){if(n)actor->acl(0x25,cid,Bytes(p,p+n));}
void tick(GigaBredr& r){
 r.poll();unsigned budget=8;while(budget--&&!actor->tx.empty())actor->acknowledge(actor->pop());
 while(read<actor->output.size()){const auto p=actor->output[read++];if(p.cid!=0x90)continue;size_t n=0;bth_rfcomm_event_t e{};
  CHECK(bth_rfcomm_receive(&peer,19,actor->now,p.bytes.data(),p.bytes.size(),reply,sizeof reply,&n,&e)>=BTH_OK);
  if(e.type==BTH_RFCOMM_DATA){inbox.insert(inbox.end(),e.payload.data,e.payload.data+e.payload.len);CHECK(bth_rfcomm_release(&peer,19,8,1)==BTH_OK);}send(reply,n);}
 size_t n=0;CHECK(bth_rfcomm_next(&peer,19,actor->now,reply,sizeof reply,&n)>=BTH_OK);send(reply,n);
}
void connect(GigaBredr& r){
 actor->boot();actor->classicConnect();cid=actor->channel(3,0x90);actor->output.clear();read=0;inbox.clear();CHECK(bth_rfcomm_init(&peer,19,true,0,512,7));
 size_t n=0;CHECK(bth_rfcomm_start(&peer,19,actor->now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);for(unsigned i=0;i<5;++i)tick(r);
 CHECK(bth_rfcomm_open(&peer,19,4,true,actor->now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
 ObexStats s;for(unsigned i=0;i<60&&!(r.obexStats(s)==Status::Ok&&s.opp_connected&&bth_rfcomm_ready(&peer,19,8));++i)tick(r);CHECK(s.opp_connected);
}
void peerPacket(const Bytes& p){
 for(size_t at=0;at<p.size();){const size_t k=p.size()-at<127?p.size()-at:127;size_t n=0;const auto st=bth_rfcomm_send(&peer,19,8,p.data()+at,k,reply,sizeof reply,&n);
  if(st==BTH_MORE){tick(*current);continue;}CHECK(st==BTH_OK);send(reply,n);at+=k;}
}
}
template<class F> auto contended(GigaBredr& r,F call){
 std::promise<void> entered;auto ready=entered.get_future();r.impl_->radio_mutex.lock();
 auto result=std::async(std::launch::async,[&]{entered.set_value();return call();});
 ready.wait();const bool nonblocking=result.wait_for(std::chrono::seconds(1))==std::future_status::ready;
 r.impl_->radio_mutex.unlock();CHECK(nonblocking);return result.get();
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);GigaBredr r;current=&r;ReceiverConfig config;config.enable_i2s=false;config.ble.enabled=false;ObexStats stats;
 const ObexConfig defaults;
 CHECK(!defaults.opp_enabled&&!defaults.pbap_enabled&&defaults.opp_channel==4&&defaults.event_buffer_bytes==4096&&!strcmp(defaults.opp_service_name,"GIGA Object Push"));
 CHECK(r.pbapPullPhonebook()==Status::Unsupported&&r.pbapAbort()==Status::Unsupported&&r.obexStats(stats)==Status::Unsupported);
 // Default disabled: no OBEX storage, records, EIR UUIDs or RFCOMM owner.
 CHECK(r.begin(config)==Status::Ok);CHECK(!r.impl_->obex&&!actor->config.voice_control&&r.pbapPullVcardListing()==Status::Unsupported&&r.obexStats(stats)==Status::Unsupported);
 actor->boot();CHECK(actor->host.classic_.record_count_==2&&actor->host.gap_.config.uuid_count==1);r.end();
 // Validation happens before any allocation or radio start.
 auto bad=[&](void(*edit)(ReceiverConfig&),Status expected){ReceiverConfig c=config;c.obex.opp_enabled=true;edit(c);CHECK(r.begin(c)==expected&&!actor&&detail::memoryStats().current_blocks==0);};
 bad([](ReceiverConfig& c){c.obex.opp_channel=0;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.obex.opp_channel=31;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.spp.enabled=true;c.obex.opp_channel=c.spp.channel;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.obex.event_buffer_bytes=1024;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.obex.opp_service_name=nullptr;},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.obex.opp_enabled=false;c.obex.pbap_enabled=true;c.obex.pbap_service_name="";},Status::InvalidArgument);
 bad([](ReceiverConfig& c){c.enable_a2dp=false;c.ble.enabled=true;},Status::Unsupported);
 // Enabled: lazily allocated port, ring and a dedicated RFCOMM owner.
 ReceiverConfig enabled=config;enabled.obex.opp_enabled=enabled.obex.pbap_enabled=true;enabled.obex.user=&seen;
 enabled.obex.on_object_begin=[](const char* n,const char*,uint32_t l,void* u){auto& s=*static_cast<Seen*>(u);s.name=n;s.length=l;++s.begins;s.thread=std::this_thread::get_id();};
 enabled.obex.on_object_data=[](const uint8_t* p,size_t n,void* u){static_cast<Seen*>(u)->data.append(reinterpret_cast<const char*>(p),n);};
 enabled.obex.on_object_end=[](bool ok,void* u){auto& s=*static_cast<Seen*>(u);s.ok=ok;++s.ends;};
 enabled.obex.on_phonebook_end=[](const PbapResult& res,void* u){auto& s=*static_cast<Seen*>(u);s.result=res;++s.pb_ends;};
 CHECK(r.begin(enabled)==Status::Ok&&r.impl_->obex&&r.impl_->obex->profiles&&actor->config.voice_control==r.impl_->obex->profiles&&actor->config.voice_config.obex==&r.impl_->obex->port);
 CHECK(r.obexStats(stats)==Status::Ok&&stats.opp_enabled&&stats.pbap_enabled&&!stats.opp_connected&&stats.pbap_state==PbapState::Idle);
 // Nonblocking contention, and a pull with no phone connected fails promptly.
 CHECK(contended(r,[&]{return r.pbapPullPhonebook();})==Status::Busy&&contended(r,[&]{return r.pbapAbort();})==Status::Busy);
 CHECK(r.pbapAbort()==Status::InvalidArgument&&r.pbapPullPhonebook(nullptr)==Status::InvalidArgument);
 actor->boot();CHECK(actor->host.classic_.record_count_==4&&actor->host.gap_.config.uuid_count==3);
 CHECK(r.pbapPullPhonebook("telecom/pb.vcf",10,0)==Status::Ok&&r.pbapPullPhonebook()==Status::Busy);
 for(unsigned i=0;i<5;++i)tick(r);CHECK(seen.pb_ends==1&&!seen.result.ok&&seen.result.operation==PbapOperation::PullPhonebook);
 // OPP push from a phone: callbacks run from poll() on the caller's thread.
 connect(r);peerPacket({0x80,0,7,0x10,0,0x04,0x00});for(unsigned i=0;i<10;++i)tick(r);CHECK(inbox==Bytes({0xa0,0,7,0x10,0,0x04,0x00}));inbox.clear();
 const Bytes put={0x82,0,26,0x01,0,15,0,'a',0,'.',0,'t',0,'x',0,'t',0,0,0x49,0,8,'h','e','l','l','o'};
 peerPacket(put);for(unsigned i=0;i<10;++i)tick(r);
 CHECK(inbox==Bytes({0xa0,0,3})&&seen.begins==1&&seen.name=="a.txt"&&seen.length==ObexUnknownLength&&seen.data=="hello"&&seen.ends==1&&seen.ok&&seen.thread==std::this_thread::get_id());
 CHECK(r.obexStats(stats)==Status::Ok&&stats.objects_completed==1&&stats.object_bytes==5);
 r.end();CHECK(!actor&&detail::memoryStats().current_blocks==0);
 // With SPP enabled the OBEX endpoints share its RFCOMM owner (one mux per peer).
 ReceiverConfig both=enabled;both.spp.enabled=true;CHECK(r.begin(both)==Status::Ok&&r.impl_->obex&&!r.impl_->obex->profiles&&r.impl_->serial->profiles);
 CHECK(actor->config.voice_control==r.impl_->serial->profiles&&actor->config.voice_config.obex==&r.impl_->obex->port&&actor->config.voice_config.serial==&r.impl_->serial->port);
 actor->boot();CHECK(actor->host.classic_.record_count_==5&&actor->host.gap_.config.uuid_count==4);
 r.end();CHECK(!actor&&detail::memoryStats().current_blocks==0);
 printf("OBEX_FACADE_PASS checks=%u (real facade+actor; synthetic HCI; no radio)\n",checks);
}
