// Compile the complete Arduino owner and actual UART adapter. Only RTOS time,
// serial availability and the output device are native boundaries.
#include "support/owned_radio/OwnerRtos.h"
#include <type_traits>
#include <utility>
#define private public
#include "../src/GigaBredr.h"
#include "../src/owned/OwnedHost.h"
#undef private
#include "../src/GigaBredr.cpp"
#define OWNED_RADIO_SERVICE_MAIN owned_radio_service_reference_main
#include "test_owned_radio_service.cpp"
#undef OWNED_RADIO_SERVICE_MAIN

namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
Status i2sBegin(uint32_t){return Status::Unsupported;}
Status i2sBegin(uint32_t,uint16_t){return Status::Unsupported;}
size_t i2sWrite(const int32_t*,size_t){return 0;}
void i2sEnd(){}
OutputStats i2sStats(){return{};}
OutputTimingSnapshot i2sTiming(){return{};}
PcmRecoverySnapshot i2sRecovery(){return{};}
}}
namespace wt=owner_wake_test;
static GigaBredr::Impl* owner;
static unsigned signals,error_count;
static uint32_t drained_at,hnc_at;
static bool inject_completion;

// Keep the old source runnable: RED must show the real900us handoff, rather
// than failing to compile because the new private member is absent.
template<class T,class=void>struct OwnerWake {
    static constexpr bool available=false;
    static void signal(void*){}
};
template<class T>struct OwnerWake<T,std::void_t<decltype(&T::onOwnerWake)>> {
    static constexpr bool available=true;
    static void signal(void*p){T::onOwnerWake(p);}
};
[[maybe_unused]]static void wake(void*p){++signals;OwnerWake<GigaBredr::Impl>::signal(p);}
template<class T,class=void>struct BindWake {static bool apply(T&,void*){return false;}};
template<class T>struct BindWake<T,std::void_t<decltype(std::declval<T&>().owner_wake)>> {
    static bool apply(T&cb,void*p){cb.owner_wake=wake;cb.user=p;cb.diagnostic=nullptr;return true;}
};
static bool sendHnc(const uint8_t*p,size_t n,void*u){
    if(n==9&&p[0]==1&&p[1]==0x35&&p[2]==0x0c){
        CHECK(p[4]==1&&p[5]==0x26&&p[7]==1);
        hnc_at=micros();owner->running.store(false);
    }
    return radio::sendH4(p,n,u);
}
static void waitForTx(uint32_t ms,uint32_t*bits){
    const auto deadline=micros()+1000*ms;
    if(inject_completion){
        wt::advance(100);drained_at=micros();inject_completion=false;
        radio::txIrq(); // actual mailbox finish and actual owner notification
    }
    if(!bits||!(*bits&1))wt::timeUs(deadline);
}
static bool realHandoff(){
    ReceiverConfig cfg;GigaBredr::Impl target(cfg);owner=&target;
    Adapter a;auto&f=a.app;f.boot();f.leConnect();f.ready=false;
    first(f);f.poll();CHECK(f.host.rx_.links[1].pending==1);
    const bool bound=BindWake<radio::RadioCallbacks>::apply(a.state->callbacks,&target);
    f.host.hooks_.txReady=radio::txReady;
    f.host.hooks_.sendH4=sendHnc;f.host.hooks_.user=a.state;
    const uint8_t command[]={1,0x16,0x0c,0};
    wt::timeUs(f.now*1000);CHECK(radio::sendH4(command,sizeof command,a.state));
    target.running.store(true);wt::wait_hook=waitForTx;inject_completion=true;
    target.hostMain();wt::wait_hook=nullptr;
    CHECK(f.host.rx_.links[1].pending==0&&!f.host.radioStats().transport_errors);
    printf("Actual owner+adapter+Core HNC handoff=%uus (controlled native clock)\n",hnc_at-drained_at);
    return bound&&hnc_at==drained_at;
}
[[maybe_unused]]static void stopErrorLoop(){if(++error_count==3)owner->running.store(false);}
template<class T>static void focusedChecks(){
    if constexpr(OwnerWake<T>::available){
        ReceiverConfig cfg;T target(cfg);owner=&target;signals=0;
        {
            Adapter a(false);auto&c=*a.state;
            CHECK(BindWake<radio::RadioCallbacks>::apply(c.callbacks,&target));
            uint8_t packet[130]={1,0x16,0x0c,126};
            CHECK(radio::sendH4(packet,sizeof packet,&c));
            for(unsigned i=0;i<2;++i){owned_radio_test::write_allowance=64;radio::txIrq();CHECK(!signals);}
            owned_radio_test::write_allowance=0;radio::txIrq();CHECK(!signals&&c.tx_pos==128);
            owned_radio_test::write_allowance=64;radio::txIrq();CHECK(signals==1&&c.tx.empty());
            radio::txIrq();CHECK(signals==1); // spurious empty IRQ
            const uint8_t short_packet[]={1,0x16,0x0c,0};
            CHECK(radio::sendH4(short_packet,sizeof short_packet,&c));radio::txIrq();
            CHECK(signals==2&&target.host_events.get()==1); // coalesced sticky bit
            CHECK(target.host_events.wait_any(1,0,true)==1&&target.host_events.get()==0);
            owned_radio_test::input_bytes=100;radio::uartIrq();radio::uartIrq();
            CHECK(c.rx_write==100&&signals==2); // RX byte bursts do not wake
            CHECK(radio::sendH4(short_packet,sizeof short_packet,&c));
            owned_radio_test::write_failure=true;radio::txIrq();owned_radio_test::write_failure=false;
            CHECK(signals==3&&c.tx_failed&&!c.uart->tx_callback.function);
            radio::radioPoll();CHECK(!radio::context&&radio::radioStats().transport_errors==1);
            CHECK(target.host_events.wait_any(1,0,true)==1); // Impl outlives Context
        }
        wt::error_hook=stopErrorLoop;
        for(auto error:{osFlagsErrorResource,osFlagsErrorTimeout}){
            wt::wait_error=error;wt::sleeps=error_count=0;wt::timeUs(0);
            target.running.store(true);target.hostMain();
            CHECK(micros()==3000&&wt::sleeps==(error==osFlagsErrorTimeout?0u:3u));
        }
        wt::wait_error=0;wt::error_hook=nullptr;
    }
}
int main(){
    if(!realHandoff()){puts("RED: TX completion waited for the1ms timer");return 2;}
    focusedChecks<GigaBredr::Impl>();
    printf("PASS owner wake realHNC/coalescing/CTS/RX/error/lifetime %u checks\n",checks);
}

namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins&){return i2sBegin(rate,ms);}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
}}
