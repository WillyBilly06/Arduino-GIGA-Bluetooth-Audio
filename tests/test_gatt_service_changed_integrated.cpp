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
int main(){Fixture f;
 f.req({0x10,7,0,0xff,0xff,0,0x28},{0x11,20,7,0,13,0,0xd8,0x65,0x3e,0xd2,0x0a,0x03,0x9c,0x91,0xaf,0x4a,0x49,0x9c,1,0,0x77,0x9f});
 f.req({0x10,14,0,0xff,0xff,0,0x28},{0x11,6,14,0,17,0,1,0x18});
 f.req({8,14,0,17,0,3,0x28},{9,7,15,0,0x20,16,0,5,0x2a});
 f.req({0x0a,16,0},{1,0x0a,16,0,2});
 f.req({0x0a,17,0},{0x0b,0,0});
 f.req({0x0a,13,0},{0x0b,'G','I','G','A',' ','d','a','t','a'});
 f.req({0x12,17,0,1,0},{1,0x12,17,0,0x13});
 assert(!f.p.setVendorDescription(8,false));assert(f.p.setVendorDescription(7,false));f.p.poll(1,false);assert(!f.tx.peers[1].count);
 f.req({0x0a,13,0},{1,0x0a,13,0,1});assert(f.p.setVendorDescription(7,true));
 f.req({0x12,17,0,2,0},{0x13});
 f.req({0x0a,17,0},{0x0b,2,0});
 bth_control_reservation_t occupied[3]={};for(auto& r:occupied)assert(bth_control_tx_reserve(&f.tx,1,&r)==BTH_OK);
 assert(f.p.setVendorDescription(7,false));f.p.poll(2,false);assert(f.tx.peers[1].count==0);
 for(auto& r:occupied)assert(bth_control_tx_cancel(&f.tx,r.token)==BTH_OK);
 f.p.poll(3,false);assert(f.tx.peers[1].count==1);
 auto& s=f.tx.slots[f.tx.peers[1].order[f.tx.peers[1].read]];
 const uint8_t indication[]={0x1d,16,0,7,0,13,0};assert(s.length==sizeof indication&&!memcmp(s.data,indication,sizeof indication));
 f.confirm(7,4);assert(f.p.setVendorDescription(7,true));f.p.poll(4,false);assert(f.tx.peers[1].count==1);
 auto a=f.fragment(5);assert(a.first_fragment&&!a.final_fragment);f.confirm(7,6);
 auto b=f.fragment(7);assert(!b.first_fragment&&!b.final_fragment);auto c=f.fragment(8);assert(!c.first_fragment&&c.final_fragment);
 f.confirm(8,9);f.p.poll(9,false);assert(!f.tx.peers[1].count);
 f.confirm(7,9);f.p.poll(10,false);assert(f.tx.peers[1].count==1);
 a=f.fragment(0xfffffff0u);assert(a.first_fragment);f.p.poll(0xfffffff0u+29999u,false);assert(!f.p.closeRequested());
 f.p.poll(0xfffffff0u+30000u,false);assert(f.p.closeRequested()&&f.tx.peers[1].att_blocked);
 uint8_t out[100];size_t n=0;uint32_t owners[]={0,7};bth_control_tx_info_t info={};
 assert(bth_control_tx_next_policy(&f.tx,owners,&f.flow,nullptr,&info,out,sizeof out,&n)==BTH_MORE);
 f.p.detach(7);assert(bth_control_tx_detach(&f.tx,1,7));f.gen=8;f.connect();
 f.req({0x0a,17,0},{0x0b,0,0});assert(!f.p.closeRequested());f.confirm(7,0);
 assert(f.p.setVendorDescription(8,false));f.p.poll(0,false);assert(!f.tx.peers[1].count);
 Fixture z;z.req({0x12,17,0,2,0},{0x13});
 for(auto& slot:z.tx.slots)slot.serial=UINT32_MAX;
 assert(z.p.setVendorDescription(7,false));z.p.poll(1,false);
 assert(z.p.closeRequested()&&z.tx.peers[1].att_blocked);
 puts("PASS candidate actual discovery, permissions, CCCD, mutation, capacity, fragmented submission, confirmation, timeout, generation and reconnect");
}
