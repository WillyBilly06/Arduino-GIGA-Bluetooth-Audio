// Regression breaks: continuing a poll after a callback replaces Impl can
// deliver old volume to new user state or consume the new configuration early.
// Exercise actual public GigaBredr; substitute only hardware-facing radio/I2S.
#include "GigaBredr.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include "detail/Memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace giga_bredr;
namespace giga_bredr { namespace detail {
RadioCallbacks callbacks;
Status nextRadioStatus=Status::Ok;
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig&){
    if(nextRadioStatus!=Status::Ok) return nextRadioStatus;
    callbacks=c; return Status::Ok;
}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){}
void radioEnd(){callbacks={};}
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
}}
static unsigned checks=0;
static void require(bool value,const char* message){
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(2);}
}
// Real memory-provider boundary, deterministic ABA: every successive idle
// receiver Impl gets the same aligned address. Other codec blocks use malloc.
alignas(64) static unsigned char arena[131072];
static bool arenaActive=false;
static unsigned arenaReuses=0;
static void* allocation(size_t n,void*){
    if(!arenaActive){require(n<=sizeof arena,"Impl fits test arena");arenaActive=true;++arenaReuses;return arena;}
    return std::malloc(n);
}
static void deallocation(void* p,void*){
    if(p==arena){require(arenaActive,"balanced arena release");arenaActive=false;}
    else std::free(p);
}
enum class Trigger { Diagnostic,Metadata,Configuration,Volume };
enum class Action { Restart,End,RadioFailure,AllocationFailure,Busy,Invalid };
struct Calls {unsigned diagnostic=0,metadata=0,configuration=0,volume=0;uint8_t lastVolume=0;uint32_t lastRate=0;};
struct Context {GigaBredr receiver;Calls oldCalls,newCalls;Trigger trigger;Action action;bool acted=false;void* previousImpl=nullptr;};
static void newDiagnostic(const char*,void* p){++static_cast<Calls*>(p)->diagnostic;}
static void newMetadata(uint8_t a,const char* text,size_t n,void* p){
    require(a==2&&n==3&&std::memcmp(text,"new",3)==0,"new metadata payload/user");++static_cast<Calls*>(p)->metadata;
}
static void newConfiguration(const CodecConfig& c,void* p){auto& x=*static_cast<Calls*>(p);++x.configuration;x.lastRate=c.sample_rate;}
static void newVolume(uint8_t v,void* p){auto& x=*static_cast<Calls*>(p);++x.volume;x.lastVolume=v;}
static ReceiverConfig fresh(Calls& calls){
    ReceiverConfig c;c.enable_i2s=false;c.user=&calls;c.diagnostic=newDiagnostic;c.metadata_changed=newMetadata;
    c.configuration_changed=newConfiguration;c.volume_changed=newVolume;return c;
}
static void act(Context& c,Trigger caller){
    if(c.trigger!=caller||c.acted)return;c.acted=true;
    if(c.action==Action::Busy){require(c.receiver.begin(fresh(c.newCalls))==Status::Busy,"reentrant begin remains Busy");return;}
    if(c.action==Action::Invalid){ReceiverConfig invalid=fresh(c.newCalls);invalid.maximum_packet_bytes=1;
        // Busy takes precedence while this receiver is still alive.
        require(c.receiver.begin(invalid)==Status::Busy,"invalid begin while alive remains Busy");return;}
    c.receiver.end();
    if(c.action==Action::End)return;
    if(c.action==Action::AllocationFailure){
        detail::setAllocationFailureCountdown(0);
        require(c.receiver.begin(fresh(c.newCalls))==Status::OutOfMemory,"failed replacement allocation");
        detail::setAllocationFailureCountdown(-1);return;
    }
    if(c.action==Action::RadioFailure){
        detail::nextRadioStatus=Status::HardwareError;
        require(c.receiver.begin(fresh(c.newCalls))==Status::HardwareError,"installed replacement fails radio begin");
        detail::nextRadioStatus=Status::Ok;return;
    }
    require(c.receiver.begin(fresh(c.newCalls))==Status::Ok,"successful callback restart");
    require(detail::callbacks.user==c.previousImpl,"deterministic same-address Impl reuse");
    require(c.receiver.stats().absolute_volume==127,"replacement starts at default volume");
    CodecConfig next;next.sample_rate=32000;
    require(detail::callbacks.configure(next,detail::callbacks.user)==Status::Ok,"new configuration remains pending");
    detail::callbacks.metadata(2,"new",3,detail::callbacks.user);
}
static void oldDiagnostic(const char* text,void* p){
    auto& c=*static_cast<Context*>(p);require(std::strcmp(text,"old")==0,"old diagnostic payload");
    ++c.oldCalls.diagnostic;act(c,Trigger::Diagnostic);
}
static void oldMetadata(uint8_t a,const char* text,size_t n,void* p){
    auto& c=*static_cast<Context*>(p);require(a==1&&n==3&&std::memcmp(text,"old",3)==0,"old metadata payload");
    ++c.oldCalls.metadata;act(c,Trigger::Metadata);
}
static void oldConfiguration(const CodecConfig& config,void* p){
    auto& c=*static_cast<Context*>(p);require(config.sample_rate==16000,"old configuration payload");
    ++c.oldCalls.configuration;act(c,Trigger::Configuration);
}
static void oldVolume(uint8_t v,void* p){auto& c=*static_cast<Context*>(p);++c.oldCalls.volume;c.oldCalls.lastVolume=v;act(c,Trigger::Volume);}
static void run(Trigger trigger,Action action){
    Context c;c.trigger=trigger;c.action=action;
    ReceiverConfig initial;initial.enable_i2s=false;initial.user=&c;initial.diagnostic=oldDiagnostic;
    initial.metadata_changed=oldMetadata;initial.configuration_changed=oldConfiguration;initial.volume_changed=oldVolume;
    require(c.receiver.begin(initial)==Status::Ok,"initial begin");
    c.previousImpl=detail::callbacks.user;
    CodecConfig old;old.sample_rate=16000;
    require(detail::callbacks.configure(old,detail::callbacks.user)==Status::Ok,"old configure");
    detail::callbacks.volume(64,detail::callbacks.user);
    if(trigger==Trigger::Diagnostic)detail::callbacks.diagnostic("old",detail::callbacks.user);
    if(trigger==Trigger::Metadata)detail::callbacks.metadata(1,"old",3,detail::callbacks.user);
    c.receiver.poll();
    require(c.acted,"selected callback ran");
    if(action==Action::Restart){
        require(c.newCalls.configuration==0,"old poll must not consume new configuration");
        require(c.newCalls.metadata==0&&c.newCalls.diagnostic==0,"old poll must not consume new event queue");
        require(c.newCalls.volume==0,"old volume must not reach new receiver user");
        require(c.receiver.stats().absolute_volume==127,"new volume stays 127 after old poll");
        c.receiver.poll();
        require(c.newCalls.configuration==1&&c.newCalls.lastRate==32000,"new configuration delivered next poll once");
        require(c.newCalls.metadata==1&&c.newCalls.volume==0,"new pending event delivered next poll without old volume");
        detail::callbacks.volume(101,detail::callbacks.user);c.receiver.poll();c.receiver.poll();
        require(c.newCalls.volume==1&&c.newCalls.lastVolume==101,"new volume delivered once to new user");
    }else if(action==Action::Busy||action==Action::Invalid){
        require(c.oldCalls.configuration==1&&c.oldCalls.volume==1&&c.oldCalls.lastVolume==64,"Busy begin preserves pending old configuration and volume");
        require(c.newCalls.volume==0&&c.newCalls.configuration==0,"Busy begin does not install new callbacks");
        c.receiver.poll();require(c.oldCalls.volume==1&&c.oldCalls.configuration==1,"ordinary pending events delivered once");
    }else{
        require(detail::memoryStats().current_bytes==0,"end and failed begin clean all allocations");
        require(c.oldCalls.volume==(trigger==Trigger::Volume?1u:0u),"no old volume after callback teardown");
        c.receiver.poll();
        require(c.receiver.begin(fresh(c.newCalls))==Status::Ok,"owner released after failed begin or end");
        require(c.receiver.stats().absolute_volume==127,"fresh begin after failure has default volume");
    }
    c.receiver.end();require(detail::memoryStats().current_bytes==0&&detail::memoryStats().current_blocks==0&&!arenaActive,"balanced final allocation/accounting");
}
int main(int argc,char** argv){
    require(detail::setMemoryProvider(MemoryKind::Fast,allocation,deallocation,nullptr),"install deterministic provider before allocation");
    if(argc==3)run(static_cast<Trigger>(std::atoi(argv[1])),static_cast<Action>(std::atoi(argv[2])));
    else for(unsigned t=0;t<4;++t)for(unsigned a=0;a<6;++a)run(static_cast<Trigger>(t),static_cast<Action>(a));
    std::printf("PASS callback lifetime checks=%u deterministic_Impl_allocations=%u sizeof_GigaBredr=%zu alignof_GigaBredr=%zu\n",checks,arenaReuses,sizeof(GigaBredr),alignof(GigaBredr));
}

namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins&){return i2sBegin(rate,ms);}
}}
