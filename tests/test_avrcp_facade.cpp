// Breaks caught: public commands blocking on radio_mutex, stale begin/session
// reuse, unsupported BLE-only calls, or returning borrowed metadata storage.
#if defined(__has_include) && __has_include("GigaAvrcp.h")
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#define private public
#include "GigaBredr.h"
#undef private
#include "GigaBredr.cpp"
using namespace giga_bredr;
static unsigned calls=0;
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks&,const BleConfig&){return Status::Ok;}
Status radioBegin(const char*n,uint32_t m,const RadioCallbacks&c){return radioBegin(n,m,c,{});}
void radioPoll(){}void radioEnd(){}RadioStats radioStats(){return{};}BleStats radioBleStats(){return{};}
Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}bool radioBleReadWrite(BleWrite&){return false;}
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}Status radioBleResumeAdvertising(){return Status::Unsupported;}
AvrcpStatus radioAvrcpStatus(){AvrcpStatus s;s.connected=true;s.generation=7;s.playback_status=1;return s;}
AvrcpMetadata radioAvrcpMetadata(){AvrcpMetadata m;m.generation=7;m.valid_attributes=1;std::memcpy(m.title,"track",6);return m;}
Status radioAvrcpCommand(AvrcpCommand,uint32_t generation,uint32_t&id){++calls;if(generation!=7)return Status::InvalidArgument;id=42;return Status::Ok;}
Status radioAvrcpCancel(uint32_t generation,uint32_t id){++calls;return generation==7&&id==42?Status::Ok:Status::InvalidArgument;}
Status i2sBegin(uint32_t){return Status::Unsupported;}Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&,uint16_t){return Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t){return 0;}void i2sEnd(){}OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}PcmRecoverySnapshot i2sRecovery(){return{};}
}}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);return 2;}}while(0)
int main(){GigaBredr r;uint32_t id=99;CHECK(r.avrcpCommand(AvrcpCommand::Play,7,id)==Status::Unsupported&&id==99);
 ReceiverConfig c;c.enable_i2s=false;CHECK(r.begin(c)==Status::Ok);CHECK(r.avrcpStatus().generation==7);auto m=r.avrcpMetadata();CHECK(!std::strcmp(m.title,"track"));m.title[0]='X';CHECK(r.avrcpMetadata().title[0]=='t');
 for(bool cancel:{false,true}){std::promise<Status> p;auto done=p.get_future();r.impl_->radio_mutex.lock();
  std::thread caller([&]{p.set_value(cancel?r.avrcpCancel(7,42):r.avrcpCommand(AvrcpCommand::Play,7,id));});
  bool early=done.wait_for(std::chrono::seconds(1))==std::future_status::ready;r.impl_->radio_mutex.unlock();auto s=done.get();caller.join();
  CHECK(early&&s==Status::Busy&&calls==0&&id==99);}
 CHECK(r.avrcpCommand(AvrcpCommand::Play,7,id)==Status::Ok&&id==42);CHECK(r.avrcpCancel(7,id)==Status::Ok);r.end();
 CHECK(!r.avrcpStatus().connected&&!r.avrcpMetadata().generation);CHECK(r.avrcpCancel(7,id)==Status::Unsupported);
 c.enable_a2dp=false;CHECK(r.begin(c)==Status::Ok);CHECK(!r.avrcpStatus().connected&&!r.avrcpMetadata().generation);
 unsigned before=calls;CHECK(r.avrcpCommand(AvrcpCommand::Play,7,id)==Status::Unsupported&&calls==before);r.end();
 CHECK(detail::memoryStats().current_bytes==0);std::puts("PASS AVRCP facade ownership, copied snapshots and bounded contention");}
#else
#include <stdio.h>
int main(){puts("MISSING_TYPED_AVRCP_FACADE");return 1;}
#endif
