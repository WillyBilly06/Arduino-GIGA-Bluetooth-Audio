// Real Classic profile + control/ACL/HCI kernels; synthetic CPU bytes only.
// Breaks caught: old queued taps becoming visible after a same-CID new session,
// canceling an emitted ACL prefix, or retiring unrelated Classic/LE controls.
#pragma GCC diagnostic push
// The included test main is renamed only to borrow its real fixture. Its
// implicit main return is irrelevant because that renamed function is unused.
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main existing_avrcp_fixture_main
#include "test_avrcp_controls.cpp"
#undef main
#pragma GCC diagnostic pop
namespace {
unsigned retirementFailures;
#define EXPECT(x) do{if(!(x)){++retirementFailures;printf("FAIL line %u: %s\n",unsigned(__LINE__),#x);}}while(0)
struct Wire {
 std::vector<uint8_t> classic,le;unsigned unrelatedClassic=0,unrelatedLe=0,oldKeys=0;
 void accept(const uint8_t* p,size_t n,bool newSession){
  EXPECT(n>=5&&p[0]==2);if(n<5)return;uint16_t handle=bth_read_le16(p+1)&0xfff;uint8_t pb=uint8_t((bth_read_le16(p+1)>>12)&3);
  auto& assembly=handle==0x25?classic:le;
  if(pb!=1){EXPECT(assembly.empty());assembly.clear();}else EXPECT(!assembly.empty());
  assembly.insert(assembly.end(),p+5,p+n);
  if(assembly.size()<4)return;size_t total=4+bth_read_le16(assembly.data());EXPECT(assembly.size()<=total);if(assembly.size()!=total)return;
  uint16_t cid=bth_read_le16(assembly.data()+2);std::vector<uint8_t> payload(assembly.begin()+4,assembly.end());
  if(handle==0x25&&cid==0x83&&payload.size()==8&&payload[5]==0x7c){++oldKeys;EXPECT(!newSession);}
  if(handle==0x25&&cid==0x84){EXPECT(payload==std::vector<uint8_t>({0xaa,0x55}));++unrelatedClassic;}
  if(handle==0x26&&cid==4){EXPECT(payload==std::vector<uint8_t>({0xbb,0x66}));++unrelatedLe;}
  assembly.clear();
 }
};
bool queuedOldKeys(const Fixture& f){for(const auto& s:f.q.slots)if(s.state==2&&s.peer==0&&s.owner==f.owner&&s.cid==0x83&&s.length==8&&s.data[5]==0x7c)return true;return false;}
void queueUnrelated(Fixture& f){
 bth_control_reservation_t r;assert(bth_control_tx_reserve(&f.q,0,&r)==BTH_OK);r.data[0]=0xaa;r.data[1]=0x55;assert(bth_control_tx_commit(&f.q,r.token,0x84,2,672)==BTH_OK);
 assert(bth_control_tx_attach(&f.q,1,0x26,2));assert(bth_hci_flow_attach(&f.flow,0x26,BTH_LINK_LE));assert(bth_control_tx_reserve(&f.q,1,&r)==BTH_OK);r.data[0]=0xbb;r.data[1]=0x66;assert(bth_control_tx_commit(&f.q,r.token,4,2,672)==BTH_OK);
}
void sameCidReopenRetiresOnlyOldChannel(bool partial){
 Fixture f;Wire wire;uint32_t generation=f.p.avrcpStatus().generation,id=0;
 if(partial)f.flow.classic.length=5; // Synthetic reported controller ACL payload bound.
 assert(f.p.avrcpCommand(AvrcpCommand::Play,generation,true,1,id)==Status::Ok);f.p.poll(true,2);queueUnrelated(f);
 uint8_t packet[1030];size_t n=0;uint32_t owners[2]={f.owner,2};
 if(partial){assert(bth_control_tx_next(&f.q,owners,&f.flow,packet,sizeof packet,&n)==BTH_OK);wire.accept(packet,n,false);assert(f.q.peers[0].tx.active&&f.q.peers[0].tx.position>0);}
 const auto credits=f.flow.classic.available;
 f.p.channelClosed(0x43);EXPECT(f.flow.classic.available==credits);EXPECT(!f.p.avrcpStatus().generation);
 bool reopened=f.p.channelOpened(0x17,0x43,0x83,672);
 EXPECT(!reopened||!queuedOldKeys(f));
 unsigned fragments=0;
 for(unsigned i=0;i<64;++i){auto st=bth_control_tx_next(&f.q,owners,&f.flow,packet,sizeof packet,&n);if(st==BTH_MORE)break;assert(st==BTH_OK);++fragments;wire.accept(packet,n,reopened);}
 EXPECT(f.q.used==0&&f.q.peers[0].count==0&&f.q.peers[1].count==0);
 EXPECT(f.flow.classic.available+fragments==credits);EXPECT(wire.classic.empty()&&wire.le.empty());
 EXPECT(wire.unrelatedClassic==1&&wire.unrelatedLe==1);if(partial)EXPECT(wire.oldKeys>=1);
 if(!reopened)reopened=f.p.channelOpened(0x17,0x43,0x83,672);
 EXPECT(reopened&&f.p.avrcpStatus().generation&&f.p.avrcpStatus().generation!=generation);
 uint32_t stale=0;EXPECT(f.p.avrcpCommand(AvrcpCommand::Play,generation,true,3,stale)==Status::InvalidArgument);
}
}
int main(int argc,char** argv){
 if(argc==2){if(!strcmp(argv[1],"queued"))sameCidReopenRetiresOnlyOldChannel(false);else if(!strcmp(argv[1],"partial"))sameCidReopenRetiresOnlyOldChannel(true);else return 2;}
 else {sameCidReopenRetiresOnlyOldChannel(false);sameCidReopenRetiresOnlyOldChannel(true);}
 return retirementFailures?1:0;
}
