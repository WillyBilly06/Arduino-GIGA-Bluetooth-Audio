#include "../RadioBuildConfig.h"
#if !GIGA_BREDR_USE_BTSTACK && defined(ARDUINO)
#include "GigaRadio.h"
#include "GigaAvrcp.h"
#include "RadioProtocol.h"
#include "TxMailbox.h"
#include "../owned/OwnedHost.h"
#include "../detail/Memory.h"
#include "../detail/TxAdmissionOrder.h"
#include "../detail/OwnedUartBaudPolicy.h"
#include <Arduino.h>
#include <mbed.h>
#include <drivers/UnbufferedSerial.h>
#include <new>
#include <stdio.h>
#include <string.h>
extern "C" {
extern const uint8_t brcm_patchram_buf[];
extern const uint32_t brcm_patch_ram_length;
extern const char brcm_patch_version[];
extern const uint8_t brcm_patchram_format;
}
#ifndef GIGA_BREDR_UART_BAUD
#define GIGA_BREDR_UART_BAUD 2000000
#endif
static_assert(giga_bredr::detail::ownedUartBaudSupported(GIGA_BREDR_UART_BAUD),
              "Unsupported GIGA UART baud");
namespace giga_bredr { namespace detail {
namespace {
constexpr unsigned RxSize=8192;
struct Context {
    OwnedHost host;
    RadioCallbacks callbacks;
    H4Parser parser;
    alignas(mbed::UnbufferedSerial) uint8_t uart_storage[sizeof(mbed::UnbufferedSerial)];
    mbed::UnbufferedSerial* uart=nullptr;
    gpio_t power,device_wake,host_wake;
    uint8_t rx[RxSize]={};
    TxMailbox tx;
    volatile unsigned rx_write=0,rx_read=0;
    volatile bool rx_paused=false;
    unsigned tx_size=0,tx_pos=0;
    uint32_t tx_failed=0;
    uint32_t started=0,last_byte=0,tx_started=0;
    uint32_t hnc_submitted=0,hnc_submitted_credits=0,hnc_uart=0,hnc_uart_credits=0;
    uint32_t hnc_tc=0,hnc_tc_credits=0,hnc_pending_tc=0,hnc_pending_tc_credits=0;
    uint32_t tx_hnc_credits=0;
    bool powered=false;
};
// Placement construction happens only when this radio is requested. In
// particular an I2S-only sketch does not retain or construct the host actor.
Context* context=nullptr;
RadioStats stopped_stats;
BleStats stopped_ble;
struct Critical {
    Critical() { core_util_critical_section_enter(); }
    ~Critical() { core_util_critical_section_exit(); }
};
void diagnostic(const char* text) {
    if(context && context->callbacks.diagnostic)
        context->callbacks.diagnostic(text,context->callbacks.user);
}
// The callback only sets an Impl-owned event bit. Context can be destroyed by
// radioPoll; the owner's wait state therefore never lives in this allocation.
void notifyOwner(Context& c) {
    if(c.callbacks.owner_wake) c.callbacks.owner_wake(c.callbacks.user);
}
// Mbed SerialBase owns the nonzero IRQ dispatcher context. IRQ work is bounded
// byte movement, with one hardware reader and one ring producer only.
void uartIrq() {
    Context* c=context;
    if(!c || !c->uart) return;
    unsigned budget=64;
    while(budget-- && c->uart->readable()) {
        unsigned next=(c->rx_write+1)&(RxSize-1);
        if(next==c->rx_read) {
            c->rx_paused=true;
            c->uart->attach(mbed::Callback<void()>(),mbed::SerialBase::RxIrq);
            break;
        }
        uint8_t byte;
        if(c->uart->read(&byte,1)!=1) break;
        c->rx[c->rx_write]=byte; __DMB(); c->rx_write=next;
    }
}
// Sole UART TX writer. TXE wakes this callback as each byte becomes writable;
// the host's1ms poll cadence never limits byte or credit-return throughput.
void txIrq() {
    Context* c=context;
    if(!c || !c->uart) return;
    unsigned budget=64;
    while(budget-- && c->uart->writeable()) {
        uint8_t byte;
        if(!c->tx.peek(byte)) {
            c->uart->attach(mbed::Callback<void()>(),mbed::SerialBase::TxIrq);
            return;
        }
        if(c->uart->write(&byte,1)!=1 || !c->tx.advance()) {
            __atomic_store_n(&c->tx_failed,1u,__ATOMIC_RELEASE);
            c->uart->attach(mbed::Callback<void()>(),mbed::SerialBase::TxIrq);
            notifyOwner(*c);
            return;
        }
        ++c->tx_pos;
        if(c->tx.drained()) {
            // Disable the level-triggered TXE source before publishing empty.
            // TC is sampled later by foreground diagnostics; never spin here.
            c->uart->attach(mbed::Callback<void()>(),mbed::SerialBase::TxIrq);
            if(c->tx_hnc_credits) {
                ++c->hnc_uart;c->hnc_uart_credits+=c->tx_hnc_credits;
                ++c->hnc_pending_tc;c->hnc_pending_tc_credits+=c->tx_hnc_credits;c->tx_hnc_credits=0;
            }
            c->tx_size=c->tx_pos=0;
            if(c->tx.finish()) notifyOwner(*c);
            return;
        }
    }
}
bool txReady(void* user) {
    const auto& c=*static_cast<Context*>(user);
    return c.uart && c.tx.empty() && !__atomic_load_n(&c.tx_failed,__ATOMIC_ACQUIRE);
}
// The serialized actor is the sole producer. TX IRQ detaches before publishing
// empty, so no prior consumer can read this copy/preparation before IRQ arm.
struct TxAdmissionIO {
    Context& c;
    const uint8_t* packet;
    size_t length;
    bool copyIfReady() { return txReady(&c) && c.tx.submit(packet,length); }
    void prepare() {
        c.tx_pos=0;c.tx_size=unsigned(length);c.tx_started=millis();
        c.tx_hnc_credits=0;
        if(length>=9 && packet[0]==1 && packet[1]==0x35 && packet[2]==0x0c &&
           length==size_t(5+4*packet[4]) && packet[3]==length-4) {
            for(unsigned i=0;i<packet[4];++i) c.tx_hnc_credits+=uint32_t(packet[7+4*i])|(uint32_t(packet[8+4*i])<<8);
            ++c.hnc_submitted;c.hnc_submitted_credits+=c.tx_hnc_credits;
        }
    }
    struct ArmGuard : Critical {
        explicit ArmGuard(TxAdmissionIO&) {}
    };
    void arm() { c.uart->attach(mbed::callback(txIrq),mbed::SerialBase::TxIrq); }
    void afterArm() {}
};
bool sendH4(const uint8_t* packet,size_t length,void* user) {
    auto& c=*static_cast<Context*>(user);
    TxAdmissionIO io{c,packet,length};
    return admitTxBeforeIrqArm(io);
}
bool setBaud(uint32_t baud,void* user) {
    auto& c=*static_cast<Context*>(user);
    { Critical guard;
      if(!txReady(user) || !(UART7->ISR&USART_ISR_TC)) return false;
    }
    // Empty is published only after TX IRQ detachment. The serialized actor is
    // the sole producer, so HAL register/ack waits need no global IRQ mask.
    c.uart->baud(int(baud));return true;
}
void closeHardware(Context& c) {
    if(c.uart) {
        // Detach under the core lock before freeing its ring or SerialBase
        // owner; no interrupt can retain a pointer to the released context.
        core_util_critical_section_enter();
        c.uart->attach(mbed::Callback<void()>(),mbed::SerialBase::TxIrq);
        c.uart->attach(mbed::Callback<void()>(),mbed::SerialBase::RxIrq);
        c.tx.resetStopped();
        core_util_critical_section_exit();
        c.uart->~UnbufferedSerial();c.uart=nullptr;
    }
    if(c.powered) { gpio_write(&c.power,0);gpio_write(&c.device_wake,0);c.powered=false; }
    c.tx_size=c.tx_pos=0;
}
void updateTransmitComplete(Context& c) {
    Critical guard;
    if(c.hnc_pending_tc && (UART7->ISR&USART_ISR_TC)) {
        c.hnc_tc+=c.hnc_pending_tc;c.hnc_tc_credits+=c.hnc_pending_tc_credits;
        c.hnc_pending_tc=c.hnc_pending_tc_credits=0;
    }
}
void transportReceipt(Context& c) {
    uint32_t v[10];
    { Critical guard;
      updateTransmitComplete(c);
      v[0]=c.hnc_submitted;v[1]=c.hnc_submitted_credits;v[2]=c.hnc_uart;v[3]=c.hnc_uart_credits;
      v[4]=c.hnc_tc;v[5]=c.hnc_tc_credits;v[6]=(c.rx_write-c.rx_read)&(RxSize-1);
      v[7]=c.tx_pos;v[8]=c.tx_size;v[9]=(UART7->ISR&USART_ISR_TC)!=0;
    }
    char line[128];
    snprintf(line,sizeof line,"Owned UART HNC c=%lu/%lu u=%lu/%lu t=%lu/%lu rx=%u tx=%u/%u TC=%u",
        (unsigned long)v[0],(unsigned long)v[1],(unsigned long)v[2],(unsigned long)v[3],
        (unsigned long)v[4],(unsigned long)v[5],unsigned(v[6]),unsigned(v[7]),unsigned(v[8]),unsigned(v[9]));
    diagnostic(line);
}
void stopWithError(const char* message) {
    diagnostic(message);
    transportReceipt(*context);
    RadioStats s=context->host.radioStats();
    radioEnd();
    stopped_stats=s;stopped_stats.controller_ready=stopped_stats.connected=stopped_stats.streaming=false;
    ++stopped_stats.transport_errors;
}
}
Status radioBegin(const char* name,uint32_t mask,const RadioCallbacks& callbacks) {
    return radioBegin(name,mask,callbacks,BleConfig());
}
Status radioBegin(const char* name,uint32_t mask,const RadioCallbacks& callbacks,const BleConfig& ble) {
    if(context) return Status::Busy;
    if(brcm_patchram_format!=1 || brcm_patch_ram_length!=35182 || strncmp(brcm_patch_version,"BCM4343A1_",10))
        return Status::FirmwareMismatch;
    void* storage=allocate(sizeof(Context));
    if(!storage) return Status::OutOfMemory;
    context=new(storage)Context();context->callbacks=callbacks;
    OwnedHostConfig config;config.name=name;config.codecMask=mask;config.callbacks=callbacks;config.ble=ble;
    config.firmware=brcm_patchram_buf;config.firmwareSize=brcm_patch_ram_length;config.baud=GIGA_BREDR_UART_BAUD;
    uint32_t words[]={HAL_GetUIDw0(),HAL_GetUIDw1(),HAL_GetUIDw2()};uint8_t uid[12];
    for(unsigned i=0;i<12;++i) uid[i]=uint8_t(words[i/4]>>(8*(3-i%4)));
    Status status=prototypeBdAddrFromUid(uid,sizeof uid,config.address);
    OwnedHostHooks hooks;hooks.txReady=txReady;hooks.sendH4=sendH4;hooks.setBaud=setBaud;hooks.user=context;
    if(status==Status::Ok) status=context->host.begin(config,hooks);
    if(status!=Status::Ok) { context->~Context();release(context);context=nullptr;return status; }
    stopped_stats={};stopped_ble={};
    gpio_init_out(&context->power,PA_10);gpio_write(&context->power,0);
    gpio_init_out(&context->device_wake,PH_7);gpio_write(&context->device_wake,0);
    gpio_init_in(&context->host_wake,PG_3);context->powered=true;
    delay(20);
    context->uart=new(context->uart_storage)mbed::UnbufferedSerial(PF_7,PA_8,115200);
    context->uart->format(8,mbed::SerialBase::None,1);
    context->uart->set_flow_control(mbed::SerialBase::RTSCTS,PF_8,PF_9);
    context->uart->attach(mbed::callback(uartIrq),mbed::SerialBase::RxIrq);
    gpio_write(&context->power,1);delay(150);
    context->started=context->last_byte=millis();
    diagnostic(callbacks.enable_a2dp?"GIGA owned Classic + BLE host; UART7 initial=115200; RAM-only bonds":"GIGA owned BLE-only host; UART7 initial=115200; RAM-only bonds");
    return Status::Ok;
}
void radioPoll() {
    if(!context) return;
    Context& c=*context;
    updateTransmitComplete(c);
    if(__atomic_load_n(&c.tx_failed,__ATOMIC_ACQUIRE)) { stopWithError("Owned UART write failed");return; }
    if(!c.tx.empty() && uint32_t(millis()-c.tx_started)>3000) { stopWithError("Owned UART TX/CTS timeout");return; }
    // Return retired receive credits before admitting another buffered H4 frame.
    // This service performs no deadlines, security, profiles or deferred events.
    c.host.serviceReceiveCredits(millis());
    if(c.host.radioStats().transport_errors) { transportReceipt(c);radioEnd();return; }
    unsigned rx_budget=2048;
    while(rx_budget-- && c.rx_read!=c.rx_write) {
        __DMB();uint8_t byte=c.rx[c.rx_read];c.rx_read=(c.rx_read+1)&(RxSize-1);c.last_byte=millis();
        if(c.parser.feed(byte)) {
            c.host.receive(c.parser.type(),c.parser.data(),c.parser.size(),c.last_byte);
            // Reassemble/retire each copied fragment and service its credit
            // return before admitting the next complete H4 frame in this burst.
            // The byte budget and receive-window guard remain unchanged.
            c.host.poll(millis());
            if(c.host.radioStats().transport_errors) break;
        }
    }
    if(c.rx_paused && ((c.rx_write-c.rx_read)&(RxSize-1))<RxSize/2) {
        core_util_critical_section_enter();
        c.rx_paused=false;c.uart->attach(mbed::callback(uartIrq),mbed::SerialBase::RxIrq);
        core_util_critical_section_exit();
    }
    if(c.parser.errors() || (c.parser.incomplete() && uint32_t(millis()-c.last_byte)>500)) {
        stopWithError("Owned H4 invalid or incomplete packet; radio stopped");return;
    }
    c.host.poll(millis());
    auto state=c.host.radioStats();
    if(state.transport_errors) { transportReceipt(c);radioEnd();return; }
    if(!state.controller_ready && uint32_t(millis()-c.started)>60000) {
        stopWithError("Owned controller startup timeout");return;
    }
}
void radioEnd() {
    if(!context) return;
    Context* c=context;closeHardware(*c);c->host.end();
    stopped_stats=c->host.radioStats();stopped_ble=c->host.bleStats();
    c->~Context();context=nullptr;release(c);
}
RadioStats radioStats() { return context?context->host.radioStats():stopped_stats; }
BleStats radioBleStats() { return context?context->host.bleStats():stopped_ble; }
Status radioBleNotify(const uint8_t* data,size_t length) { return context?context->host.bleNotify(data,length):Status::Unsupported; }
bool radioBleReadWrite(BleWrite& value) { return context && context->host.bleReadWrite(value); }
Status radioBleDisconnect(uint32_t expected,bool advertise_after) { return context?context->host.bleDisconnect(expected,advertise_after):Status::Unsupported; }
Status radioBleResumeAdvertising() { return context?context->host.bleResumeAdvertising():Status::Unsupported; }
Status radioAvrcpInit(const RadioCallbacks&) { return Status::Unsupported; }
void radioAvrcpPoll() {}
void radioAvrcpDeinit() {}
AvrcpStatus radioAvrcpStatus() { return context?context->host.avrcpStatus():AvrcpStatus(); }
Status radioAvrcpSetVolume(uint8_t volume) { return context?context->host.setVolume(volume):Status::Busy; }
}}
#endif
