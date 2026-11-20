#include <cstddef>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main fixture_main
#include "test_owned_host.cpp"
#undef main
int main(){
 Fixture f;f.boot();f.leConnect();uint32_t generation=f.host.bleStats().generation;
 f.acl(0x26,4,{0x12,17,0,2,0});f.settle();f.output.clear();
 CHECK(f.host.bleSetVendorDescription(generation,false));f.settle();
 unsigned indicated=0;for(const auto& p:f.output)if(p.cid==4&&p.bytes==Bytes({0x1d,16,0,7,0,13,0}))++indicated;
 CHECK(indicated==1);f.output.clear();CHECK(f.host.bleSetVendorDescription(generation,true));f.settle();CHECK(f.output.empty());
 f.acl(0x26,4,{0x1e});f.settle();indicated=0;
 for(const auto& p:f.output)if(p.cid==4&&p.bytes==Bytes({0x1d,16,0,7,0,13,0}))++indicated;
 CHECK(indicated==1);f.poll(30000);
 bool disconnect=false;for(unsigned i=0;i<30;++i){f.poll();while(!f.tx.empty()){auto packet=f.pop();
  if(packet[0]==1 && Fixture::op(packet)==0x0406) disconnect=true;f.acknowledge(packet);}}
 CHECK(disconnect);CHECK(!f.host.radioStats().transport_errors);
 Fixture g;g.boot();g.leConnect();auto owner=g.host.bleStats().generation;
 g.acl(0x26,4,{0x12,17,0,2,0});g.settle();g.ready=false;
 g.host.flow_.le.length=5; // Synthetic small ACL pool forces a multi-fragment SDU.
 CHECK(g.host.bleSetVendorDescription(owner,false));for(unsigned i=0;i<8;++i)g.poll();
 CHECK(g.host.bleStats().connected&&!g.host.radioStats().transport_errors);
 g.ready=true;bool first=false;for(unsigned i=0;i<20&&!first;++i){g.poll();while(!g.tx.empty()){
  auto packet=g.pop();if(packet[0]==2){first=true;CHECK(bth_read_le16(packet.data()+3)==5);}g.acknowledge(packet);}}
 CHECK(first);g.fail_send=true;for(unsigned i=0;i<20&&!g.host.radioStats().transport_errors;++i)g.poll();
 CHECK(g.host.radioStats().transport_errors==1&&!g.host.bleStats().connected);
 printf("PASS actual OwnedHost submission callback, mutation, indication, confirmation and timeout disconnect request\n");
}
