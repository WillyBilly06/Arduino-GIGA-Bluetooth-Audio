#include "../src/owned/LeProfiles.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
using namespace giga_bredr;using namespace giga_bredr::detail;
struct F {LeProfiles le;bth_cmdq_t q;bth_control_tx_t control;bth_hci_flow_t flow;
 F(){bth_cmdq_init(&q);bth_control_tx_init(&control);bth_hci_flow_init(&flow);assert(le.begin(BleConfig{},"Reset BLE",q,control)==Status::Ok);}
 bool reset(uint16_t op=0x0c03,uint8_t status=0,size_t n=4){uint8_t p[]={1,uint8_t(op),uint8_t(op>>8),status};bth_h4_frame_t e={};e.type=4;e.event={14,{p,n}};return le.startAfterReset(e);}
 uint16_t command(uint8_t* param=nullptr){uint8_t bytes[260];size_t n=0;assert(bth_cmdq_next(&q,&flow,bytes,sizeof bytes,&n)==BTH_OK);if(param)*param=bytes[4];return bth_read_le16(bytes+1);}
 void done(uint16_t op,uint8_t status=0){uint8_t p[]={1,uint8_t(op),uint8_t(op>>8),status};bth_h4_frame_t e={};e.type=4;e.event={14,{p,4}};assert(bth_hci_flow_on_event(&flow,&e)==BTH_OK);uint32_t owner=0;bth_cmdq_complete(&q,&e,&owner);le.event(e);}
 void setup(){assert(reset());for(uint16_t op:{0x2006,0x2008,0x2009}){le.poll(20000,false);assert(command()==op);done(op);}assert(!le.stats().advertising);}
};
int main(){
 F bad;assert(!bad.reset(0x1001)&&!bad.reset(0x0c03,1)&&!bad.reset(0x0c03,0,3));bad.le.poll(20000,false);assert(!bad.reset());assert(bad.command()==0x200a);bad.done(0x200a,0x0c);assert(bad.le.stats().status==Status::HardwareError);
 F verified;verified.setup();assert(!verified.reset());verified.le.poll(20001,true);uint8_t enable=0;assert(verified.command(&enable)==0x200a&&enable==1);verified.done(0x200a);assert(verified.le.stats().advertising);assert(!verified.reset());verified.le.poll(20002,false);assert(verified.command(&enable)==0x200a&&enable==0);verified.done(0x200a,0x0c);assert(verified.le.stats().status==Status::HardwareError);
 F parameters;assert(parameters.reset());parameters.le.poll(20000,true);assert(parameters.command()==0x2006);parameters.done(0x2006,0x0c);assert(parameters.le.stats().status==Status::HardwareError);
 puts("Verified-reset-only LE bootstrap, invalid proof, unknown state and runtime-error preservation passed");
}
