#include <atomic>
#include <future>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define private public
#include "GigaBredr.h"
#undef private
#include "GigaBredr.cpp"
using namespace giga_bredr;
namespace probe {void(*beforeLock)(void*)=nullptr;void(*sleepHook)(unsigned)=nullptr;}
static std::atomic<uint32_t> ticks{100};uint32_t micros(){return ticks.load();}
static std::atomic<unsigned> checks{0};
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"CHECK line%d %s\n",__LINE__,#x);std::abort();}}while(0)
struct Scenario {GigaBredr* receiver=nullptr;bool replace=false,same=false,shutdown=false,stall=false;std::thread::id hostId;std::promise<void> sleeping,attempted,controlDone,resume;std::shared_future<void> resumeFuture;std::atomic<bool> firstSleep{true},attemptNotified{false},radioOnce{false};std::atomic<unsigned> writes{0};void* watched=nullptr;Scenario():resumeFuture(resume.get_future().share()){} };
static Scenario* current=nullptr;static detail::RadioCallbacks callbacks;
static CodecConfig format(uint32_t rate){CodecConfig c;c.codec=Codec::LhdcV5;c.sample_rate=rate;c.channels=2;c.bits_per_sample=24;c.frame_duration_us=5000;return c;}
namespace giga_bredr {namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::LhdcV5);}
static Status open(void**p,const CodecConfig&c){*p=allocate(sizeof(uint32_t));if(!*p)return Status::OutOfMemory;*static_cast<uint32_t*>(*p)=c.sample_rate;return Status::Ok;}
static void close(void*p){release(p);}
static DecodeResult decode(void*p,const uint8_t*,size_t n,int32_t*pcm,size_t cap){size_t frames=*static_cast<uint32_t*>(p)/200;CHECK(cap>=frames);for(size_t i=0;i<frames*2;++i)pcm[i]=int32_t(i);return {Status::Ok,n,frames};}
const DecoderOps* codecOperations(Codec c){static DecoderOps ops={open,decode,close};return c==Codec::LhdcV5?&ops:nullptr;}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig&){callbacks=c;return Status::Ok;}
Status radioBegin(const char*n,uint32_t m,const RadioCallbacks&c){return radioBegin(n,m,c,{});}
void radioPoll(){if(!current||current->radioOnce.exchange(true))return; if(current->replace){CHECK(callbacks.configure(format(96000),callbacks.user)==Status::Ok);callbacks.stream(true,callbacks.user);}else if(current->same||current->shutdown||current->stall){CHECK(callbacks.configure(format(192000),callbacks.user)==Status::Ok);if(current->shutdown)current->receiver->impl_->running.store(false);}else callbacks.stream(false,callbacks.user);current->controlDone.set_value();}
void radioEnd(){callbacks={};}RadioStats radioStats(){return{};}BleStats radioBleStats(){return{};}Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}bool radioBleReadWrite(BleWrite&){return false;}
Status i2sBegin(uint32_t){return Status::Ok;}Status i2sBegin(uint32_t,uint16_t){return Status::Ok;}Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Ok;}
size_t i2sWrite(const int32_t* p,size_t n){unsigned count=++current->writes;if(count==1||current->stall)return 0;volatile int32_t sum=0;for(size_t i=0;i<n*2;++i)sum^=p[i];(void)sum;return n;}
void i2sEnd(){}OutputStats i2sStats(){OutputStats s;s.initialized=s.running=true;return s;}OutputTimingSnapshot i2sTiming(){return{};}PcmRecoverySnapshot i2sRecovery(){return{};}
}}
static void blocked(void*mutex){if(current&&mutex==current->watched&&std::this_thread::get_id()==current->hostId&&!current->attemptNotified.exchange(true))current->attempted.set_value();}
static void sleeping(unsigned ms){if(current->firstSleep.exchange(false)){current->sleeping.set_value();current->resumeFuture.wait();}ticks.fetch_add(ms*1000);}
static int exercise(const char* action){const bool replace=!strcmp(action,"replace");Scenario scenario;current=&scenario;scenario.replace=replace;scenario.same=!strcmp(action,"same");scenario.shutdown=!strcmp(action,"shutdown");scenario.stall=!strcmp(action,"stall");ticks.store(100);GigaBredr receiver;scenario.receiver=&receiver;
 ReceiverConfig options;options.enable_i2s=true;options.codecs=codecBit(Codec::LhdcV5);options.apply_remote_volume=false;options.queue_packets=2;options.startup_packets=1;
 CHECK(receiver.begin(options)==Status::Ok);CHECK(callbacks.configure(format(192000),callbacks.user)==Status::Ok);callbacks.stream(true,callbacks.user);const auto epoch=receiver.impl_->stream_epoch;
 uint8_t packet[34]={0x80,0x60,0,1};packet[11]=1;packet[12]=4;packet[13]=1;packet[14]=9;callbacks.media(packet,sizeof packet,callbacks.user);
 scenario.watched=&receiver.impl_->codec_mutex;probe::beforeLock=blocked;probe::sleepHook=sleeping;
 auto sleepReady=scenario.sleeping.get_future();auto attemptReady=scenario.attempted.get_future();auto controlReady=scenario.controlDone.get_future();
 std::thread decoder([&]{receiver.impl_->processOne();});sleepReady.wait();bool available=receiver.impl_->codec_mutex.trylock();if(available)receiver.impl_->codec_mutex.unlock();
 std::thread host([&]{scenario.hostId=std::this_thread::get_id();receiver.impl_->hostMain();});attemptReady.wait();
 bool radioFree=receiver.impl_->radio_mutex.trylock();if(radioFree)receiver.impl_->radio_mutex.unlock();if(!available)CHECK(!radioFree);
 if(available){CHECK(controlReady.wait_for(std::chrono::seconds(2))==std::future_status::ready);CHECK(scenario.writes==1);if(replace)CHECK(receiver.impl_->stream_epoch!=epoch&&receiver.impl_->playing.load());else if(scenario.same||scenario.shutdown||scenario.stall)CHECK(receiver.impl_->stream_epoch==epoch&&receiver.impl_->playing.load());else CHECK(!receiver.impl_->playing.load());}
 else CHECK(controlReady.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready);
 scenario.resume.set_value();decoder.join();controlReady.wait();receiver.impl_->running.store(false);receiver.impl_->host_events.set(1);host.join();
 probe::beforeLock=nullptr;probe::sleepHook=nullptr;
 if(available){
 unsigned expected=(scenario.same?2u:scenario.stall?107u:1u);CHECK(scenario.writes==expected);
 if(replace){auto t=receiver.impl_->work_timing.snapshot();CHECK(t.packets==0&&t.epoch!=epoch&&t.rejected_epoch==0);}
 else if(scenario.same||scenario.shutdown||scenario.stall){auto t=receiver.impl_->work_timing.snapshot();CHECK(t.packets==1&&t.epoch==epoch);if(scenario.stall)CHECK(t.elapsed.total_us==106000u);}
 CHECK(receiver.impl_->counters.output_dropped_frames==((scenario.shutdown||scenario.stall)?960u:0u));
 }
 receiver.end();CHECK(detail::memoryStats().current_blocks==0);printf("ACTUAL_ARDUINO_FRONTEND action=%s mutex_available_during_wait=%u host_blocked_by_codec=%u writes=%u checks=%u\n",action,unsigned(available),unsigned(!available),scenario.writes.load(),checks.load());current=nullptr;return available?0:3;}
int main(int argc,char**argv){return exercise(argc>1?argv[1]:"stop");}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}
