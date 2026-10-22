#include "support/owned_radio/OwnerRtos.h"
#define private public
#include "../src/GigaBredr.h"
#undef private
#include "../src/GigaBredr.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace giga_bredr;
namespace wt=owner_wake_test;
static unsigned checks;
static GigaBredr*receiver;
static bool producer_alive;
static detail::RadioCallbacks producer;
#define CHECK(x)do{++checks;if(!(x)){fprintf(stderr,"line%d %s\n",__LINE__,#x);abort();}}while(0)
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks&cb,const BleConfig&){
    CHECK(cb.owner_wake&&cb.user==receiver->impl_);
    producer=cb;producer_alive=true;cb.owner_wake(cb.user);return Status::Ok;
}
Status radioBegin(const char*n,uint32_t m,const RadioCallbacks&cb){return radioBegin(n,m,cb,{});}
void radioPoll(){}
void radioEnd(){CHECK(wt::joins==2);producer_alive=false;producer={};}
RadioStats radioStats(){return{};}
BleStats radioBleStats(){return{};}
Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}
bool radioBleReadWrite(BleWrite&){return false;}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}
PcmRecoverySnapshot i2sRecovery(){return{};}
}}
static void starting(){CHECK(producer_alive&&receiver->impl_->host_events.get()==1);}
static void joining(int priority){
    CHECK(producer_alive&&!receiver->impl_->running.load());
    if(priority==osPriorityAboveNormal){
        CHECK(receiver->impl_->host_events.get()==1); // signal precedes host join
        producer.owner_wake(producer.user); // late producer still has a live Impl
    }
}
static void destroyed(){CHECK(!producer_alive&&wt::joins==2);}
int main(){
    for(unsigned fail:{0u,2u}){
        GigaBredr value;receiver=&value;wt::starts=wt::joins=0;wt::fail_start=fail;
        wt::start_hook=starting;wt::join_hook=joining;wt::destroy_hook=destroyed;
        ReceiverConfig cfg;cfg.enable_i2s=false;
        CHECK(value.begin(cfg)==(fail?Status::OutOfMemory:Status::Ok));
        value.end();CHECK(!value.impl_&&detail::memoryStats().current_blocks==0);
        wt::start_hook=nullptr;wt::join_hook=nullptr;wt::destroy_hook=nullptr;
    }
    printf("PASS actual public startup/early wake/join/producer close/flag destruction %u checks\n",checks);
}

namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins&){return i2sBegin(rate,ms);}
}}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}
