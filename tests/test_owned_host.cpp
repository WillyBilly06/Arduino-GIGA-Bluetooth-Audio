#include "../src/owned/OwnedHost.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
using namespace giga_bredr;using namespace giga_bredr::detail;
using Bytes=std::vector<uint8_t>;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);abort();}}while(0)
struct Fixture {
 // Default tests own an actor; adapter tests bind to its actual Context.host.
 OwnedHost owned_host;OwnedHost& host;OwnedHostConfig config;OwnedHostHooks hooks;uint32_t now=10;bool ready=true,fail_send=false,fail_baud=false,fail_decoder=false;
 struct Pdu{uint16_t handle,cid;Bytes bytes;};std::vector<Pdu> output;
 unsigned baud_changes=0,configured=0,media=0,stops=0;bool playing=false;Bytes last_media;std::vector<Bytes> tx;std::vector<std::string> messages;
 const uint8_t hcd[8]={0x4c,0xfc,1,0xaa,0x4e,0xfc,1,0xbb};
 Fixture(bool ble=true,OwnedHost* external=nullptr):host(external?*external:owned_host){config.name="Owned GIGA";config.codecMask=AllCodecs;config.ble.enabled=ble;config.firmware=hcd;config.firmwareSize=sizeof hcd;
  config.address[0]=0x42;config.address[1]=0x12;config.address[5]=1;config.callbacks.user=this;
  config.callbacks.configure=[](const CodecConfig&,void* p){auto& f=*static_cast<Fixture*>(p);++f.configured;return f.fail_decoder?Status::OutOfMemory:Status::Ok;};
  config.callbacks.media=[](const uint8_t* p,size_t n,void* u){auto& f=*static_cast<Fixture*>(u);++f.media;f.last_media.assign(p,p+n);};
  config.callbacks.stream=[](bool p,void* u){auto& f=*static_cast<Fixture*>(u);f.playing=p;if(!p)++f.stops;};
  config.callbacks.diagnostic=[](const char* p,void* u){static_cast<Fixture*>(u)->messages.emplace_back(p);};
  hooks.user=this;hooks.txReady=[](void* p){return static_cast<Fixture*>(p)->ready;};
  hooks.sendH4=[](const uint8_t* p,size_t n,void* u){auto& f=*static_cast<Fixture*>(u);if(f.fail_send)return false;f.tx.emplace_back(p,p+n);return true;};
  hooks.setBaud=[](uint32_t baud,void* u){auto& f=*static_cast<Fixture*>(u);CHECK(baud==2000000);++f.baud_changes;return !f.fail_baud;};
  CHECK(host.begin(config,hooks)==Status::Ok);
 }
 void poll(unsigned add=1){now+=add;host.poll(now);}
 void event(uint8_t code,const Bytes& p){Bytes e={code,uint8_t(p.size())};e.insert(e.end(),p.begin(),p.end());host.receive(4,e.data(),e.size(),now);}
 void complete(uint16_t opcode,Bytes data={0},uint8_t grants=1){Bytes p={grants,uint8_t(opcode),uint8_t(opcode>>8)};p.insert(p.end(),data.begin(),data.end());event(14,p);}
 void status(uint16_t opcode,uint8_t result=0){event(15,{result,1,uint8_t(opcode),uint8_t(opcode>>8)});}
 static uint16_t op(const Bytes& p){CHECK(p.size()>=4&&p[0]==1);return bth_read_le16(p.data()+1);}
 Bytes pop(){CHECK(!tx.empty());auto p=tx.front();tx.erase(tx.begin());return p;}
 Bytes response(uint16_t opcode){
  switch(opcode){
  case 0x1001:return {0,7,0x53,0,7,15,0,9,0x22};
  case 0x1003:return {0,0xbf,0xfe,0xcf,0xfe,0xdb,0xff,0x7b,0x87};
  case 0x1009:return {0,config.address[5],config.address[4],config.address[3],config.address[2],config.address[1],config.address[0]};
  case 0x1005:return {0,0xfd,3,0,8,0,0,0};
  case 0x2002:return {0,0xfb,0,4};
  case 0x2003:return {0,1,0,0,0,0,0,0,0};
  default:return {0};
  }
 }
 void boot(){unsigned count=0;while(!host.radioStats().controller_ready&&count++<1400){poll();if(!tx.empty()){Bytes packet=pop();uint16_t opcode=op(packet);complete(opcode,response(opcode));}if(count==1400)CHECK(false);}
  CHECK(host.radioStats().controller_ready&&host.radioStats().bredr_supported&&host.radioStats().manufacturer==15&&baud_changes==1);
  CHECK(!memcmp(host.radioStats().address,config.address,6));settle();
 }
 void acknowledge(const Bytes& packet){
  if(packet[0]==2){CHECK(packet.size()>=9);output.push_back({uint16_t(bth_read_le16(packet.data()+1)&0xfff),bth_read_le16(packet.data()+7),Bytes(packet.begin()+9,packet.end())});uint16_t handle=bth_read_le16(packet.data()+1)&0xfff;event(0x13,{1,uint8_t(handle),uint8_t(handle>>8),1,0});return;}
  uint16_t opcode=op(packet);if(opcode==0x0c35)return;
  if(opcode==0x0409||opcode==0x0411||opcode==0x0413||opcode==0x0406){status(opcode);return;}
  if(opcode==0x1408){complete(opcode,{0,0x25,0,16});return;}
  if(opcode==0x040b||opcode==0x040c||opcode==0x040d||opcode==0x040e||opcode==0x042b||opcode==0x042c||opcode==0x042d||opcode==0x0434){complete(opcode,{0,1,2,3,4,5,6});return;}
  complete(opcode,response(opcode));
 }
 void settle(){for(unsigned i=0;i<35;++i){poll();while(!tx.empty())acknowledge(pop());}}
 void classicConnect(){event(4,{1,2,3,4,5,6,0,0,0,1});settle();event(3,{0,0x25,0,1,2,3,4,5,6,1,0});settle();CHECK(host.radioStats().connected);
  event(0x17,{1,2,3,4,5,6});settle();event(0x31,{1,2,3,4,5,6});settle();event(0x33,{1,2,3,4,5,6,1,0,0,0});settle();
  Bytes key={1,2,3,4,5,6};key.insert(key.end(),16,0xaa);key.push_back(4);event(0x18,key);event(6,{0,0x25,0});settle();event(8,{0,0x25,0,1});settle();
 }
 void leConnect(){event(0x3e,{1,0,0x26,0,1,0,6,5,4,3,2,1,24,0,0,0,0xf4,1,0});settle();CHECK(host.bleStats().connected);}
 uint16_t channel(uint16_t psm,uint16_t remote){
  output.clear();acl(0x25,1,{2,7,4,0,uint8_t(psm),uint8_t(psm>>8),uint8_t(remote),uint8_t(remote>>8)});settle();
  CHECK(!output.empty());uint16_t local=0;uint8_t id=0;for(const auto& p:output)if(p.cid==1)for(size_t pos=0;pos+4<=p.bytes.size();pos+=size_t(bth_read_le16(p.bytes.data()+pos+2))+4){if(p.bytes[pos]==3){CHECK(bth_read_le16(p.bytes.data()+pos+8)==0);local=bth_read_le16(p.bytes.data()+pos+4);}if(p.bytes[pos]==4)id=p.bytes[pos+1];}
  CHECK(local>=0x40&&id);acl(0x25,1,{5,id,6,0,uint8_t(local),uint8_t(local>>8),0,0,0,0});settle();
  acl(0x25,1,{4,8,8,0,uint8_t(local),uint8_t(local>>8),0,0,1,2,0xa0,2});settle();return local;
 }
 void rawAcl(uint16_t handle,uint8_t pb,const Bytes& payload){Bytes p={uint8_t(handle),uint8_t((handle>>8)|(pb<<4)),uint8_t(payload.size()),uint8_t(payload.size()>>8)};p.insert(p.end(),payload.begin(),payload.end());host.receive(2,p.data(),p.size(),now);}
 void acl(uint16_t handle,uint16_t cid,const Bytes& payload,uint8_t pb=2){Bytes p={uint8_t(handle),uint8_t((handle>>8)|(pb<<4)),uint8_t(payload.size()+4),uint8_t((payload.size()+4)>>8),uint8_t(payload.size()),uint8_t(payload.size()>>8),uint8_t(cid),uint8_t(cid>>8)};p.insert(p.end(),payload.begin(),payload.end());host.receive(2,p.data(),p.size(),now);}
};
static void lifecycle_and_boot(){Fixture f;CHECK(!f.host.radioStats().controller_ready);f.ready=false;for(unsigned i=0;i<10;++i)f.poll();CHECK(f.tx.empty());f.ready=true;
 f.poll();CHECK(Fixture::op(f.pop())==0x0c03);f.complete(0x1001);f.poll();CHECK(f.tx.empty());f.complete(0x0c03,{0},0);f.poll();CHECK(f.tx.empty());f.complete(0,{0});f.poll();CHECK(Fixture::op(f.pop())==0xfc2e);f.complete(0xfc2e);
 f.poll();CHECK(Fixture::op(f.pop())==0xfc4c);f.complete(0xfc4c);f.poll();CHECK(Fixture::op(f.pop())==0xfc4e);f.complete(0xfc4e);f.poll();f.poll(999);CHECK(f.tx.empty());f.poll(1);CHECK(Fixture::op(f.pop())==0x0c03);f.complete(0x0c03);f.boot();
 CHECK(sizeof(OwnedHost)<40000);printf("OwnedHost bytes=%zu, slots=%u, window=%u\n",sizeof(OwnedHost),OwnedHost::RxSlots,OwnedHost::RxWindow);
 f.host.end();CHECK(!f.host.radioStats().controller_ready&&!f.host.bleStats().connected);CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.host.end();
}
static void failures(){Fixture timeout;timeout.poll();timeout.poll(3001);CHECK(timeout.host.radioStats().transport_errors==1&&!timeout.host.radioStats().controller_ready);
 Fixture rejected;rejected.poll();auto p=rejected.pop();rejected.complete(Fixture::op(p),{0x0c});CHECK(rejected.host.radioStats().transport_errors==1);
 Fixture bad;bad.poll();bad.complete(0x0c03);bad.poll();bad.complete(0xfc2e);bad.poll();bad.complete(0xfc4c);bad.poll();bad.complete(0xfc4e);bad.poll();bad.poll(1001);bad.complete(0x0c03);bad.poll();bad.complete(0x1001,{0,7,0,0,7,0,0,0,0});CHECK(bad.host.radioStats().transport_errors==1);
}
static void dual_mode(){Fixture f;f.boot();CHECK(f.host.bleStats().advertising);f.classicConnect();f.leConnect();CHECK(f.host.radioStats().connected&&f.host.bleStats().connected);
 f.acl(0x26,4,{0x12,9,0,1,2,3});f.settle();BleWrite write;CHECK(f.host.bleReadWrite(write)&&write.length==3&&write.data[2]==3);CHECK(!f.host.bleReadWrite(write));
 f.acl(0x26,4,{0x12,12,0,1,0});f.settle();CHECK(f.host.bleStats().notify_enabled);uint8_t value[]={7,8};CHECK(f.host.bleNotify(value,2)==Status::Ok);f.settle();CHECK(f.host.bleStats().notifications_queued==1);
 f.event(5,{0,0x26,0,0x13});f.settle();CHECK(!f.host.bleStats().connected&&f.host.radioStats().connected);f.host.end();
}
static void classic_profiles(){Fixture f;f.boot();f.classicConnect();auto sdp=f.channel(1,0x80);f.output.clear();f.acl(0x25,sdp,{2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,4,0});f.settle();CHECK(!f.output.empty()&&f.output.back().bytes[0]==3);
 auto signal=f.channel(0x19,0x81);f.output.clear();f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();CHECK(f.configured==1&&!f.output.empty()&&f.output.back().bytes==Bytes({0x12,3}));
 f.acl(0x25,signal,{0x20,6,4});f.settle();auto media=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing&&f.host.radioStats().streaming&&f.configured==2);
 Bytes packet={0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa};f.acl(0x25,media,packet);f.settle();CHECK(f.media==1&&f.last_media==packet&&f.host.radioStats().received_packets==1);
 f.acl(0x25,signal,{0x40,9,4});f.settle();CHECK(!f.playing);f.fail_decoder=true;f.output.clear();f.acl(0x25,signal,{0x50,7,4});f.settle();CHECK(!f.host.radioStats().streaming&&f.output.back().bytes[0]==0x53);
 f.fail_decoder=false;f.acl(0x25,signal,{0x60,7,4});f.settle();CHECK(f.playing);f.event(5,{0,0x25,0,0x13});f.settle();CHECK(!f.playing&&!f.host.radioStats().connected);
}
static void retained_fragment_and_generation(){Fixture f;f.boot();f.leConnect();
 f.acl(0x26,4,{0x12,9,0,0xa1});f.settle();CHECK(f.host.bleStats().writes==1);
 f.acl(0x26,4,{0x12,9,0,0xb2});f.settle();CHECK(f.host.bleStats().writes==1);
 // B owns an independent ATT copy. An invalid pipelined request C cannot
 // overwrite it, even when C arrives fragmented and its physical slots retire.
 f.rawAcl(0x26,2,{6,0,4,0,0x12});f.rawAcl(0x26,1,{9,0,0xc3,0xd4,0xe5});f.settle();CHECK(f.host.bleStats().writes==1);
 BleWrite w;CHECK(f.host.bleReadWrite(w)&&w.length==1&&w.data[0]==0xa1);f.settle();CHECK(f.host.bleReadWrite(w)&&w.length==1&&w.data[0]==0xb2);
 f.settle();CHECK(!f.host.bleReadWrite(w));
 f.rawAcl(0x26,2,{6,0,4,0,0x12});f.rawAcl(0x26,1,{9,0,0xc3,0xd4,0xe5});f.settle();CHECK(f.host.bleReadWrite(w)&&w.length==3&&w.data[0]==0xc3&&w.data[2]==0xe5);CHECK(f.host.radioStats().transport_errors==0);
 f.acl(0x26,4,{0x12,9,0,0xf6});f.settle();f.acl(0x26,4,{0x12,9,0,0x77});f.settle();uint32_t old=f.host.bleStats().generation;
 f.event(5,{0,0x26,0,0x13});f.settle();CHECK(!f.host.bleReadWrite(w));f.leConnect();CHECK(f.host.bleStats().generation!=old);
 f.settle();CHECK(!f.host.bleReadWrite(w));f.acl(0x26,4,{0x12,9,0,0x88});f.settle();CHECK(f.host.bleReadWrite(w)&&w.data[0]==0x88&&w.generation!=old);
}
static void failure_boundaries(){
 Fixture window;window.boot();window.leConnect();window.acl(0x26,4,{0x0a,3,0});window.settle();window.ready=false;
 for(unsigned i=0;i<OwnedHost::RxWindow;++i)window.acl(0x26,4,{0x0a,3,0});
 CHECK(window.host.radioStats().transport_errors==0);window.acl(0x26,4,{0x0a,3,0});CHECK(window.host.radioStats().transport_errors==1&&!window.host.radioStats().controller_ready);
 bool header=false,link=false,hnc=false;for(const auto& line:window.messages){CHECK(line.size()<128);if(line.find("Owned RXWIN handle=0026 pb=2 bc=0 n=7 out=4 raw=0 used=4 busy=4 budget=4")!=std::string::npos)header=true;if(line.find("Owned RXWIN link=1 active=1")!=std::string::npos&&line.find("out=4 pending=0")!=std::string::npos)link=true;if(line.find("Owned HNC commands=1 packets=1")!=std::string::npos&&line.find("h0=0026 n0=1")!=std::string::npos)hnc=true;}CHECK(header&&link&&hnc);
 Fixture credits;credits.boot();credits.leConnect();credits.event(0x13,{1,0x26,0,1,0});CHECK(credits.host.radioStats().transport_errors==1); // cannot grant unsubmitted packets
 Fixture pending;pending.boot();pending.event(4,{1,2,3,4,5,6,0,0,0,1});pending.settle();pending.poll(15001);CHECK(pending.host.radioStats().transport_errors==1);
 Fixture sent;sent.boot();sent.event(4,{1,2,3,4,5,6,0,0,0,1});sent.poll();CHECK(!sent.tx.empty());sent.poll(5001);CHECK(sent.host.radioStats().transport_errors==1);
 Fixture tx;tx.fail_send=true;tx.poll();CHECK(tx.host.radioStats().transport_errors==1);tx.host.end();
 Fixture disabled(false);disabled.boot();CHECK(!disabled.host.bleStats().enabled&&!disabled.host.bleStats().advertising);
}
static void outgoing_avrcp(){Fixture f;f.host.end();f.config.callbacks.volume=[](uint8_t,void*){};CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();f.channel(0x19,0x80);f.output.clear();f.poll(600);f.settle();bool attempted=false;for(const auto& p:f.output)if(p.cid==1&&p.bytes.size()>=8&&p.bytes[0]==2&&bth_read_le16(p.bytes.data()+4)==0x17)attempted=true;CHECK(attempted);}
static void le_startup_rejection_diagnostic(){
 Fixture f;unsigned loops=0;while(!f.host.radioStats().controller_ready&&loops++<1400){f.poll();if(!f.tx.empty()){auto p=f.pop();f.complete(Fixture::op(p),f.response(Fixture::op(p)));}}CHECK(f.host.radioStats().controller_ready);f.poll();CHECK(!f.tx.empty());CHECK(Fixture::op(f.pop())==0x2006);f.complete(0x2006,{0x0c});f.poll();CHECK(!f.host.radioStats().controller_ready&&f.host.radioStats().transport_errors==1);bool identified=false;for(const auto& m:f.messages)if(m.find("op=2006")!=std::string::npos&&m.find("status=0c")!=std::string::npos)identified=true;CHECK(identified);
 Fixture stale;loops=0;while(!stale.host.radioStats().controller_ready&&loops++<1400){stale.poll();if(!stale.tx.empty()){auto p=stale.pop();stale.complete(Fixture::op(p),stale.response(Fixture::op(p)));}}stale.poll();CHECK(Fixture::op(stale.pop())==0x2006);stale.complete(0x0c52);stale.poll();CHECK(stale.host.radioStats().controller_ready&&stale.host.radioStats().transport_errors==0);while(!stale.tx.empty())stale.acknowledge(stale.pop());stale.complete(0x2006);stale.settle();CHECK(stale.host.bleStats().advertising);
}
int main(){lifecycle_and_boot();failures();dual_mode();classic_profiles();retained_fragment_and_generation();failure_boundaries();outgoing_avrcp();le_startup_rejection_diagnostic();printf("Owned actor %u checks passed\n",checks);return 0;}
