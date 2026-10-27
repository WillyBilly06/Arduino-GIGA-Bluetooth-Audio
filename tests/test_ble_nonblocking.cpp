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
static unsigned checks, notifyCalls, readCalls;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);std::exit(2); } } while(0)
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status radioBegin(const char*,uint32_t,const RadioCallbacks&,const BleConfig&){return Status::Ok;}
Status radioBegin(const char*n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}
void radioPoll(){} void radioEnd(){}
RadioStats radioStats(){return {};}
BleStats radioBleStats(){BleStats s;s.enabled=s.connected=true;s.generation=7;return s;}
Status radioBleNotify(const uint8_t* p,size_t n){++notifyCalls;return p&&n==3?Status::Ok:Status::InvalidArgument;}
bool radioBleReadWrite(BleWrite& out){++readCalls;out.generation=7;out.length=3;out.data[0]=1;out.data[1]=2;out.data[2]=3;return true;}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t,const I2sPins&){return Status::Unsupported;}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return {};}
OutputTimingSnapshot i2sTiming(){return {};}
PcmRecoverySnapshot i2sRecovery(){return {};}
}}
struct Reply{Status status=Status::Ok;bool read=false;};
static int exercise(const char* which){
    notifyCalls=readCalls=0;
    GigaBredr receiver;
    uint8_t data[]={1,2,3};BleWrite origin;origin.generation=7;origin.length=3;
    std::memcpy(origin.data,data,3);
    CHECK(receiver.bleNotify(data,3)==Status::Unsupported);
    CHECK(receiver.bleNotify(origin)==Status::Unsupported);
    BleWrite out;out.generation=99;out.length=1;out.data[0]=0xaa;
    CHECK(!receiver.bleReadWrite(out)&&out.generation==99);
    ReceiverConfig config;config.enable_i2s=false;
    CHECK(receiver.begin(config)==Status::Ok);
    std::promise<void> entered;
    std::promise<Reply> result;
    auto started=entered.get_future();auto finished=result.get_future();
    // The actual actor's mutex is held by a different thread from the caller.
    receiver.impl_->radio_mutex.lock();
    std::thread caller([&]{
        entered.set_value();Reply r;
        if(!std::strcmp(which,"bytes"))r.status=receiver.bleNotify(data,3);
        else if(!std::strcmp(which,"origin"))r.status=receiver.bleNotify(origin);
        else if(!std::strcmp(which,"read"))r.read=receiver.bleReadWrite(out);
        else std::abort();
        result.set_value(r);
    });
    started.wait();
    const bool returnedWhileBusy=finished.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    receiver.impl_->radio_mutex.unlock();
    Reply reply=finished.get();caller.join();
    // Release/join even on the old RED path, so a failed test cannot hang.
    if(!returnedWhileBusy){std::fprintf(stderr,"data API blocked on radio owner\n");receiver.end();return 3;}
    CHECK(notifyCalls==0&&readCalls==0);
    if(!std::strcmp(which,"read"))CHECK(!reply.read&&out.generation==99&&out.length==1&&out.data[0]==0xaa);
    else CHECK(reply.status==Status::Busy);
    CHECK(receiver.bleNotify(data,3)==Status::Ok&&notifyCalls==1);
    CHECK(receiver.bleNotify(origin)==Status::Ok&&notifyCalls==2);
    origin.generation=8;
    CHECK(receiver.bleNotify(origin)==Status::InvalidArgument&&notifyCalls==2);
    CHECK(receiver.bleReadWrite(out)&&out.generation==7&&out.length==3&&out.data[2]==3&&readCalls==1);
    receiver.end();
    CHECK(detail::memoryStats().current_bytes==0&&detail::memoryStats().current_blocks==0);
    std::printf("Actual BLE facade %s: %u checks PASS\n",which,checks);
    return 0;
}

int main(int argc,char** argv){
    if(argc==2)return exercise(argv[1]);
    CHECK(argc==1);
    for(const char* which:{"bytes","origin","read"}){
        int status=exercise(which);if(status)return status;
    }
    return 0;
}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}

// New AVRCP facade boundary is unavailable in this pre-existing hardware stub.
namespace giga_bredr { namespace detail {
AvrcpStatus radioAvrcpStatus(){return{};}
AvrcpMetadata radioAvrcpMetadata(){return{};}
Status radioAvrcpCommand(AvrcpCommand,uint32_t,uint32_t&){return Status::Unsupported;}
Status radioAvrcpCancel(uint32_t,uint32_t){return Status::Unsupported;}
}}
