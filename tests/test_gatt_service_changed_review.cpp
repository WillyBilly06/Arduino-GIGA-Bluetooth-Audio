#include "owned/LeProfiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace giga_bredr; using namespace giga_bredr::detail;
struct Fixture {
 bth_cmdq_t cmd{}; bth_control_tx_t tx{}; bth_hci_flow_t flow{}; LeProfiles p; uint32_t gen=7;
 Fixture() { bth_cmdq_init(&cmd); bth_control_tx_init(&tx); bth_hci_flow_init(&flow);
  assert(bth_hci_flow_set_buffers(&flow,5,50,5,50)); assert(bth_hci_flow_attach(&flow,0x42,BTH_LINK_LE));
  BleConfig c;c.maximum_mtu=517;assert(p.begin(c,"GIGA owned BLE",cmd,tx)==Status::Ok); connect(); }
 void connect(){assert(bth_control_tx_attach(&tx,1,0x42,gen));assert(p.attach(gen,0x42));}
 void clear(){assert(bth_control_tx_detach(&tx,1,gen));assert(bth_control_tx_attach(&tx,1,0x42,gen));}
 void req(std::initializer_list<uint8_t> in,std::initializer_list<uint8_t> out) {
  assert(p.receive(gen,4,in.begin(),in.size(),0)); auto& peer=tx.peers[1];assert(peer.count==1);
  auto& s=tx.slots[peer.order[peer.read]];assert(s.cid==4&&s.length==out.size()&&!memcmp(s.data,out.begin(),out.size()));clear();
 }
 bth_control_tx_info_t fragment(uint32_t now) {
  uint8_t data[100];size_t n=0;uint32_t owners[]={0,gen};bth_control_tx_info_t info={};
  assert(bth_control_tx_next_policy(&tx,owners,&flow,nullptr,&info,data,sizeof data,&n)==BTH_OK);
  assert(info.owner==gen&&info.peer==1&&n>5);p.controlSubmitted(info,now);return info;
 }
 void confirm(uint32_t g,uint32_t now){const uint8_t c[]={0x1e};assert(p.receive(g,4,c,1,now));}
};
int main(){
 // Wrong owner, slot, or serial submission callbacks cannot start or complete an indication.
 {Fixture f;f.req({0x12,17,0,2,0},{0x13});assert(f.p.setVendorDescription(7,false));f.p.poll(1,false);
  assert(f.tx.peers[1].count==1);auto &slot=f.tx.slots[f.tx.peers[1].order[f.tx.peers[1].read]];
  bth_control_tx_info_t x={};x.peer=1;x.owner=8;x.token={slot.serial,f.tx.peers[1].order[f.tx.peers[1].read]};x.first_fragment=x.final_fragment=true;
  f.p.controlSubmitted(x,2);x.owner=7;++x.token.serial;f.p.controlSubmitted(x,2);--x.token.serial;x.token.slot^=1;f.p.controlSubmitted(x,2);
  f.confirm(7,2);assert(f.p.setVendorDescription(7,true));f.p.poll(2,false);assert(f.tx.peers[1].count==1);
  auto a=f.fragment(3);assert(a.first_fragment&&!a.final_fragment);f.confirm(7,4);
  auto b=f.fragment(4);assert(!b.first_fragment&&!b.final_fragment);auto c=f.fragment(5);assert(c.final_fragment);f.confirm(7,6);f.p.poll(6,false);assert(f.tx.peers[1].count==1);
 }
 // Real unrelated notification fragments must not start the queued indication clock.
 {Fixture f;f.req({0x12,12,0,1,0},{0x13});f.req({0x12,17,0,2,0},{0x13});const uint8_t value[]={1,2,3};assert(f.p.notify(value,3)==Status::Ok);f.p.poll(1,false);assert(f.tx.peers[1].count==1);
  assert(f.p.setVendorDescription(7,false));f.p.poll(2,false);assert(f.tx.peers[1].count==2);
  auto n1=f.fragment(3);auto n2=f.fragment(4);assert(n1.first_fragment&&!n1.final_fragment&&!n2.first_fragment&&n2.final_fragment);f.p.poll(30004,false);assert(!f.p.closeRequested());
  f.confirm(7,30005);assert(f.p.setVendorDescription(7,true));f.p.poll(30005,false);assert(f.tx.peers[1].count==1);
  auto a=f.fragment(30006);auto b=f.fragment(30007);auto c=f.fragment(30008);assert(a.first_fragment&&!a.final_fragment&&!b.final_fragment&&c.final_fragment);
  f.confirm(7,30009);f.p.poll(30009,false);assert(f.tx.peers[1].count==1);
 }
 // A prepared write owns copied bytes, not an attribute pointer that shifts during mutation.
 {Fixture f;f.req({0x16,9,0,0,0,0x41,0x42},{0x17,9,0,0,0,0x41,0x42});
  assert(f.p.setVendorDescription(7,false));f.req({0x18,1},{0x19});BleWrite result;assert(f.p.readWrite(result));assert(result.generation==7&&result.length==2&&result.data[0]==0x41&&result.data[1]==0x42);
  f.req({0x0a,13,0},{1,0x0a,13,0,1});assert(f.p.setVendorDescription(7,true));f.req({0x0a,13,0},{0x0b,'G','I','G','A',' ','d','a','t','a'});
 }
 // Notification value lives in handle 11 even after descriptor array shifts; CCCDs remain independent.
 {Fixture f;f.req({0x12,12,0,1,0},{0x13});f.req({0x12,17,0,2,0},{0x13});
  assert(f.p.setVendorDescription(7,false));const uint8_t value[]={0xaa,0xbb,0xcc};assert(f.p.notify(value,sizeof value)==Status::Ok);
  f.p.poll(1,false);assert(f.tx.peers[1].count==2);auto &peer=f.tx.peers[1];auto &first=f.tx.slots[peer.order[peer.read]];auto &second=f.tx.slots[peer.order[(peer.read+1)&3]];
  const uint8_t indication[]={0x1d,16,0,7,0,13,0};const uint8_t notification[]={0x1b,11,0,0xaa,0xbb,0xcc};assert(first.length==7&&!memcmp(first.data,indication,7));assert(second.length==6&&!memcmp(second.data,notification,6));
  f.clear();f.req({0x0a,11,0},{0x0b,0xaa,0xbb,0xcc});f.req({0x0a,12,0},{0x0b,1,0});f.req({0x0a,17,0},{0x0b,2,0});
 }
 puts("PASS independent identity, prepared-write mutation and shared notification cases synthetic_only=1");return 0;
}
