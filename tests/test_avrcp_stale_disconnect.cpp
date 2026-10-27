// Synthetic OwnedHost/H4 regression: an unsolicited DisconnectionResponse must
// not retire an OPEN profile before the signaling kernel checks state and ID.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main existing_owned_host_fixture_main
#include "test_owned_host.cpp"
#undef main
namespace {
unsigned staleFailures;
#define EXPECT(x) do{if(!(x)){++staleFailures;printf("FAIL line %u: %s\n",unsigned(__LINE__),#x);}}while(0)
struct Ready:Fixture {
 uint16_t cid=0;uint32_t generation=0;
 Ready(){host.end();config.callbacks.volume=[](uint8_t,void*){};CHECK(host.begin(config,hooks)==Status::Ok);boot();classicConnect();cid=channel(0x17,0x83);generation=host.avrcpStatus().generation;CHECK(generation);output.clear();}
};
void staleResponseCannotInventChannelGeneration(){
 Ready f;f.acl(0x25,1,{7,0x7d,4,0,0x83,0,uint8_t(f.cid),uint8_t(f.cid>>8)});f.settle();
 EXPECT(f.host.avrcpStatus().connected&&f.host.avrcpStatus().generation==f.generation);
 const auto* c=bth_l2cap_sig_find(&f.host.signaling_,f.cid);EXPECT(c&&c->state==BTH_CHANNEL_OPEN);
 EXPECT(f.host.radioStats().transport_errors==0);
}
void matchingResponseStillCompletesRealDisconnect(){
 Ready f;CHECK(f.host.disconnectChannel(f.cid));f.settle();uint8_t identifier=0;
 for(const auto& p:f.output)if(p.cid==1)for(size_t at=0;at+4<=p.bytes.size();){const size_t n=4+bth_read_le16(p.bytes.data()+at+2);CHECK(n<=p.bytes.size()-at);if(p.bytes[at]==6&&n==8&&bth_read_le16(p.bytes.data()+at+4)==0x83&&bth_read_le16(p.bytes.data()+at+6)==f.cid)identifier=p.bytes[at+1];at+=n;}
 CHECK(identifier);f.acl(0x25,1,{7,identifier,4,0,0x83,0,uint8_t(f.cid),uint8_t(f.cid>>8)});f.settle();
 EXPECT(!f.host.avrcpStatus().connected&&!f.host.avrcpStatus().generation);
 EXPECT(!bth_l2cap_sig_find(&f.host.signaling_,f.cid));EXPECT(f.host.radioStats().transport_errors==0);
}
}
int main(int argc,char** argv){if(argc==2){if(!strcmp(argv[1],"stale"))staleResponseCannotInventChannelGeneration();else if(!strcmp(argv[1],"valid"))matchingResponseStillCompletesRealDisconnect();else return 2;}else{staleResponseCannotInventChannelGeneration();matchingResponseStillCompletesRealDisconnect();}return staleFailures?1:0;}
