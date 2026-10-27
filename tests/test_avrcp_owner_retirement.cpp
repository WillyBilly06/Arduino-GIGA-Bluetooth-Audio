// Real OwnedHost H4/L2CAP input, synthetic copied transport only.
// Breaks caught: a compound CLOSE+CONNECT can accept a reused remote CID while
// old key SDUs remain queued, or while their accepted H4 copy still owns TX.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main existing_owned_host_fixture_main
#include "test_owned_host.cpp"
#undef main
namespace {
unsigned ownerRetirementFailures;
#define EXPECT(x) do{if(!(x)){++ownerRetirementFailures;printf("FAIL line %u: %s\n",unsigned(__LINE__),#x);}}while(0)
bool keysQueued(const Fixture& f){for(const auto& s:f.host.control_.slots)if(s.state==2&&s.peer==0&&s.cid==0x83&&s.length==8&&s.data[5]==0x7c)return true;return false;}
bool acceptance(const uint8_t* p,size_t n){for(size_t at=0;at+4<=n;){size_t bytes=4+bth_read_le16(p+at+2);if(bytes>n-at)return false;if(p[at]==3&&p[at+1]==0x32&&bytes==12&&bth_read_le16(p+at+8)==0)return true;at+=bytes;}return false;}
bool acceptanceQueued(const Fixture& f){for(const auto& s:f.host.control_.slots)if(s.state==2&&s.peer==0&&s.cid==1&&acceptance(s.data,s.length))return true;return false;}
void compoundCloseConnectWaitsForOldTransport(bool mailboxOnly,bool mediaCase=false,bool stallCase=false){
 Fixture f;f.host.end();f.config.callbacks.volume=[](uint8_t,void*){};CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();f.leConnect();
 uint16_t mediaCid=0;
 if(mediaCase){const auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();mediaCid=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing);}
 const uint16_t cid=f.channel(0x17,0x83);const uint32_t oldGeneration=f.host.avrcpStatus().generation;CHECK(oldGeneration);f.output.clear();
 f.ready=false;uint32_t id=0;CHECK(f.host.avrcpCommand(AvrcpCommand::Play,oldGeneration,id)==Status::Ok);f.poll();CHECK(keysQueued(f));
 bth_control_reservation_t le;CHECK(bth_control_tx_reserve(&f.host.control_,1,&le)==BTH_OK);le.data[0]=0x13;CHECK(bth_control_tx_commit(&f.host.control_,le.token,4,1,672)==BTH_OK);
 if(mailboxOnly){for(unsigned i=0;i<16&&keysQueued(f);++i){f.ready=true;f.poll();f.ready=false;}CHECK(!keysQueued(f)&&!f.tx.empty());}
 // Both commands are in ONE L2CAP signaling SDU. No wire/configuration state
 // is injected; the production signaling parser handles both transitions.
 const Bytes compound={6,0x31,4,0,uint8_t(cid),uint8_t(cid>>8),0x83,0,
                       2,0x32,4,0,0x17,0,0x83,0};
 f.acl(0x25,1,compound);f.poll();
 EXPECT(!acceptanceQueued(f)); // No successful reused-CID admission before fence.
 EXPECT(f.host.radioStats().transport_errors==0);
 if(mediaCase){const Bytes media={0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa};f.acl(0x25,mediaCid,media);f.poll();EXPECT(f.media==1&&f.last_media==media&&f.playing);bool completed=false;for(const auto& link:f.host.rx_.links)if(link.active&&link.handle==0x25&&link.pending>=2)completed=true;EXPECT(completed);}
 if(stallCase){const auto credits=f.host.flow_.classic.available;f.poll(5001);EXPECT(f.host.links_.classic.disconnecting);EXPECT(keysQueued(f));EXPECT(f.host.flow_.classic.available==credits);f.event(5,{0,0x25,0,0x13});f.ready=true;f.settle();EXPECT(!f.host.radioStats().connected&&f.host.bleStats().connected);bool kept=false;for(const auto& p:f.output)if(p.handle==0x26&&p.cid==4&&p.bytes==Bytes({0x13}))kept=true;EXPECT(kept);return;}
 f.ready=true;f.settle();
 bool accepted=false;uint16_t local=0;uint8_t configId=0;unsigned leCount=0;
 for(const auto& p:f.output){
  if(p.handle==0x26&&p.cid==4&&p.bytes==Bytes({0x13}))++leCount;
  if(p.cid!=1)continue;
  for(size_t at=0;at+4<=p.bytes.size();){size_t bytes=4+bth_read_le16(p.bytes.data()+at+2);CHECK(bytes<=p.bytes.size()-at);
   if(p.bytes[at]==3&&p.bytes[at+1]==0x32&&bytes==12&&bth_read_le16(p.bytes.data()+at+8)==0){accepted=true;local=bth_read_le16(p.bytes.data()+at+4);}
   if(p.bytes[at]==4&&bytes>=8&&bth_read_le16(p.bytes.data()+at+4)==0x83)configId=p.bytes[at+1];at+=bytes;
  }
 }
 EXPECT(accepted&&local==cid&&configId);EXPECT(leCount==1);
 if(accepted&&configId){
  f.acl(0x25,1,{5,configId,6,0,uint8_t(local),uint8_t(local>>8),0,0,0,0});f.settle();
  f.acl(0x25,1,{4,0x33,8,0,uint8_t(local),uint8_t(local>>8),0,0,1,2,0xa0,2});f.settle();
  EXPECT(f.host.avrcpStatus().connected&&f.host.avrcpStatus().generation!=oldGeneration);
 }
 EXPECT(f.host.radioStats().transport_errors==0);
}
}
int main(int argc,char** argv){if(argc==2){if(!strcmp(argv[1],"compound"))compoundCloseConnectWaitsForOldTransport(false);else if(!strcmp(argv[1],"mailbox"))compoundCloseConnectWaitsForOldTransport(true);else if(!strcmp(argv[1],"media"))compoundCloseConnectWaitsForOldTransport(false,true);else if(!strcmp(argv[1],"stall"))compoundCloseConnectWaitsForOldTransport(false,false,true);else return 2;}else{compoundCloseConnectWaitsForOldTransport(false);compoundCloseConnectWaitsForOldTransport(true);compoundCloseConnectWaitsForOldTransport(false,true);compoundCloseConnectWaitsForOldTransport(false,false,true);}return ownerRetirementFailures?1:0;}
