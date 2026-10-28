// Exactly two source-review counterexamples. Compile only here; Root runs.
// Real frozenr5 host/profiles/core and original progress helpers, fake wire only.
#include "support/classic_review_seeded.h"
static bool reviewExpect(bool ok,const char* stage){
 if(!ok){std::printf("REVIEW_EXPECTATION_FAIL stage=%s\n",stage);std::fflush(stdout);}return ok;
}
static int availableGrants(const Playing& p,unsigned sent_at_start,unsigned returned_at_start,unsigned outstanding_at_start){
 return int(OwnedHost::RxWindow)-int(outstanding_at_start)+int(p.wire.returned_classic-returned_at_start)-int(p.wire.in_classic-sent_at_start);
}
static bool fourPieceHeader(){
 const auto expected=normalResponses(false);Playing p;fill_safe_pressure(p);auto& f=p.f;auto& w=p.wire;
 const auto request=p.requests(false).back();pendingView(p,request);
 const auto& peer=f.host.peers_[0];const int borrowed=peer.borrowed_slot,parked=f.host.classic_parked_slot_;
 bool borrowed_owned=false,parked_owned=false;
 if(borrowed>=0&&borrowed<int(OwnedHost::RxSlots)){const auto& packet=f.host.packets_[borrowed];const auto& lease=f.host.rx_.slots[borrowed];borrowed_owned=packet.used&&packet.charged&&packet.lease.slot==unsigned(borrowed)&&lease.busy&&lease.serial==packet.lease.serial&&lease.owner==peer.owner&&lease.handle==peer.handle;}
 if(parked>=0&&parked<int(OwnedHost::RxSlots))parked_owned=borrowed<0&&f.host.classic_parked_owner_==peer.owner&&peer.pending.payload.data==f.host.packets_[parked].bytes&&!f.host.packets_[parked].used&&!f.host.rx_.slots[parked].busy;
 // The original request is still single-ACL-origin; early owned copying is valid.
 if(!reviewExpect(!peer.pending.reassembled&&(borrowed_owned||parked_owned),"borrowed_origin_pending_storage_owned"))return false;
 const uint32_t owner=f.host.peers_[0].owner;const unsigned before=f.media;
 const unsigned sent=w.in_classic,returned=w.returned_classic,outstanding=f.host.rx_.outstanding;
 const Bytes media=mediaPacket(970,14);const uint8_t header[]={14,0,uint8_t(p.media),uint8_t(p.media>>8)};
 for(unsigned i=0;i<4;++i){
  const int grants=availableGrants(p,sent,returned,outstanding);
  std::printf("FOUR_HEADER_BEFORE part=%u grants=%d rx_outstanding=%u parked=%d returned=%u sent=%u\n",i,grants,unsigned(f.host.rx_.outstanding),f.host.classic_parked_slot_,w.returned_classic-returned,w.in_classic-sent);std::fflush(stdout);
  // Never inject a packet without a genuine advertised/returned host grant.
  if(!reviewExpect(grants>0&&f.host.rx_.outstanding<OwnedHost::RxWindow,"next_header_piece_has_real_grant"))return false;
  ++w.in_classic;f.rawAcl(0x25,i?1:2,Bytes{header[i]});w.pump(2);w.accounting();pendingView(p,request);
 }
 if(!reviewExpect(availableGrants(p,sent,returned,outstanding)>0&&f.host.rx_.outstanding<OwnedHost::RxWindow,"payload_has_real_grant"))return false;
 ++w.in_classic;f.rawAcl(0x25,1,media);w.pump(3);w.accounting();pendingView(p,request);
 if(!reviewExpect(f.media==before+1&&f.last_media==media&&f.host.peers_[0].owner==owner&&f.playing,"four_piece_header_delivers_exact_media"))return false;
 if(!reviewExpect(w.returned_classic-returned>=w.in_classic-sent,"fragment_credits_retired"))return false;
 w.release();std::vector<Response> actual;
 for(const auto& r:w.responses)if(r.handle==0x25&&((r.cid==0x80&&!r.bytes.empty()&&(r.bytes[0]&2))||(r.cid==0x81&&r.bytes.size()>=3&&r.bytes[0]==3&&r.bytes[1]==0x12&&r.bytes[2]==0x34)))actual.push_back(r);
 if(!reviewExpect(actual.size()==expected.size(),"reply_count_after_header_progress"))return false;
 for(size_t i=0;i<actual.size();++i)if(!reviewExpect(actual[i].cid==expected[i].cid&&actual[i].bytes==expected[i].bytes,"reply_FIFO_after_header_progress"))return false;
 return reviewExpect(!f.host.rx_.outstanding&&!f.host.peers_[0].has_pending&&f.host.classic_parked_slot_<0&&!f.host.radioStats().transport_errors&&!f.host.radioStats().dropped_packets,"four_piece_final_ownership");
}
static bool startPartial(Playing& p,uint32_t& admission){
 auto& f=p.f;auto& w=p.wire;const Bytes media=mediaPacket(971,14);
 if(!reviewExpect(f.host.rx_.outstanding<OwnedHost::RxWindow,"partial_first_has_host_grant"))return false;
 ++w.in_classic;f.rawAcl(0x25,2,Bytes{14,0,uint8_t(p.media),uint8_t(p.media>>8),media[0]});w.pump(2);w.accounting();
 if(!reviewExpect(f.host.bypass_media_active_&&f.host.peers_[0].assembly.active&&f.host.classic_parked_slot_>=0,"active_bypass_partial_setup"))return false;
 admission=f.host.peers_[0].assembly_deadline-2000u;return true;
}
static bool zeroAssemblyDeadline(){
 uint32_t setup_delta=0;
 {
  // Calibrate only normal setup/poll advancement; do not jump an active clock.
  const uint32_t seed=10;Playing measured(false,seed);fill_safe_pressure(measured);uint32_t admission=0;
  if(!startPartial(measured,admission))return false;setup_delta=admission-seed;measured.f.host.end();
 }
 const uint32_t seed=uint32_t(0u-2000u-setup_delta);Playing p(false,seed);fill_safe_pressure(p);uint32_t admission=0;
 if(!startPartial(p,admission))return false;auto& f=p.f;auto& w=p.wire;const uint32_t deadline=f.host.peers_[0].assembly_deadline;
 std::printf("ZERO_ASSEMBLY_SETUP seed=%lu setup_delta=%lu admitted=%lu deadline=%lu now=%lu pending_deadline=%lu\n",(unsigned long)seed,(unsigned long)setup_delta,(unsigned long)admission,(unsigned long)deadline,(unsigned long)f.now,(unsigned long)f.host.peers_[0].pending_deadline);std::fflush(stdout);
 if(!reviewExpect(deadline==0&&admission==uint32_t(0u-2000u),"naturally_scheduled_zero_deadline"))return false;
 if(!reviewExpect(int32_t(f.now-f.host.peers_[0].pending_deadline)<0,"older_control_deadline_not_due"))return false;
 unsigned ticks=0;while(int32_t(f.now-deadline)<0&&ticks++<2100)w.pump();w.pump(1);
 std::printf("ZERO_ASSEMBLY_AT_EXPIRY now=%lu elapsed_ms=%lu pending_deadline=%lu disconnecting=%u assembly_active=%u rx_outstanding=%u\n",(unsigned long)f.now,(unsigned long)uint32_t(f.now-admission),(unsigned long)f.host.peers_[0].pending_deadline,unsigned(f.host.links_.classic.disconnecting),unsigned(f.host.peers_[0].assembly.active),unsigned(f.host.rx_.outstanding));std::fflush(stdout);
 if(!reviewExpect(uint32_t(f.now-admission)>=2000&&uint32_t(f.now-admission)<=2002&&int32_t(f.now-f.host.peers_[0].pending_deadline)<0,"exact_two_second_not_five_second_boundary"))return false;
 return reviewExpect(f.host.links_.classic.disconnecting&&!f.host.radioStats().transport_errors,"active_assembly_expired_at_two_seconds");
}
int review_case_main(int argc,char** argv){
 if(argc!=2)return 3;const std::string selected=argv[1];bool ok=false;
 if(selected=="four_piece_header")ok=fourPieceHeader();else if(selected=="zero_assembly_deadline")ok=zeroAssemblyDeadline();else return 3;
 std::printf("REVIEW_COUNTEREXAMPLE name=%s result=%s\n",selected.c_str(),ok?"PASS":"FAIL");return ok?0:2;
}

#ifndef GBR_CLASSIC_REVIEW_BATCH
int main(int argc,char** argv){return review_case_main(argc,argv);}
#endif
