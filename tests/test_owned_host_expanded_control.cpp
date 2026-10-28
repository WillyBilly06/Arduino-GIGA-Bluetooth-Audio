#define private public
#include "owned/OwnedHost.h"
#undef private
#define main unused_fixture_main
#include "test_owned_host.cpp"
#undef main

static uint16_t audio(Fixture& f) {
 f.boot();f.classicConnect();auto signal=f.channel(0x19,0x81);
 f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();
 f.acl(0x25,signal,{0x20,6,4});f.settle();auto media=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();
 CHECK(f.playing);f.output.clear();return media;
}
static Bytes bundle(unsigned count,unsigned first=1) {
 Bytes out;for(unsigned i=0;i<count;++i){const uint8_t p[]={10,uint8_t(first+i),2,0,3,0};out.insert(out.end(),p,p+6);}return out;
}
static Bytes media(unsigned sequence){return {0x80,0x60,0,uint8_t(sequence),1,2,3,4,5,6,7,8,1,0xaa};}
struct Wire {
 Fixture& f;std::vector<Fixture::Pdu> sent;unsigned returned=0;bool return_controller_credits=true;
 explicit Wire(Fixture& x):f(x){}
 void collect(){while(!f.tx.empty()){
  auto p=f.pop();if(p[0]==2){CHECK(p.size()>=9&&((p[2]>>4)&3)==0);const auto handle=uint16_t(bth_read_le16(p.data()+1)&0xfff),cid=bth_read_le16(p.data()+7);CHECK(p.size()-9==bth_read_le16(p.data()+5));
   sent.push_back({handle,cid,Bytes(p.begin()+9,p.end())});if(return_controller_credits)f.event(0x13,{1,uint8_t(handle),uint8_t(handle>>8),1,0});
  }else {if(Fixture::op(p)==0x0c35)returned+=bth_read_le16(p.data()+7);f.acknowledge(p);}
 }}
 void poll(unsigned ticks=1){for(unsigned n=0;n<ticks;++n){f.poll();collect();}}
 void finish(){for(unsigned n=0;n<2500;++n){poll();if(!f.host.control_.used&&!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding)return;}CHECK(false);}
 unsigned info(unsigned first,unsigned count)const{unsigned found=0;for(const auto& p:sent)if(p.cid==1)for(size_t at=0;at<p.bytes.size();){CHECK(p.bytes.size()<=672&&p.bytes.size()-at>=4);size_t n=4+bth_read_le16(p.bytes.data()+at+2);CHECK(n<=p.bytes.size()-at);CHECK(p.bytes[at]==11&&n==16&&p.bytes[at+1]==first+found);const uint8_t payload[]={3,0,0,0,2,0,0,0,0,0,0,0};CHECK(!memcmp(p.bytes.data()+at+4,payload,sizeof payload));++found;at+=n;}CHECK(found==count);return found;}
 void exhaust(){return_controller_credits=false;for(unsigned i=0;i<8;++i){f.acl(0x25,1,{8,uint8_t(200+i),0,0});poll(10);}CHECK(f.host.flow_.classic.available==0&&sent.size()==8&&!f.host.rx_.outstanding);sent.clear();}
 void restore(){CHECK(!return_controller_credits);f.event(0x13,{1,0x25,0,8,0});return_controller_credits=true;}
};
static void bounded_backpressure_and_media(){
 Fixture f(false);const auto cid=audio(f);Wire w(f);w.exhaust();f.acl(0x25,1,bundle(112));w.poll(10);
 CHECK(f.host.classic_deferred_count_==1&&!f.host.peers_[0].has_pending&&f.host.control_.peers[0].owned==3&&!f.host.rx_.outstanding);
 const auto slot=f.host.classic_deferred_read_;CHECK((f.host.classic_deferred_[slot].signal_cursor&0x7fff)==18);
 for(unsigned n=0;n<64;++n){f.acl(0x25,cid,media(n));w.poll(4);CHECK(f.media==n+1&&!f.host.rx_.outstanding&&f.host.flow_.classic.available==0);}
 CHECK((f.host.classic_deferred_[slot].signal_cursor&0x7fff)==18&&w.sent.empty()&&!f.host.radioStats().transport_errors);
 w.restore();w.finish();w.info(1,112);CHECK(f.media==64&&f.host.flow_.classic.available==8&&f.host.radioStats().streaming);
 puts("BACKPRESSURE_112_EXACT_RESPONSES_64_MEDIA_RX_CREDITS_PASS");
}
static void one_owner_budget(){
 Fixture f(false);audio(f);f.ready=false;f.acl(0x25,1,bundle(112));f.poll();
 CHECK(f.host.control_.peers[0].owned==3&&f.host.classic_deferred_count_==1&&f.host.expanded_control_budget_==0);
 const auto cursor=f.host.classic_deferred_[f.host.classic_deferred_read_].signal_cursor;
 for(unsigned i=0;i<20;++i)f.poll();CHECK(f.host.control_.peers[0].owned==3&&f.host.classic_deferred_[f.host.classic_deferred_read_].signal_cursor==cursor);
 f.ready=true;Wire w(f);w.finish();w.info(1,112);puts("MAX3_SPLIT_REPLIES_PER_OWNER_PASS_NO_REPLAY_PASS");
}
static void benign_capacity_full(){
 Fixture f(false);const auto cid=audio(f);Wire w(f);w.exhaust();
 for(unsigned i=0;i<3;++i){f.acl(0x25,1,bundle(43,1+i*43));w.poll(4);CHECK(f.host.classic_deferred_count_==i+1&&!f.host.peers_[0].has_pending);}
 f.acl(0x25,1,bundle(43,130));w.poll(4);CHECK(f.host.classic_deferred_count_==3&&f.host.peers_[0].has_pending);
 f.acl(0x25,cid,media(90));w.poll(4);CHECK(f.media==1); // Wholly stateless control remains FIFO while independent media progresses.
 w.restore();w.finish();w.info(1,172);CHECK(f.media==1&&f.host.flow_.classic.available==8&&!f.host.radioStats().transport_errors);
 puts("THREE_COPIED_BUNDLES_FOURTH_PENDING_EXACT172_FIFO_RECOVERY_PASS");
}
static void malformed_and_mutating_tail(){
 {Fixture f(false);auto cid=audio(f);Wire w(f);const auto before=f.host.signaling_;auto input=bundle(43);input.insert(input.end(),{10,90,2,0,3});f.acl(0x25,1,input);w.poll(30);
  printf("MALFORMED_OBS sent=%zu equal=%u copied=%u pending=%u dropped=%lu errors=%lu\n",w.sent.size(),unsigned(!memcmp(&before,&f.host.signaling_,sizeof before)),unsigned(f.host.classic_deferred_count_),unsigned(f.host.peers_[0].has_pending),(unsigned long)f.host.radioStats().dropped_packets,(unsigned long)f.host.radioStats().transport_errors);
  // The original atomic parser reaches capacity before it reaches this tail.
  // The candidate must leave that path atomic rather than emit a valid prefix.
  CHECK(w.sent.empty()&&!memcmp(&before,&f.host.signaling_,sizeof before)&&!f.host.classic_deferred_count_&&f.host.peers_[0].has_pending&&f.host.radioStats().dropped_packets==0);
  f.acl(0x25,cid,media(1));w.poll(3);CHECK(!f.media);}
 {Fixture f(false);auto cid=audio(f);Wire w(f);const auto before=f.host.signaling_;auto input=bundle(43);input.insert(input.end(),{6,90,4,0,uint8_t(cid),uint8_t(cid>>8),0x82,0});f.acl(0x25,1,input);w.poll(30);
  CHECK(w.sent.empty()&&!memcmp(&before,&f.host.signaling_,sizeof before)&&!f.host.classic_deferred_count_&&f.host.peers_[0].has_pending&&f.playing);
  f.acl(0x25,cid,media(1));w.poll(3);CHECK(!f.media&&f.playing);}
 puts("MALFORMED_TAIL_NO_PARTIAL_COMMIT_MIXED_MUTATING_BARRIER_UNCHANGED_PASS");
}
static void teardown_cursor(){
 Fixture f(false);audio(f);Wire w(f);w.exhaust();f.acl(0x25,1,bundle(112));w.poll(4);CHECK(f.host.classic_deferred_count_==1);
 auto owner=f.host.peers_[0].owner;f.event(5,{0,0x25,0,0x13});w.poll(3);CHECK(!f.host.control_.used&&!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending&&!f.host.peers_[0].pending_signal_cursor&&!f.host.rx_.outstanding);
 for(const auto& p:f.host.classic_deferred_)CHECK(!p.signal_cursor);f.classicConnect();CHECK(f.host.peers_[0].owner!=owner);w.sent.clear();w.return_controller_credits=true;
 f.acl(0x25,1,{10,99,2,0,3,0});w.finish();w.info(99,1);CHECK(!f.host.radioStats().transport_errors);puts("PARTIAL_CURSOR_DISCONNECT_HANDLE_REUSE_CLEAN_PASS");
}
static void echo_and_information_order(){
 Fixture f(false);audio(f);Wire w(f);Bytes input={8,1,0,2};for(unsigned i=0;i<512;++i)input.push_back(uint8_t(i*13));auto infos=bundle(12,2);input.insert(input.end(),infos.begin(),infos.end());CHECK(input.size()==588);
 f.acl(0x25,1,input);w.finish();CHECK(w.sent.size()==13&&w.sent[0].bytes.size()==516&&w.sent[0].bytes[0]==9&&w.sent[0].bytes[1]==1);
 for(unsigned i=0;i<512;++i)CHECK(w.sent[0].bytes[i+4]==uint8_t(i*13));
 for(unsigned i=1;i<13;++i)CHECK(w.sent[i].bytes.size()==16&&w.sent[i].bytes[0]==11&&w.sent[i].bytes[1]==i+1);
 puts("EXPANDED_ECHO512_AND_INFO_EXACT_PAYLOAD_COMMAND_ORDER_PASS");
}
static void reassembly_and_wrap(){
 Fixture f(false);auto cid=audio(f);Wire w(f);w.exhaust();auto in=bundle(112);Bytes all={uint8_t(in.size()),uint8_t(in.size()>>8),1,0};all.insert(all.end(),in.begin(),in.end());
 f.rawAcl(0x25,2,Bytes(all.begin(),all.begin()+101));w.poll(3);f.rawAcl(0x25,1,Bytes(all.begin()+101,all.end()));w.poll(3);
 CHECK(f.host.classic_deferred_count_==1&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding);
 f.acl(0x25,cid,media(1));w.poll(3);CHECK(f.media==1);w.restore();w.finish();w.info(1,112);
 // A real zero deadline is occupied, so it expires on wrap rather than becoming immortal.
 Fixture zero(false);audio(zero);Wire z(zero);z.exhaust();zero.now=uint32_t(0u-5000u);zero.acl(0x25,1,bundle(112));zero.host.poll(zero.now);z.collect();CHECK(zero.host.classic_deferred_count_==1&&zero.host.classic_deferred_[zero.host.classic_deferred_read_].deadline==0);
 zero.now=0xffffffffu;zero.host.poll(zero.now);z.collect();CHECK(zero.playing);zero.now=0;zero.host.poll(zero.now);z.collect();CHECK(!zero.playing&&zero.host.links_.classic.disconnecting);
 puts("REASSEMBLED_INPUT_OWNERSHIP_ZERO_DEADLINE_WRAP_PASS");
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);bounded_backpressure_and_media();one_owner_budget();benign_capacity_full();malformed_and_mutating_tail();teardown_cursor();echo_and_information_order();reassembly_and_wrap();printf("EXPANDED_SIGNALING_PASS checks=%u host_bytes=%zu\n",checks,sizeof(OwnedHost));}
