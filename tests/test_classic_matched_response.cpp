// Private, unexecuted expected-RED fixture. Actual host/profiles/core, fake wire.
// No radio, phone, serial, decoder, I2S or wall-clock API is used.
#include <algorithm>
#include <new>
#define private public
#include "../src/owned/OwnedHost.cpp"
#undef private
#define main original_owned_fixture_main
#include "test_owned_host.cpp"
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
static Bytes unitInfo(unsigned label){return {uint8_t(label<<4),0x11,0x0e,1,0xff,0x30,0xff,0xff,0xff,0xff,0xff};}
static Bytes serviceSearch(){return {2,0x12,0x34,0,8,0x35,3,0x19,0x11,0x0b,0,4,0};}
static Bytes mediaPacket(unsigned sequence,size_t size){
 Bytes p(size);for(size_t i=0;i<size;++i)p[i]=uint8_t(i*13+sequence);p[0]=0x80;p[1]=0x60;p[2]=uint8_t(sequence>>8);p[3]=uint8_t(sequence);p[12]=1;return p;
}
struct Playing {
 Fixture f;Wire wire;uint16_t avrcp=0,sdp=0,signal=0,media=0;
 explicit Playing(bool ble=false,bool metadata=false):wire(f){
  f.host.end();(void)metadata;f.config.callbacks.volume=[](uint8_t,void*){};f.config.callbacks.metadata=[](uint8_t,const char*,size_t,void*){};requireProgress(f.host.begin(f.config,f.hooks)==Status::Ok,"begin_actual_profiles");f.boot();f.classicConnect();
  sdp=f.channel(1,0x81);signal=f.channel(0x19,0x82);
  f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();media=f.channel(0x19,0x83);f.acl(0x25,signal,{0x30,7,4});f.settle();avrcp=f.channel(0x17,0x80);
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
static void progress(bool pending_sdp,bool pending_fragmented,bool media_fragmented,bool ble){
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
 if(media_fragmented){last=mediaPacket(700,2048);wire.fragmented(0x25,p.media,last);expected_media=1;}
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
static unsigned realRequestLabel(Playing& p,uint8_t pdu){
 std::printf("CT_SETUP now=%lu local_avrcp=%u remote_avrcp=128 output_count=%zu\n",(unsigned long)p.f.now,unsigned(p.avrcp),p.f.output.size());
 for(const auto& out:p.f.output){
  std::printf("CT_SETUP_OUT handle=%04x cid=%04x len=%zu bytes",unsigned(out.handle),unsigned(out.cid),out.bytes.size());for(size_t j=0;j<out.bytes.size()&&j<16;++j)std::printf(" %02x",out.bytes[j]);std::puts("");
 }
 for(const auto& out:p.f.output)if(out.handle==0x25&&out.cid==0x80&&out.bytes.size()>=13&&!(out.bytes[0]&3)&&out.bytes[1]==0x11&&out.bytes[2]==0x0e&&out.bytes[9]==pdu){
  const unsigned label=out.bytes[0]>>4;const auto& pending=p.f.host.classic_.controller_.pending[label];const int32_t remaining=int32_t(pending.deadline-p.f.now);
  std::printf("CT_SETUP_PENDING pdu=%02x label=%u kind=%u deadline=%lu remaining=%ld retired=%u\n",unsigned(pdu),label,unsigned(pending.kind),(unsigned long)pending.deadline,(long)remaining,unsigned((p.f.host.classic_.controller_.retired_labels>>label)&1));std::fflush(stdout);
  if(pending.kind&&remaining>0&&!(p.f.host.classic_.controller_.retired_labels&(1u<<label)))return label;
 }
 std::fflush(stdout);requireProgress(false,"real_pending_controller_request_not_found");return 0;
}
static Bytes readResponse(unsigned label,uint8_t pdu){Bytes q={uint8_t((label<<4)|2),0x11,0x0e,12,0x48,0,0,0x19,0x58,pdu,0,0,0};
 if(pdu==0x10){q[12]=4;q.insert(q.end(),{3,2,1,2});}
 else if(pdu==0x30){q[12]=9;q.insert(q.end(),{0,0,1,0,0,0,0,10,1});}
 else if(pdu==0x20){q[12]=12;q.insert(q.end(),{1,0,0,0,1,0,106,0,3,'a','b','c'});}
 else requireProgress(false,"known_controller_pdu");return q;
}
static void responseProgress(uint8_t pdu,bool fragmented){Playing p(false,pdu==0x20);auto& f=p.f;auto& w=p.wire;const unsigned label=realRequestLabel(p,pdu);const Bytes response=readResponse(label,pdu);p.exhaust();for(unsigned i=0;i<6;++i)w.acl(0x25,p.avrcp,unitInfo(i));requireProgress(f.host.classic_deferred_count_==3,"response_test_deferred_full");const auto& pending=f.host.classic_.controller_.pending[label];requireProgress(pending.kind&&int32_t(pending.deadline-f.now)>0,"real_pending_request_still_live");if(fragmented)w.fragmented(0x25,p.avrcp,response,true);else w.acl(0x25,p.avrcp,response);requireProgress(f.host.peers_[0].has_pending,"actual_matched_response_pending");const auto before=f.media;const unsigned credits=w.returned_classic;const unsigned incoming=w.in_classic;const unsigned count=OwnedHost::RxWindow-f.host.rx_.outstanding;requireProgress(count>0,"response_test_receive_window");for(unsigned i=0;i<count;++i)w.acl(0x25,p.media,mediaPacket(980+i,14));for(unsigned i=0;i<110;++i)w.pump();std::printf("CONTROLLER_READ_RESPONSE_OBS pdu=%02x label=%u fragmented=%u media=%u expected=%u credits=%u incoming=%u\n",unsigned(pdu),label,unsigned(fragmented),f.media-before,count,w.returned_classic-credits,w.in_classic-incoming);std::fflush(stdout);requireProgress(f.media==before+count,"matched_read_response_must_not_block_media");requireProgress(w.returned_classic-credits>=w.in_classic-incoming,"matched_response_media_credit");w.release();if(pdu==0x10)requireProgress(f.host.classic_.controller_.capabilities_known&&(f.host.classic_.controller_.supported_events&6)==6,"matched_caps_processed");else if(pdu==0x30)requireProgress(f.host.classic_.avrcpStatus().song_length_ms==256&&f.host.classic_.avrcpStatus().song_position_ms==10&&f.host.classic_.avrcpStatus().playback_status==1,"matched_status_processed");else requireProgress(!std::strcmp(f.host.classic_.avrcpMetadata().title,"abc"),"matched_metadata_processed");requireProgress(!f.host.rx_.outstanding&&!f.host.radioStats().transport_errors,"matched_response_credit_cleanup");std::puts("CONTROLLER_READ_RESPONSE_PASS");}
static void responseFence(Playing& p,const Bytes& response,uint16_t cid,bool expire,unsigned label){auto& f=p.f;auto& w=p.wire;p.exhaust();for(unsigned i=0;i<6;++i)w.acl(0x25,p.avrcp,unitInfo(i));requireProgress(f.host.classic_deferred_count_==3,"fence_deferred_full");w.acl(0x25,cid,response);requireProgress(f.host.peers_[0].has_pending,"fence_response_pending");if(expire){f.poll(3001);w.collect();requireProgress(!f.host.classic_.controller_.pending[label].kind&&(f.host.classic_.controller_.retired_labels&(1u<<label)),"actual_request_expired_and_retired");}const auto before=f.media;w.acl(0x25,p.media,mediaPacket(990,14));w.pump(5);requireProgress(f.media==before,"unsafe_response_remains_media_fence");w.accounting();std::puts("CONTROLLER_RESPONSE_FENCE_PASS");}
static void unmatchedFence(){Playing p;const auto label=realRequestLabel(p,0x10);unsigned wrong=(label+1)&15;for(unsigned i=0;i<16&&p.f.host.classic_.controller_.pending[wrong].kind;++i)wrong=(wrong+1)&15;requireProgress(!p.f.host.classic_.controller_.pending[wrong].kind,"unused_response_label");responseFence(p,readResponse(wrong,0x10),p.avrcp,false,wrong);}
static void malformedFence(){Playing p;const auto label=realRequestLabel(p,0x10);auto q=readResponse(label,0x10);q[12]++;responseFence(p,q,p.avrcp,false,label);}
static void expiredFence(){Playing p;const auto label=realRequestLabel(p,0x10);responseFence(p,readResponse(label,0x10),p.avrcp,true,label);}
static void wrongCidFence(){Playing p;const auto label=realRequestLabel(p,0x10);responseFence(p,readResponse(label,0x10),p.signal,false,label);}
static void fragmentedAvctpFence(){Playing p;const auto label=realRequestLabel(p,0x10);Bytes q={uint8_t((label<<4)|6),2,0x11,0x0e,12,0x48,0,0};responseFence(p,q,p.avrcp,false,label);}
static void notificationFence(){Playing p;auto& f=p.f;auto& w=p.wire;const unsigned cap=realRequestLabel(p,0x10);w.acl(0x25,p.avrcp,readResponse(cap,0x10));w.pump(50);int label=-1;for(const auto& x:w.responses)if(x.handle==0x25&&x.cid==0x80&&x.bytes.size()==18&&!(x.bytes[0]&3)&&x.bytes[9]==0x31&&x.bytes[13]==1){unsigned n=x.bytes[0]>>4;if(f.host.classic_.controller_.pending[n].kind){label=int(n);break;}}requireProgress(label>=0,"real_notification_request_pending");Bytes q={uint8_t((label<<4)|2),0x11,0x0e,15,0x48,0,0,0x19,0x58,0x31,0,0,2,1,1};responseFence(p,q,p.avrcp,false,unsigned(label));}
static void keyFence(){Playing p;auto& f=p.f;auto& w=p.wire;uint32_t request=0;requireProgress(f.host.avrcpCommand(AvrcpCommand::Play,f.host.avrcpStatus().generation,request)==Status::Ok&&request,"real_key_request_queued");w.pump(20);Bytes response;int label=-1;for(const auto& x:w.responses)if(x.handle==0x25&&x.cid==0x80&&x.bytes.size()==8&&!(x.bytes[0]&3)&&x.bytes[5]==0x7c){unsigned n=x.bytes[0]>>4;if(f.host.classic_.controller_.pending[n].kind){response=x.bytes;label=int(n);break;}}requireProgress(label>=0,"real_key_request_pending");response[0]|=2;response[3]=9;responseFence(p,response,p.avrcp,false,unsigned(label));}
int controller_case_main(int argc,char** argv){
 const std::string selector=argc>1?argv[1]:"avrcp_plain_plain";
 if(selector=="ct_unmatched_fence"){unmatchedFence();return 0;}if(selector=="ct_malformed_fence"){malformedFence();return 0;}if(selector=="ct_expired_fence"){expiredFence();return 0;}if(selector=="ct_wrong_cid_fence"){wrongCidFence();return 0;}if(selector=="ct_fragmented_avctp_fence"){fragmentedAvctpFence();return 0;}if(selector=="ct_notification_fence"){notificationFence();return 0;}if(selector=="ct_key_fence"){keyFence();return 0;}
 for(uint8_t pdu:{uint8_t(0x10),uint8_t(0x30),uint8_t(0x20)})for(bool fragment:{false,true}){const std::string name=std::string(pdu==0x10?"ct_caps":pdu==0x30?"ct_status":"ct_metadata")+(fragment?"_fragment":"_plain");if(selector==name){responseProgress(pdu,fragment);return 0;}}
 if(selector=="security_control"){barriers(true);return 0;}if(selector=="lifecycle_control"){barriers(false);return 0;}
 bool selected=false;for(bool sdp:{false,true})for(bool control_fragment:{false,true})for(bool media_fragment:{false,true}){
  const std::string name=std::string(sdp?"sdp":"avrcp")+"_"+(control_fragment?"fragment":"plain")+"_"+(media_fragment?"fragment":"plain");
  if(selector==name||selector==name+"_ble"){progress(sdp,control_fragment,media_fragment,selector==name+"_ble");selected=true;}
 }
 requireProgress(selected,"known_selector");return 0;
}

#ifndef GBR_CLASSIC_CT_BATCH
int main(int argc,char** argv){return controller_case_main(argc,argv);}
#endif
