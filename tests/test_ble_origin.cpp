// Break caught: the exact Receiver loop retries a copied generation7 write
// as a notification after disconnect/reconnect installs generation8.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "GigaBredr.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include "detail/Memory.h"
#include "owned/LeProfiles.h"
#include <USB/PluggableUSBSerial.h>
TestSerial _SerialUSB;
uint32_t millis(){return 0;}
void delay(unsigned){}
using namespace giga_bredr;
static unsigned checks=0;
static void check(bool c,const char* what){++checks;if(!c){std::fprintf(stderr,"FAIL %s\n",what);std::exit(2);}}
namespace fixture {
static bool canSend=false;
static std::vector<std::vector<uint8_t>> notifications;
static bth_cmdq_t commands;
static bth_control_tx_t replies;
static detail::LeProfiles ble;
static void drain(){
    const auto s=ble.stats();
    for(unsigned i=0;i<replies.peers[1].count;++i){const auto& v=replies.slots[replies.peers[1].order[(replies.peers[1].read+i)%BTH_CONTROL_SLOTS]];
        if(v.length>=3&&v.data[0]==0x1b)notifications.emplace_back(v.data+3,v.data+v.length);}
    check(bth_control_tx_detach(&replies,1,s.generation),"drain peer ownership");check(bth_control_tx_attach(&replies,1,s.connection_handle,s.generation),"restore peer ownership");
}
static void pump(){ble.poll(0,false);if(canSend)drain();}
static void connect(uint32_t generation){check(bth_control_tx_attach(&replies,1,0x42,generation),"owned control attach");check(ble.attach(generation,0x42),"owned attach");uint8_t cccd[]={0x52,12,0,1,0};check(ble.receive(generation,4,cccd,sizeof cccd,0),"owned subscribe");}
static void disconnect(uint32_t generation){ble.detach(generation);check(bth_control_tx_detach(&replies,1,generation),"owned detach");}
static void write(uint8_t a,uint8_t b){uint8_t value[]={0x52,9,0,a,b};check(ble.receive(ble.stats().generation,4,value,sizeof value,0),"owned RX write");value[3]=0xee;}
static void block(){while(bth_control_tx_available(&replies,1)){bth_control_reservation_t v;check(bth_control_tx_reserve(&replies,1,&v)==BTH_OK,"fill actual control capacity");v.data[0]=0;check(bth_control_tx_commit(&replies,v.token,4,1,23)==BTH_OK,"commit actual capacity");}}
static void readable(uint8_t a,uint8_t b){uint8_t req[]={0x0a,11,0};check(ble.receive(ble.stats().generation,4,req,sizeof req,0),"read TX");const auto& v=replies.slots[replies.peers[1].order[replies.peers[1].read]];check(v.length==3&&v.data[0]==0x0b&&v.data[1]==a&&v.data[2]==b,"intentional readable TX value persistence");drain();}
}
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char* n,uint32_t,const RadioCallbacks&,const BleConfig& c){
bth_cmdq_init(&fixture::commands);bth_control_tx_init(&fixture::replies);return fixture::ble.begin(c,n,fixture::commands,fixture::replies);
}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){fixture::pump();}
void radioEnd(){}
RadioStats radioStats(){return{};}
BleStats radioBleStats(){return fixture::ble.stats();}
Status radioBleNotify(const uint8_t* p,size_t n){return fixture::ble.notify(p,n);}
bool radioBleReadWrite(BleWrite& out){return fixture::ble.readWrite(out);}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}
PcmRecoverySnapshot i2sRecovery(){return{};}
}}
#include "../examples/Receiver/Receiver.ino"
static void subscription(bool enabled){
    uint8_t cccd[]={0x52,12,0,uint8_t(enabled),0};check(fixture::ble.receive(fixture::ble.stats().generation,4,cccd,sizeof cccd,0),"owned subscription update");
}
static BleWrite copied(uint8_t a,uint8_t b){
    fixture::write(a,b);BleWrite value;check(receiver.bleReadWrite(value),"public RX pop");
    check(value.generation==receiver.bleStats().generation&&value.length==2&&value.data[0]==a&&value.data[1]==b,"copied input payload and origin");
    BleWrite untouched;untouched.generation=99;check(!receiver.bleReadWrite(untouched)&&untouched.generation==99,"empty pop leaves caller value unchanged");return value;
}
static void verify(size_t index,uint8_t a,uint8_t b){check(fixture::notifications.size()>index&&fixture::notifications[index].size()==2&&fixture::notifications[index][0]==a&&fixture::notifications[index][1]==b,"accepted notification retains copied payload");}
int main(int argc,char** argv){
    check(argc==2,"one test case requested");const char* name=argv[1];
    if(std::strcmp(name,"stopped")==0){BleWrite origin;origin.generation=1;check(receiver.bleNotify(origin)==Status::Unsupported,"stopped origin API");check(receiver.bleNotify(origin.data,0)==Status::Unsupported,"stopped byte API");check(!receiver.bleReadWrite(origin)&&!receiver.bleStats().connected,"stopped facade unchanged");}
    else{
        ReceiverConfig options;options.enable_i2s=false;check(receiver.begin(options)==Status::Ok,"actual public begin");
        if(std::strcmp(name,"actual_loop_reconnect")==0){
            fixture::connect(1);fixture::write(0xa1,0x11);loop();fixture::write(0xa2,0x22);fixture::block();loop();
            fixture::disconnect(1);fixture::connect(2);fixture::readable(0xa1,0x11);fixture::canSend=true;fixture::pump();check(fixture::notifications.empty(),"backend clears old pending work");
            loop();fixture::pump();check(fixture::notifications.empty(),"actual loop drops stale pending echo");
            fixture::write(0xb3,0x44);loop();fixture::pump();verify(0,0xb3,0x44);check(fixture::notifications.size()==1,"actual loop continues with replacement peer");
        }else if(std::strcmp(name,"copy_retry")==0){
            fixture::connect(1);auto first=copied(0x11,0x12);check(receiver.bleNotify(first)==Status::Ok,"same-peer initial origin accepted");first.data[0]=0xee;
            auto second=copied(0x21,0x22);fixture::block();check(receiver.bleNotify(second)==Status::Busy,"pending notification preserves Busy retry");
            fixture::canSend=true;fixture::pump();fixture::pump();verify(0,0x11,0x12);fixture::readable(0x11,0x12);
            check(receiver.bleNotify(second)==Status::Ok,"same-peer retry accepted after capacity");second.data[0]=0xee;fixture::pump();verify(1,0x21,0x22);
            check(fixture::notifications.size()==2&&receiver.bleStats().notifications_queued==2,"two same-peer notifications admitted once");
        }else if(std::strcmp(name,"stale_generation")==0){
            const uint8_t initial[]={0x5a,1};check(receiver.bleNotify(initial,2)==Status::Ok,"byte API retains disconnected readable value");fixture::connect(1);fixture::readable(0x5a,1);
            BleWrite invalid;invalid.length=2;invalid.data[0]=0xee;check(receiver.bleNotify(invalid)==Status::InvalidArgument,"zero origin rejected");
            invalid.generation=2;check(receiver.bleNotify(invalid)==Status::InvalidArgument,"wrong live generation rejected");invalid.generation=1;invalid.length=513;check(receiver.bleNotify(invalid)==Status::InvalidArgument,"current origin preserves backend value length validation");fixture::readable(0x5a,1);
            auto old=copied(0xa1,0x11);fixture::disconnect(1);check(receiver.bleNotify(old)==Status::InvalidArgument,"disconnected origin rejected without readable update");
            fixture::connect(2);check(receiver.bleNotify(old)==Status::InvalidArgument,"same handle replacement rejects previous generation");fixture::readable(0x5a,1);
            auto current=copied(0xb2,0x22);check(receiver.bleNotify(current)==Status::Ok,"replacement peer's own origin succeeds");fixture::canSend=true;fixture::pump();verify(0,0xb2,0x22);check(fixture::notifications.size()==1,"no stale notification admitted");
        }else if(std::strcmp(name,"byte_value_semantics")==0){
            uint8_t initial[]={0x31,0x32};check(receiver.bleNotify(initial,2)==Status::Ok,"disconnected byte value copied");initial[0]=0xee;fixture::connect(1);fixture::readable(0x31,0x32);
            subscription(false);uint8_t current[]={0x41,0x42};check(receiver.bleNotify(current,2)==Status::Ok,"unsubscribed byte value update");current[0]=0xee;fixture::readable(0x41,0x42);
            auto origin=copied(0x51,0x52);check(receiver.bleNotify(origin)==Status::Ok,"current connected origin preserves value-only behavior without subscription");origin.data[0]=0xee;fixture::readable(0x51,0x52);
            fixture::canSend=true;fixture::pump();check(fixture::notifications.empty(),"Ok without subscription does not promise a notification");subscription(true);fixture::pump();check(fixture::notifications.empty(),"subscription does not itself resubmit readable values");
        }else if(std::strcmp(name,"disconnect_after_admission")==0){
            fixture::connect(1);auto old=copied(0x61,0x62);check(receiver.bleNotify(old)==Status::Ok,"old peer accepts notification before disconnect");fixture::disconnect(1);fixture::connect(2);fixture::readable(0x61,0x62);
            fixture::canSend=true;fixture::pump();check(fixture::notifications.empty(),"existing backend teardown cancels old pending notification");
            check(receiver.bleNotify(old)==Status::InvalidArgument,"old origin retry rejected after replacement");
        }else check(false,"known test name");
        receiver.end();
    }
    check(detail::memoryStats().current_bytes==0&&detail::memoryStats().current_blocks==0,"zero allocations at case end");
    std::printf("PASS %s checks=%u\n",name,checks);
}
