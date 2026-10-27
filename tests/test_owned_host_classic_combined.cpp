#include <algorithm>
#include <new>
#define private public
#include "../src/owned/OwnedHost.cpp"
#undef private
#define main original_owned_fixture_main
#include "test_owned_host.cpp"
#undef main

static unsigned copied_acl,returned_rx;
static std::vector<Fixture::Pdu> copied;
static void drain(Fixture& f,bool ncp=false,unsigned ticks=10){
 for(unsigned k=0;k<ticks;++k){f.poll();while(!f.tx.empty()){
  auto p=f.pop();if(p[0]==2){++copied_acl;CHECK(p.size()>=9);copied.push_back({uint16_t(bth_read_le16(p.data()+1)&0xfff),bth_read_le16(p.data()+7),Bytes(p.begin()+9,p.end())});if(ncp)f.event(0x13,{1,0x25,0,1,0});}
  else if(Fixture::op(p)==0x0c35){CHECK(p.size()==9&&p[4]==1&&bth_read_le16(p.data()+5)==0x25);returned_rx+=bth_read_le16(p.data()+7);}
  else f.acknowledge(p);
 }}
}
static Bytes media_packet(unsigned seq){return {0x80,0x60,uint8_t(seq>>8),uint8_t(seq),1,2,3,4,5,6,7,8,1,0xaa};}
static bool response(uint16_t cid,const Bytes& p){for(const auto& v:copied)if(v.cid==cid&&v.bytes==p)return true;return false;}
struct Playing {
 Fixture f;uint16_t avrcp,sdp,signal,media;
 Playing(){f.host.end();f.config.callbacks.volume=[](uint8_t,void*){};CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();
  avrcp=f.channel(0x17,0x80);sdp=f.channel(1,0x81);signal=f.channel(0x19,0x82);
  f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();media=f.channel(0x19,0x83);f.acl(0x25,signal,{0x30,7,4});f.settle();
  CHECK(f.playing&&f.host.radioStats().streaming&&f.host.flow_.classic.available==8&&!f.host.control_.used&&!f.host.rx_.outstanding);
  copied.clear();copied_acl=returned_rx=0;
 }
 void exhaust(){for(unsigned n=1;n<=8;++n){f.acl(0x25,1,{8,uint8_t(n),0,0});drain(f);CHECK(response(1,{9,uint8_t(n),0,0}));CHECK(f.host.flow_.classic.available==8-n&&!f.host.rx_.outstanding);}CHECK(copied_acl==8&&returned_rx==8);}
 void threeReplies(){
  f.acl(0x25,sdp,{2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,4,0});drain(f);
  f.acl(0x25,avrcp,{0x50,0x11,0x0e,1,0xff,0x30,0xff,0xff,0xff,0xff,0xff});drain(f);
  f.acl(0x25,signal,{0x40,2,4});drain(f);CHECK(f.host.control_.peers[0].owned==3&&!f.host.rx_.outstanding);
 }
};
static void isolation64(){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.acl(0x25,1,{8,9,0,0});drain(f);
 CHECK(!f.host.rx_.outstanding); // RED on the original actor: Echo holds its lease.
 const unsigned delivered=f.media,stops=f.stops,configured=f.configured;
 for(unsigned n=0;n<64;++n){CHECK(f.host.rx_.outstanding<4);f.acl(0x25,p.media,media_packet(n));drain(f);
  CHECK(f.media==delivered+n+1&&f.last_media==media_packet(n));CHECK(!f.host.rx_.outstanding&&returned_rx==13+n&&copied_acl==8&&f.host.flow_.classic.available==0);
 }
 CHECK(f.playing&&f.stops==stops&&f.configured==configured&&f.host.radioStats().transport_errors==0);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,80);
 CHECK(response(1,{9,9,0,0})&&copied_acl==12&&returned_rx==76&&!f.host.control_.used&&f.host.flow_.classic.available==8);
 CHECK(copied.size()==12&&copied[8].cid==0x81&&copied[9].cid==0x80&&copied[10].cid==0x82&&copied[11].cid==1);
 std::puts("Scheduling: actual profile setup, BR8 exhausted, three profile replies, deferred Echo9,64 ordered media SDUs and genuine HNC, then exact FIFO response recovery PASS");
}
static Bytes echo_request(unsigned id,unsigned bytes=0){Bytes p={8,uint8_t(id),uint8_t(bytes),uint8_t(bytes>>8)};for(unsigned k=0;k<bytes;++k)p.push_back(uint8_t(k+id));return p;}
static Bytes echo_response(Bytes p){p[0]=9;return p;}
static void fragmented(Fixture& f,uint16_t cid,const Bytes& p){
 Bytes wire={uint8_t(p.size()),uint8_t(p.size()>>8),uint8_t(cid),uint8_t(cid>>8)};wire.insert(wire.end(),p.begin(),p.end());
 size_t at=0;for(unsigned k=0;at<wire.size();++k){size_t n=k?std::min(size_t(1019),wire.size()-at):2;Bytes part(wire.begin()+at,wire.begin()+at+n);f.rawAcl(0x25,k?1:2,part);drain(f);at+=n;}
}
static void overflow_and_fragment(bool reassembled){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();const Bytes first=echo_request(9,668),second=echo_request(10,5),third=echo_request(11,1),fourth=echo_request(12,17);
 fragmented(f,1,first);f.acl(0x25,1,second);drain(f);fragmented(f,1,third);
 CHECK(f.host.classic_deferred_count_==3&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding);
 Bytes large(2048);for(unsigned k=0;k<large.size();++k)large[k]=uint8_t(k*7);const auto delivered=f.media;fragmented(f,p.media,large);CHECK(f.media==delivered+1&&f.last_media==large);
 if(reassembled)fragmented(f,1,fourth);else {f.acl(0x25,1,fourth);drain(f);}
 CHECK(f.host.peers_[0].has_pending&&f.host.peers_[0].pending.payload.len==fourth.size()&&f.host.classic_deferred_count_==3);
 CHECK((f.host.peers_[0].borrowed_slot<0)==reassembled);CHECK(!memcmp(f.host.peers_[0].pending.payload.data,fourth.data(),fourth.size()));
 f.acl(0x25,p.media,media_packet(99));drain(f);CHECK(f.media==delivered+1);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,160);
 CHECK(f.media==delivered+2&&f.last_media==media_packet(99)&&!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding);
 CHECK(response(1,echo_response(first))&&response(1,echo_response(second))&&response(1,echo_response(third))&&response(1,echo_response(fourth)));
 CHECK(copied.size()==15&&copied[11].bytes==echo_response(first)&&copied[12].bytes==echo_response(second)&&copied[13].bytes==echo_response(third)&&copied[14].bytes==echo_response(fourth));
 CHECK(f.host.flow_.classic.available==8&&!f.host.radioStats().transport_errors);
}
static void profile_safe_controls(){
 Playing p;auto& f=p.f;p.exhaust();for(unsigned id=9;id<=11;++id){f.acl(0x25,1,echo_request(id));drain(f);}CHECK(f.host.control_.peers[0].owned==3);
 const Bytes sdp={2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,4,0},avrcp={0x50,0x11,0x0e,1,0xff,0x30,0xff,0xff,0xff,0xff,0xff};
 fragmented(f,p.sdp,sdp);fragmented(f,p.avrcp,avrcp);f.acl(0x25,1,echo_request(12));drain(f);CHECK(f.host.classic_deferred_count_==3);
 f.acl(0x25,p.media,media_packet(55));drain(f);CHECK(f.media==1&&f.last_media==media_packet(55));
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,120);CHECK(copied.size()==14&&copied[11].cid==0x81&&copied[12].cid==0x80&&copied[13].cid==1);
 CHECK(!f.host.classic_deferred_count_&&!f.host.rx_.outstanding&&f.host.flow_.classic.available==8);
}
static void lifecycle_barrier(bool mixed){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.acl(0x25,1,echo_request(9));drain(f);const auto delivered=f.media;
 if(mixed){Bytes bundle=echo_request(10);const Bytes close={6,11,4,0,uint8_t(p.media),uint8_t(p.media>>8),0x83,0};bundle.insert(bundle.end(),close.begin(),close.end());f.acl(0x25,1,bundle);}
 else f.acl(0x25,p.signal,{0x60,9,4});drain(f);
 CHECK(f.host.peers_[0].has_pending&&f.host.classic_deferred_count_==1);f.acl(0x25,p.media,media_packet(1));drain(f);CHECK(f.media==delivered&&f.playing);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,120);CHECK(!f.playing&&f.media==delivered&&!f.host.peers_[0].has_pending&&!f.host.classic_deferred_count_&&!f.host.rx_.outstanding);
 CHECK(!f.host.radioStats().transport_errors);
}
static void generation_cleanup(bool reassembled){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();for(unsigned id=9;id<=11;++id){f.acl(0x25,1,echo_request(id));drain(f);}
 const auto fourth=echo_request(12,9);if(reassembled)fragmented(f,1,fourth);else {f.acl(0x25,1,fourth);drain(f);}
 CHECK(f.host.classic_deferred_count_==3&&f.host.peers_[0].has_pending);const auto old_owner=f.host.peers_[0].owner;
 f.event(5,{0,0x25,0,0x13});drain(f);CHECK(!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding&&!f.host.control_.used);
 f.classicConnect();CHECK(f.host.peers_[0].owner!=old_owner&&f.host.peers_[0].handle==0x25);copied.clear();copied_acl=returned_rx=0;
 f.acl(0x25,1,echo_request(99));drain(f,true,60);CHECK(copied.size()==1&&copied[0].bytes==echo_response(echo_request(99))&&returned_rx==1);
 CHECK(!f.host.radioStats().transport_errors&&!f.media);
}
static void timeout_wrap(){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.now=0xfffff800u;f.acl(0x25,1,echo_request(9));drain(f);
 const auto deadline=f.host.classic_deferred_[f.host.classic_deferred_read_].deadline;CHECK(deadline&&deadline<f.now);
 for(unsigned n=0;n<8;++n){f.acl(0x25,p.media,media_packet(n));drain(f);CHECK(f.host.classic_deferred_[f.host.classic_deferred_read_].deadline==deadline);}
 f.poll(5000);const auto* link=bth_links_find(&f.host.links_,0x25);CHECK(link&&link->disconnecting&&!f.host.classic_deferred_count_&&!f.playing);
}
static void unavailable_transport_and_event_gate(){
 {Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.acl(0x25,1,echo_request(9));drain(f);f.ready=false;const auto credits=returned_rx;
  for(unsigned n=0;n<4;++n){f.acl(0x25,p.media,media_packet(n));drain(f);}CHECK(f.media==4&&f.host.rx_.outstanding==4&&returned_rx==credits&&f.tx.empty());
  f.ready=true;drain(f);CHECK(!f.host.rx_.outstanding&&returned_rx==credits+4&&f.host.flow_.classic.available==0);
 }
 {Playing p;auto& f=p.f;f.ready=false;while(bth_cmdq_available(&f.host.commands_))CHECK(bth_cmdq_push(&f.host.commands_,0x0c14,nullptr,0,false)==BTH_OK);
  f.event(8,{0,0x25,0,0});CHECK(f.host.deferred_count_==1);f.acl(0x25,p.media,media_packet(1));for(unsigned n=0;n<40;++n)f.poll();CHECK(!f.media&&f.host.deferred_count_==1&&!f.host.rx_.outstanding);
 }
}
static void oversize_policy(){
 {Playing p;auto& f=p.f;CHECK(f.host.peers_[0].assembly.capacity==2052);f.rawAcl(0x25,2,{1,8,0x99,0});drain(f);const auto* link=bth_links_find(&f.host.links_,0x25);CHECK(link&&link->disconnecting&&!f.playing);}
 {Fixture f;f.boot();f.leConnect();CHECK(f.host.peers_[1].assembly.capacity==4096);}
}
static void zero_deadline_and_growth(){
 {Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.now=uint32_t(0u-5000u);f.acl(0x25,1,echo_request(9));f.host.poll(f.now);
  CHECK(f.host.classic_deferred_count_==1&&f.host.classic_deferred_[f.host.classic_deferred_read_].deadline==0);
  f.now=0xffffffffu;f.host.poll(f.now);CHECK(f.host.classic_deferred_count_==1&&f.playing);f.now=0;f.host.poll(f.now);CHECK(!f.host.classic_deferred_count_&&!f.playing&&f.host.links_.classic.disconnecting);
 }
 {Playing p;auto& f=p.f;p.exhaust();p.threeReplies();f.now=uint32_t(0u-6000u);for(unsigned id=9;id<=11;++id){f.acl(0x25,1,echo_request(id));drain(f);}
  f.now=uint32_t(0u-5000u);f.acl(0x25,1,echo_request(12));f.host.poll(f.now);CHECK(f.host.peers_[0].has_pending&&f.host.peers_[0].pending_deadline==0);
  f.event(0x13,{1,0x25,0,1,0});drain(f,false,4);CHECK(!f.host.peers_[0].has_pending&&f.host.classic_deferred_count_==3);
  const auto slot=(f.host.classic_deferred_read_+2)%3;CHECK(f.host.classic_deferred_[slot].deadline==0);
 }
 {Playing p;auto& f=p.f;p.exhaust();p.threeReplies();Bytes growth;for(unsigned n=0;n<112;++n){const Bytes info={10,uint8_t(n+1),2,0,3,0};growth.insert(growth.end(),info.begin(),info.end());}
  CHECK(growth.size()==672);f.acl(0x25,1,growth);drain(f);CHECK(!f.host.classic_deferred_count_&&f.host.peers_[0].has_pending);
  f.acl(0x25,p.media,media_packet(1));drain(f);CHECK(!f.media); // Unservable aggregate reply remains a documented barrier.
 }
}
static void input_priority_and_recovery_cancel(){
 {Playing p;auto& f=p.f;f.acl(0x25,p.avrcp,{0x30,0x11,0x0e,3,0x48,0,0,0x19,0x58,0x31,0,0,5,13,0,0,0,0});f.settle();
  CHECK(f.host.classic_.target_.notification_active);p.exhaust();p.threeReplies();f.acl(0x25,1,echo_request(9));drain(f);CHECK(f.host.setVolume(99)==Status::Ok&&f.host.classic_.target_.notification_due);
  f.event(0x13,{1,0x25,0,8,0});drain(f,true,120);CHECK(copied.size()==13&&copied[11].bytes==echo_response(echo_request(9))&&copied[12].cid==0x80&&copied[12].bytes[9]==0x31);
 }
 {Playing p;auto& f=p.f;p.exhaust();f.acl(0x25,1,{2,99,4,0,0x17,0,0x99,0});for(unsigned id=9;id<=11;++id)f.acl(0x25,1,echo_request(id));drain(f);
  CHECK(f.host.avrcp_recovering_&&f.host.classic_deferred_count_==1&&bth_l2cap_sig_find(&f.host.signaling_,p.avrcp)->state==BTH_CHANNEL_OPEN);
  f.event(0x13,{1,0x25,0,1,0});drain(f,false,4);CHECK(f.host.classic_deferred_count_==1&&bth_l2cap_sig_find(&f.host.signaling_,p.avrcp)->state==BTH_CHANNEL_DISCONNECTING&&f.playing);
 }
}
static void closed_cid_purges_overflow(bool reassembled){
 Playing p;auto& f=p.f;p.exhaust();for(unsigned id=9;id<=11;++id){f.acl(0x25,1,echo_request(id));drain(f);}
 const Bytes sdp={2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,4,0};f.acl(0x25,1,echo_request(12));drain(f);f.acl(0x25,p.sdp,sdp);drain(f);f.acl(0x25,1,echo_request(13));drain(f);
 // An additional SDP request is pressure input, not a claim about legal SDP pipelining.
 if(reassembled)fragmented(f,p.sdp,sdp);else {f.acl(0x25,p.sdp,sdp);drain(f);}CHECK(f.host.classic_deferred_count_==3&&f.host.peers_[0].has_pending);
 f.event(0x13,{1,0x25,0,1,0});f.poll();while(!f.tx.empty()){auto packet=f.pop();CHECK(packet[0]==2);++copied_acl;copied.push_back({uint16_t(bth_read_le16(packet.data()+1)&0xfff),bth_read_le16(packet.data()+7),Bytes(packet.begin()+9,packet.end())});}
 // Exercise the actor's actual local teardown helper after one genuine slot frees.
 CHECK(f.host.disconnectChannel(p.sdp));f.host.synchronizeChannels();CHECK(f.host.classic_deferred_count_==2&&!f.host.peers_[0].has_pending&&f.host.peers_[0].borrowed_slot<0);
 for(unsigned k=0;k<f.host.classic_deferred_count_;++k)CHECK(f.host.classic_deferred_[(f.host.classic_deferred_read_+k)%3].cid!=p.sdp);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,120);CHECK(!f.host.classic_deferred_count_&&!f.host.rx_.outstanding&&!f.host.radioStats().transport_errors);
 for(const auto& x:copied)CHECK(x.cid!=0x81);const auto* c=bth_l2cap_sig_find(&f.host.signaling_,p.sdp);CHECK(c&&c->state==BTH_CHANNEL_DISCONNECTING);const auto id=c->disconnect_id;
 f.acl(0x25,1,{7,id,4,0,0x81,0,uint8_t(p.sdp),uint8_t(p.sdp>>8)});drain(f,true);const auto reopened=f.channel(1,0x81);CHECK(reopened==p.sdp);
 CHECK(!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending);
}
int scheduling_author_main(){isolation64();overflow_and_fragment(false);overflow_and_fragment(true);profile_safe_controls();lifecycle_barrier(false);lifecycle_barrier(true);generation_cleanup(false);generation_cleanup(true);timeout_wrap();zero_deadline_and_growth();input_priority_and_recovery_cancel();closed_cid_purges_overflow(false);closed_cid_purges_overflow(true);unavailable_transport_and_event_gate();oversize_policy();original_owned_fixture_main();std::printf("Private scheduling %u checks PASS\n",checks);return 0;}

static Bytes combined_qos(){return {3,22,0,2,0xe8,3,0,0,0,2,0,0,0xe8,3,0,0,0x40,0x42,0x0f,0,0x40,0x42,0x0f,0};}
static Bytes combined_config(uint16_t cid,uint8_t id){const auto q=combined_qos();Bytes p={4,id,uint8_t(q.size()+4),0,uint8_t(cid),uint8_t(cid>>8),0,0};p.insert(p.end(),q.begin(),q.end());return p;}
static Bytes combined_set(uint8_t label){return {uint8_t(label<<4),3,4,8,1,0,7,6,0,0,0x21,0x15,2,53};}
static bool combined_reply(uint16_t cid,const Bytes& p){return response(cid,p);}
static void combined_rejected_config_fifo_fragment(){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();const auto before=*bth_l2cap_sig_find(&f.host.signaling_,p.signal);const auto stops=f.stops,prepared=f.configured,media=f.media;
 fragmented(f,1,echo_request(12,19));f.acl(0x25,1,{10,35,2,0,2,0});drain(f);CHECK(f.host.classic_deferred_count_==2&&!f.host.rx_.outstanding);
 f.acl(0x25,1,combined_config(p.signal,40));drain(f);CHECK(f.host.peers_[0].has_pending&&f.host.peers_[0].pending.cid==1);
 Bytes audio(1024);for(unsigned n=0;n<audio.size();++n)audio[n]=uint8_t(n*11);fragmented(f,p.media,audio);CHECK(f.host.rx_.outstanding==4&&f.media==media&&f.playing);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,160);
 CHECK(!f.host.rx_.outstanding&&!f.host.peers_[0].has_pending&&!f.host.classic_deferred_count_&&f.host.flow_.classic.available==8);
 CHECK(f.media==media+1&&f.last_media==audio&&f.playing&&f.stops==stops&&f.configured==prepared&&!f.host.radioStats().transport_errors);
 CHECK(!memcmp(bth_l2cap_sig_find(&f.host.signaling_,p.signal),&before,sizeof before));
 CHECK(copied.size()==14&&copied[11].bytes==echo_response(echo_request(12,19))&&copied[12].bytes==Bytes({11,35,8,0,2,0,0,0,0x80,0,0,0}));
 const auto& reject=copied[13].bytes;CHECK(copied[13].cid==1&&reject.size()==34&&reject[0]==5&&reject[1]==40&&bth_read_le16(reject.data()+8)==3&&Bytes(reject.begin()+10,reject.end())==combined_qos());
 std::puts("COMBINED rejected OPEN config behind copied FIFO, HBS4 fragmented media barrier, exact rejection/FIFO/HNC recovery PASS");
}
static void combined_abort_fifo_config_reuse(){
 Playing p;auto& f=p.f;p.exhaust();p.threeReplies();fragmented(f,1,echo_request(12,668));f.acl(0x25,1,echo_request(13));drain(f);f.acl(0x25,1,echo_request(14));drain(f);CHECK(f.host.classic_deferred_count_==3);
 f.acl(0x25,p.signal,{0x70,10,4});drain(f);CHECK(f.host.peers_[0].has_pending&&f.playing&&!f.host.classic_.abortTransportPending());
 Bytes old_audio(1024,0xa5);fragmented(f,p.media,old_audio);CHECK(f.host.rx_.outstanding==4&&!f.media);
 f.event(0x13,{1,0x25,0,8,0});drain(f,true,200);
 CHECK(!f.playing&&!f.media&&f.host.classic_.abortTransportPending()&&!f.host.classic_.configured()&&!f.host.classic_.mediaCid());
 CHECK(!f.host.peers_[0].has_pending&&!f.host.classic_deferred_count_&&!f.host.rx_.outstanding&&combined_reply(0x82,{0x72,10}));
 const auto* old=bth_l2cap_sig_find(&f.host.signaling_,p.media);CHECK(old&&old->state==BTH_CHANNEL_DISCONNECTING);const auto old_id=old->disconnect_id;
 // The rejected OPEN guard must preserve signaling even while Abort owns an old media transport.
 const auto signal_before=*bth_l2cap_sig_find(&f.host.signaling_,p.signal);f.acl(0x25,1,combined_config(p.signal,41));drain(f,true);
 CHECK(!memcmp(bth_l2cap_sig_find(&f.host.signaling_,p.signal),&signal_before,sizeof signal_before)&&f.host.classic_.signalCid()==p.signal&&f.host.classic_.abortTransportPending());
 f.acl(0x25,p.signal,combined_set(8));drain(f,true);CHECK(combined_reply(0x82,{0x83,3,0,0x13}));
 f.acl(0x25,p.media,combined_set(9));drain(f,true);CHECK(!f.media&&f.host.classic_.signalCid()==p.signal&&!f.host.classic_.mediaCid());
 f.acl(0x25,1,{7,old_id,4,0,0x83,0,uint8_t(p.media),uint8_t(p.media>>8)});drain(f,true);CHECK(!f.host.classic_.abortTransportPending()&&f.host.classic_.avdtp_.state==BTH_AVDTP_IDLE);
 f.acl(0x25,p.signal,combined_set(9));drain(f,true);CHECK(combined_reply(0x82,{0x92,3}));f.acl(0x25,p.signal,{0xa0,6,4});drain(f,true);const auto fresh=f.channel(0x19,0x93);CHECK(fresh==p.media);
 // A stale old Disconnect Response must not affect the reused local CID.
 f.acl(0x25,1,{7,old_id,4,0,0x83,0,uint8_t(p.media),uint8_t(p.media>>8)});drain(f,true);CHECK(bth_l2cap_sig_find(&f.host.signaling_,fresh)->state==BTH_CHANNEL_OPEN&&f.host.classic_.mediaCid()==fresh);
 f.acl(0x25,p.signal,{0xb0,7,4});drain(f,true);CHECK(f.playing);f.acl(0x25,fresh,media_packet(99));drain(f,true);CHECK(f.media==1&&f.last_media==media_packet(99)&&!f.host.radioStats().transport_errors);
 std::puts("COMBINED copied FIFO/fragmented overflow Abort barrier, rejected signaling config during retirement, exact old-media role and CID reuse PASS");
}
static void combined_partial_abort_signal_loss(){
 Fixture f(false);f.boot();f.classicConnect();const auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,combined_set(1));f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();
 f.output.clear();f.acl(0x25,1,{2,55,4,0,0x19,0,0x82,0});f.settle();uint16_t old_local=0;uint8_t old_config=0;
 for(const auto& x:f.output)if(x.cid==1){if(x.bytes[0]==3)old_local=bth_read_le16(x.bytes.data()+4);if(x.bytes[0]==4)old_config=x.bytes[1];}CHECK(old_local>=0x40&&old_config&&!f.host.classic_.mediaCid());
 copied.clear();copied_acl=returned_rx=0;for(unsigned id=1;id<=8;++id){f.acl(0x25,1,echo_request(id));drain(f);CHECK(f.host.flow_.classic.available==8-id&&!f.host.rx_.outstanding);}
 for(unsigned id=9;id<=11;++id){f.acl(0x25,1,echo_request(id));drain(f);}fragmented(f,1,echo_request(12,13));CHECK(f.host.classic_deferred_count_==1);
 f.acl(0x25,signal,{0x30,10,4});drain(f);CHECK(f.host.peers_[0].has_pending&&!f.host.classic_.abortTransportPending());f.event(0x13,{1,0x25,0,8,0});drain(f,true,140);
 CHECK(f.host.classic_.abortTransportPending()&&!f.host.classic_.mediaCid()&&f.host.classic_.signalCid()==signal);const auto* old=bth_l2cap_sig_find(&f.host.signaling_,old_local);CHECK(old&&old->state==BTH_CHANNEL_DISCONNECTING);const auto old_disconnect=old->disconnect_id;
 f.acl(0x25,1,{5,old_config,6,0,uint8_t(old_local),uint8_t(old_local>>8),0,0,0,0});drain(f,true);f.acl(0x25,1,{4,56,8,0,uint8_t(old_local),uint8_t(old_local>>8),0,0,1,2,0xa0,2});drain(f,true);
 CHECK(!f.host.classic_.mediaCid()&&f.host.classic_.abortTransportPending());
 f.acl(0x25,1,{6,57,4,0,uint8_t(signal),uint8_t(signal>>8),0x81,0});drain(f,true);CHECK(!f.host.classic_.signalCid()&&f.host.classic_.abortTransportPending());
 fragmented(f,old_local,combined_set(4));CHECK(!f.host.classic_.signalCid()&&!f.host.classic_.mediaCid()&&!f.media);
 f.acl(0x25,1,{7,old_disconnect,4,0,0x82,0,uint8_t(old_local),uint8_t(old_local>>8)});drain(f,true);CHECK(!f.host.classic_.abortTransportPending());
 const auto fresh_signal=f.channel(0x19,0x91);CHECK(fresh_signal==signal);f.acl(0x25,fresh_signal,combined_set(5));drain(f,true);CHECK(combined_reply(0x91,{0x52,3}));f.acl(0x25,fresh_signal,{0x60,6,4});drain(f,true);const auto fresh_media=f.channel(0x19,0x92);CHECK(fresh_media==old_local);
 f.acl(0x25,1,{5,old_config,6,0,uint8_t(old_local),uint8_t(old_local>>8),0,0,0,0});drain(f,true);f.acl(0x25,1,{7,old_disconnect,4,0,0x82,0,uint8_t(old_local),uint8_t(old_local>>8)});drain(f,true);
 CHECK(f.host.classic_.signalCid()==fresh_signal&&f.host.classic_.mediaCid()==fresh_media&&bth_l2cap_sig_find(&f.host.signaling_,fresh_media)->state==BTH_CHANNEL_OPEN);
 f.acl(0x25,fresh_signal,{0x70,7,4});drain(f,true);f.acl(0x25,fresh_media,media_packet(100));drain(f,true);CHECK(f.playing&&f.media==1&&!f.host.radioStats().transport_errors&&!f.host.classic_deferred_count_&&!f.host.rx_.outstanding);
 std::puts("COMBINED partial media Abort behind copied control, old completion/signaling-loss fence, fragmented old role rejection, reused CID late identifiers PASS");
}
int main(){combined_rejected_config_fifo_fragment();combined_abort_fifo_config_reuse();combined_partial_abort_signal_loss();std::printf("Combined Classic integration %u checks PASS\n",checks);return 0;}
