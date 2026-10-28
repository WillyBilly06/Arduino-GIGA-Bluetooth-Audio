// Private, unexecuted expected-RED fixture. Actual host/profiles/core, fake wire.
// No radio, phone, serial, decoder, I2S or wall-clock API is used.
#include <algorithm>
#include <new>
#define private public
#include "../../src/owned/OwnedHost.cpp"
#undef private
#define main original_owned_fixture_main
#include "../test_owned_host.cpp"
#undef main

static void requireProgress(bool value,const char* stage){
 ++checks;if(!value){std::printf("CLASSIC_MEDIA_PROGRESS_FAIL stage=%s\n",stage);std::fflush(stdout);std::abort();}
}
struct Response {uint16_t handle=0,cid=0;Bytes bytes;};
struct Wire {
 Fixture& f;bool hold_classic=false;unsigned returned_classic=0,returned_le=0,in_classic=0,in_le=0;std::vector<Response> responses;
 explicit Wire(Fixture& peer):f(peer){}
 void accounting(){
  requireProgress(f.host.rx_.outstanding<=OwnedHost::RxWindow,"global_rx_budget");
  unsigned outstanding=0,pending=0;for(const auto& link:f.host.rx_.links)if(link.active){requireProgress(link.pending<=link.outstanding,"per_peer_pending_bound");outstanding+=link.outstanding;pending+=link.pending;}
  requireProgress(outstanding==f.host.rx_.outstanding&&pending<=outstanding,"per_peer_sum");
  requireProgress(f.host.control_.used<=BTH_CONTROL_SLOTS&&f.host.control_.peers[0].owned<=BTH_CONTROL_PEER_LIMIT&&f.host.control_.peers[1].owned<=BTH_CONTROL_PEER_LIMIT,"reply_bounds");
 }
 void collect(){
  while(!f.tx.empty()){
   auto packet=f.pop();if(packet[0]==2){
    requireProgress(packet.size()>=9&&((packet[2]>>4)&3)==0,"whole_small_control_reply");
    const uint16_t handle=bth_read_le16(packet.data()+1)&0xfff,cid=bth_read_le16(packet.data()+7);
    requireProgress(packet.size()-9==bth_read_le16(packet.data()+5),"control_sdu_length");
    responses.push_back({handle,cid,Bytes(packet.begin()+9,packet.end())});
    if(handle!=0x25||!hold_classic)f.event(0x13,{1,uint8_t(handle),uint8_t(handle>>8),1,0});
   }else if(Fixture::op(packet)==0x0c35){
    requireProgress(packet.size()==9&&packet[4]==1,"host_ncp_shape");
    const uint16_t handle=bth_read_le16(packet.data()+5),count=bth_read_le16(packet.data()+7);
    requireProgress(count!=0,"nonzero_host_ncp");
    if(handle==0x25)returned_classic+=count;else if(handle==0x26)returned_le+=count;else requireProgress(false,"host_ncp_owner");
   }else f.acknowledge(packet);
  }accounting();
 }
 void pump(unsigned ticks=1){for(unsigned i=0;i<ticks;++i){f.poll();collect();}}
 void acl(uint16_t handle,uint16_t cid,const Bytes& payload){
  requireProgress(f.host.rx_.outstanding<OwnedHost::RxWindow,"controller_obeys_host_window");
  if(handle==0x25)++in_classic;else if(handle==0x26)++in_le;
  f.acl(handle,cid,payload);pump(2);
 }
 void fragmented(uint16_t handle,uint16_t cid,const Bytes& payload,bool split_header=false){
  Bytes sdu={uint8_t(payload.size()),uint8_t(payload.size()>>8),uint8_t(cid),uint8_t(cid>>8)};sdu.insert(sdu.end(),payload.begin(),payload.end());
  size_t offset=0;unsigned index=0;while(offset<sdu.size()){
   requireProgress(f.host.rx_.outstanding<OwnedHost::RxWindow,"fragment_obeys_host_window");
   const size_t take=!index&&split_header?2:std::min(size_t(1021),sdu.size()-offset);
   if(handle==0x25)++in_classic;else ++in_le;
   f.rawAcl(handle,index?1:2,Bytes(sdu.begin()+offset,sdu.begin()+offset+take));offset+=take;++index;pump(2);
  }
 }
 void release(){requireProgress(hold_classic&&f.host.flow_.classic.available==0,"release_real_controller_credits");f.event(0x13,{1,0x25,0,8,0});hold_classic=false;pump(180);}
};
static unsigned readonly_mode=0;
static Bytes unitInfo(unsigned label){
 if(readonly_mode==1)return {uint8_t(label<<4),0x11,0x0e,1,0xff,0x31,7,0xff,0xff,0xff,0xff};
 if(readonly_mode==4)return {uint8_t(label<<4),0x11,0x0e,1,0xff,0x30,0,1,2,3,4};
 if(readonly_mode==5)return {uint8_t(label<<4),0x11,0x0e,1,0xff,0x31,7,1,2,3,4};
 if(readonly_mode==2||readonly_mode==3)return {uint8_t(label<<4),0x11,0x0e,1,0x48,0,0,0x19,0x58,0x10,0,0,1,uint8_t(readonly_mode)};
 return {uint8_t(label<<4),0x11,0x0e,1,0xff,0x30,0xff,0xff,0xff,0xff,0xff};
}
static Bytes serviceSearch(){return {2,0x12,0x34,0,8,0x35,3,0x19,0x11,0x0b,0,4,0};}
static Bytes mediaPacket(unsigned sequence,size_t size){
 Bytes p(size);for(size_t i=0;i<size;++i)p[i]=uint8_t(i*13+sequence);p[0]=0x80;p[1]=0x60;p[2]=uint8_t(sequence>>8);p[3]=uint8_t(sequence);p[12]=1;return p;
}
struct Playing {
 Fixture f;Wire wire;uint16_t avrcp=0,sdp=0,signal=0,media=0;
 explicit Playing(bool ble=false,uint32_t seed=10):wire(f){
  f.host.end();f.now=seed;f.config.callbacks.volume=[](uint8_t,void*){};requireProgress(f.host.begin(f.config,f.hooks)==Status::Ok,"begin_actual_profiles");f.boot();f.classicConnect();
  avrcp=f.channel(0x17,0x80);sdp=f.channel(1,0x81);signal=f.channel(0x19,0x82);
  f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();media=f.channel(0x19,0x83);f.acl(0x25,signal,{0x30,7,4});f.settle();
  if(ble)f.leConnect();
  requireProgress(f.playing&&f.host.radioStats().streaming&&f.host.flow_.classic.available==8&&!f.host.control_.used&&!f.host.rx_.outstanding,"actual_secure_playing_setup");
 }
 uint32_t exhaust(){
  wire.hold_classic=true;
  for(unsigned id=1;id<=8;++id){wire.acl(0x25,1,{8,uint8_t(id),0,0});requireProgress(f.host.flow_.classic.available==8-id,"real_credit_exhaustion");}
  requireProgress(!f.host.rx_.outstanding,"initial_rx_credits_returned");wire.responses.clear();return f.now;
 }
 std::vector<std::pair<uint16_t,Bytes>> requests(bool pending_sdp)const{
  std::vector<std::pair<uint16_t,Bytes>> out;
  if(!pending_sdp)out.push_back({sdp,serviceSearch()});
  const unsigned first=pending_sdp?0:1;
  for(unsigned label=first;label<first+6;++label)out.push_back({avrcp,unitInfo(label)});
  if(pending_sdp)out.push_back({sdp,serviceSearch()});
  return out;
 }
};
static std::vector<Response> normalResponses(bool pending_sdp){
 Playing baseline;auto requests=baseline.requests(pending_sdp);
 for(const auto& request:requests)baseline.wire.acl(0x25,request.first,request.second);
 baseline.wire.pump(30);std::vector<Response> out;
 for(const auto& response:baseline.wire.responses)if(response.handle==0x25&&((response.cid==0x80&&!response.bytes.empty()&&(response.bytes[0]&2))||(response.cid==0x81&&response.bytes.size()>=3&&response.bytes[0]==3&&response.bytes[1]==0x12&&response.bytes[2]==0x34)))out.push_back(response);
 requireProgress(out.size()==7,"normal_control_oracle_count");return out;
}
static void pendingView(Playing& p,const std::pair<uint16_t,Bytes>& request){
 const auto& peer=p.f.host.peers_[0];requireProgress(peer.has_pending,"pending_control_retained");
 requireProgress(peer.pending.cid==request.first&&peer.pending.payload.len==request.second.size()&&peer.pending.payload.data&&!std::memcmp(peer.pending.payload.data,request.second.data(),request.second.size()),"pending_control_payload_pinned");
}
static void progress(bool pending_sdp,bool pending_fragmented,bool media_fragmented,bool ble,bool split_media_header=false){
 const auto expected=normalResponses(pending_sdp);Playing p(ble);auto& f=p.f;auto& wire=p.wire;const uint32_t held_since=p.exhaust();const auto requests=p.requests(pending_sdp);
 for(unsigned i=0;i<requests.size();++i){
  if(i+1==requests.size()&&pending_fragmented)wire.fragmented(0x25,requests[i].first,requests[i].second,true);else wire.acl(0x25,requests[i].first,requests[i].second);
  if(i==2)requireProgress(f.host.control_.peers[0].owned==3,"three_actual_profile_replies");
 }
 requireProgress(f.ready&&f.host.flow_.classic.available==0&&f.host.classic_deferred_count_==3,"bounded_control_pressure");pendingView(p,requests.back());
 const unsigned before=f.media,returned_before=wire.returned_classic,packets_before=wire.in_classic;const uint32_t owner=f.host.peers_[0].owner;
 if(ble){
  requireProgress(f.host.peers_[1].attached&&f.host.peers_[1].owner!=owner,"distinct_LE_owner");const unsigned le_before=wire.returned_le;
  wire.acl(0x26,4,{0x0a,3,0});wire.pump(4);
  bool replied=false;for(const auto& response:wire.responses)if(response.handle==0x26&&response.cid==4&&!response.bytes.empty()&&response.bytes[0]==0x0b)replied=true;
  requireProgress(replied&&wire.returned_le>le_before,"unrelated_LE_read_progress");
 }
 unsigned expected_media=0;Bytes last;
 if(media_fragmented){last=mediaPacket(700,2048);wire.fragmented(0x25,p.media,last,split_media_header);expected_media=1;}
 else {const unsigned count=OwnedHost::RxWindow-f.host.rx_.outstanding;requireProgress(count>0&&count<=4,"available_media_receive_window");for(unsigned n=0;n<count;++n){last=mediaPacket(700+n,14);wire.acl(0x25,p.media,last);++expected_media;}}
 while(uint32_t(f.now-held_since)<110)wire.pump();
 pendingView(p,requests.back());wire.accounting();
 std::printf("CLASSIC_MEDIA_PROGRESS observed pending=%s control_fragmented=%u media_fragmented=%u ble=%u elapsed_ms=%lu classic_tx_available=%u received_media=%u expected_media=%u rx_outstanding=%u pending_hnc=%u deferred=%u transport_errors=%lu dropped=%lu\n",pending_sdp?"SDP":"AVRCP",unsigned(pending_fragmented),unsigned(media_fragmented),unsigned(ble),(unsigned long)(f.now-held_since),unsigned(f.host.flow_.classic.available),f.media-before,expected_media,unsigned(f.host.rx_.outstanding),unsigned(f.host.rx_.links[0].pending),unsigned(f.host.classic_deferred_count_),(unsigned long)f.host.radioStats().transport_errors,(unsigned long)f.host.radioStats().dropped_packets);std::fflush(stdout);
 requireProgress(uint32_t(f.now-held_since)<=120&&f.host.flow_.classic.available==0&&f.ready,"controller_credit_hold_within_120ms");
 requireProgress(f.media==before+expected_media&&f.last_media==last,"independent_A2DP_progress_under_safe_control_pressure");
 requireProgress(wire.returned_classic-returned_before>=wire.in_classic-packets_before,"media_host_credits_retired");
 requireProgress(f.host.peers_[0].owner==owner&&f.playing&&!f.host.radioStats().transport_errors&&!f.host.radioStats().dropped_packets,"owner_security_lifecycle_unchanged");
 wire.release();std::vector<Response> actual;
 for(const auto& response:wire.responses)if(response.handle==0x25&&((response.cid==0x80&&!response.bytes.empty()&&(response.bytes[0]&2))||(response.cid==0x81&&response.bytes.size()>=3&&response.bytes[0]==3&&response.bytes[1]==0x12&&response.bytes[2]==0x34)))actual.push_back(response);
 requireProgress(actual.size()==expected.size(),"exact_reply_count_no_replay");for(size_t i=0;i<actual.size();++i)requireProgress(actual[i].cid==expected[i].cid&&actual[i].bytes==expected[i].bytes,"exact_control_reply_FIFO_bytes");
 requireProgress(!f.host.classic_deferred_count_&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding&&f.host.flow_.classic.available==8&&!f.host.radioStats().transport_errors&&!f.host.radioStats().dropped_packets,"exact_recovery_no_credit_leak");
 std::puts("CLASSIC_MEDIA_PROGRESS_CASE_PASS");
}
static void barriers(bool security){
 Playing p;auto& f=p.f;auto& wire=p.wire;p.exhaust();const auto requests=p.requests(false);
 for(unsigned i=0;i<4;++i)wire.acl(0x25,requests[i].first,requests[i].second);
 const unsigned before=f.media;
 if(security){f.event(8,{0,0x25,0,0});wire.pump(2);}
 else {wire.acl(0x25,p.signal,{0x60,9,4});requireProgress(f.host.peers_[0].has_pending,"real_SUSPEND_retained");}
 wire.acl(0x25,p.media,mediaPacket(900,14));wire.pump(5);
 requireProgress(f.media==before,"no_security_or_lifecycle_overtake");
 if(security)requireProgress(!f.host.radioStats().streaming,"security_revocation_fence");
 else {wire.release();requireProgress(!f.playing&&f.media==before,"SUSPEND_precedes_media");}
 std::puts("CLASSIC_MEDIA_BARRIER_CONTROL_PASS");
}
static void fill_safe_pressure(Playing& p){p.exhaust();const auto req=p.requests(false);for(const auto& q:req)p.wire.acl(0x25,q.first,q.second);requireProgress(p.f.host.peers_[0].has_pending&&p.f.host.classic_deferred_count_==3,"safe_pressure_ready");}
static void unsafe_after_safe(){Playing p;auto& f=p.f;auto& w=p.wire;fill_safe_pressure(p);const auto before=f.media;w.acl(0x25,p.signal,{0x60,9,4});w.acl(0x25,p.media,mediaPacket(900,14));w.pump(5);requireProgress(f.media==before,"unsafe_queued_suspend_fence");w.release();requireProgress(!f.playing&&f.media==before,"queued_suspend_precedes_media");std::puts("CLASSIC_UNSAFE_FENCE_PASS");}
static void parked_detach(){Playing p;auto& f=p.f;auto& w=p.wire;fill_safe_pressure(p);const auto before=f.media;w.acl(0x25,p.media,mediaPacket(901,14));requireProgress(f.media==before+1&&f.host.classic_parked_slot_>=0&&f.host.classic_parked_owner_==f.host.peers_[0].owner,"control_parked_before_detach");f.event(5,{0,0x25,0,0x13});w.pump(4);requireProgress(!f.host.peers_[0].attached&&f.host.classic_parked_slot_<0&&f.host.classic_parked_owner_==0&&!f.host.rx_.outstanding,"detach_releases_parked_owner");std::puts("CLASSIC_PARKED_DETACH_PASS");}
static void parked_cid_retire(){Playing p;auto& f=p.f;auto& w=p.wire;fill_safe_pressure(p);w.acl(0x25,p.media,mediaPacket(902,14));requireProgress(f.host.classic_parked_slot_>=0&&f.host.peers_[0].has_pending,"cid_test_parked");f.host.clearClassicControls(p.avrcp);requireProgress(f.host.classic_parked_slot_<0&&!f.host.peers_[0].has_pending,"cid_retire_releases_storage");w.accounting();std::puts("CLASSIC_PARKED_CID_RETIRE_PASS");}
static void stale_parked_owner(){Playing p;auto& f=p.f;auto& w=p.wire;fill_safe_pressure(p);w.acl(0x25,p.media,mediaPacket(903,14));requireProgress(f.host.classic_parked_slot_>=0,"stale_test_parked");const auto before=f.media;const auto owner=f.host.classic_parked_owner_;f.host.classic_parked_owner_=owner+1;w.acl(0x25,p.media,mediaPacket(904,14));w.pump(3);requireProgress(f.media==before,"stale_owner_cannot_bypass");f.host.classic_parked_owner_=owner;w.pump(3);requireProgress(f.media==before+1,"restored_owner_progress");w.accounting();std::puts("CLASSIC_STALE_OWNER_FENCE_PASS");}
static void parked_timeout(){Playing p;auto& f=p.f;auto& w=p.wire;fill_safe_pressure(p);w.acl(0x25,p.media,mediaPacket(905,14));requireProgress(f.host.classic_parked_slot_>=0,"timeout_test_parked");for(unsigned i=0;i<5100&&f.host.peers_[0].attached&&!f.host.links_.classic.disconnecting;++i)w.pump();requireProgress(f.host.links_.classic.disconnecting,"pending_control_deadline_closes_link");f.event(5,{0,0x25,0,0x13});w.pump(3);requireProgress(!f.host.peers_[0].attached&&f.host.classic_parked_slot_<0&&!f.host.rx_.outstanding,"timeout_detach_releases");std::puts("CLASSIC_PARKED_TIMEOUT_PASS");}
static void unsafe_avrcp(const Bytes& candidate,bool volume){Playing p;auto& f=p.f;auto& w=p.wire;p.exhaust();auto requests=p.requests(false);requests.back().second=candidate;for(auto& q:requests)w.acl(0x25,q.first,q.second);pendingView(p,requests.back());const auto before=f.media;const auto old_volume=f.host.classic_.avrcpStatus().volume;w.acl(0x25,p.media,mediaPacket(950,14));w.pump(5);requireProgress(f.media==before&&f.host.classic_.avrcpStatus().volume==old_volume,"unsafe_avrcp_is_media_barrier");if(volume){w.release();requireProgress(f.host.classic_.avrcpStatus().volume==0x33&&f.media==before+1,"volume_precedes_later_media");}std::puts("CLASSIC_AVRCP_UNSAFE_FENCE_PASS");}
int frozen_r5_semantic_selector_main(int argc,char** argv){
 const std::string selector=argc>1?argv[1]:"avrcp_plain_plain";
 if(selector=="avrcp_volume_fence"){unsafe_avrcp({0x60,0x11,0x0e,0,0x48,0,0,0x19,0x58,0x50,0,0,1,0x33},true);return 0;}
 if(selector=="avrcp_notify_fence"){unsafe_avrcp({0x60,0x11,0x0e,3,0x48,0,0,0x19,0x58,0x31,0,0,5,13,0,0,0,0},false);return 0;}
 if(selector=="avrcp_passthrough_fence"){unsafe_avrcp({0x60,0x11,0x0e,0,0x48,0x7c,0x41,0},false);return 0;}
 if(selector=="avrcp_bad_caps_fence"){unsafe_avrcp({0x60,0x11,0x0e,1,0x48,0,0,0x19,0x58,0x10,0,0,1,4},false);return 0;}
 if(selector=="avrcp_fragment_start_fence"){unsafe_avrcp({0x64,2,0x11,0x0e,1,0x48,0,0,0x19,0x58,0x10},false);return 0;}
 for(unsigned mode=1;mode<=5;++mode)for(bool cf:{false,true})for(bool mf:{false,true}){
  const std::string name=std::string(mode==1?"subunit":mode==2?"capability2":mode==3?"capability3":mode==4?"unit_operands":"subunit_operands")+"_"+(cf?"fragment":"plain")+"_"+(mf?"fragment":"plain");
  if(selector==name||selector==name+"_ble"){readonly_mode=mode;progress(false,cf,mf,selector==name+"_ble");return 0;}
 }

 if(selector=="split_media_header"){progress(false,false,true,false,true);return 0;}
 if(selector=="unsafe_after_safe"){unsafe_after_safe();return 0;}if(selector=="parked_detach"){parked_detach();return 0;}if(selector=="parked_cid_retire"){parked_cid_retire();return 0;}if(selector=="stale_parked_owner"){stale_parked_owner();return 0;}if(selector=="parked_timeout"){parked_timeout();return 0;}
 if(selector=="security_control"){barriers(true);return 0;}if(selector=="lifecycle_control"){barriers(false);return 0;}
 bool selected=false;for(bool sdp:{false,true})for(bool control_fragment:{false,true})for(bool media_fragment:{false,true}){
  const std::string name=std::string(sdp?"sdp":"avrcp")+"_"+(control_fragment?"fragment":"plain")+"_"+(media_fragment?"fragment":"plain");
  if(selector==name||selector==name+"_ble"){progress(sdp,control_fragment,media_fragment,selector==name+"_ble");selected=true;}
 }
 requireProgress(selected,"known_selector");return 0;
}
