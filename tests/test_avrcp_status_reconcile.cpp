// Real production ClassicProfiles/control queue. The quiet peer intentionally
// omits CHANGED, matching observed stale-notification behavior without claiming
// the actual phone's missing-wire-notification cause has been identified.
#include "GigaAvrcp.h"
#include "owned/ClassicProfiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <cstdlib>
#include <new>
static bool watch_allocations=false;static unsigned poll_allocations=0;
void* operator new(size_t n){if(watch_allocations)++poll_allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
using namespace giga_bredr;
using giga_bredr::detail::ClassicProfiles;
struct Fixture {
 bth_control_tx_t q{};bth_hci_flow_t flow{};ClassicProfiles p;uint32_t owner=1;
 static Status configure(const CodecConfig&,void*){return Status::Ok;}
 static void volume(uint8_t,void*){}
 static void metadata(uint8_t,const char*,size_t,void*){}
 Fixture(){bth_control_tx_init(&q);assert(bth_control_tx_attach(&q,0,0x25,owner));
  bth_hci_flow_init(&flow);assert(bth_hci_flow_set_buffers(&flow,1021,256,0,0));assert(bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC));
  giga_bredr::detail::RadioCallbacks cb;cb.configure=configure;cb.volume=volume;cb.metadata=metadata;
  assert(p.begin(AllCodecs,cb,q)==Status::Ok);p.reset(owner);assert(p.channelOpened(0x17,0x43,0x83,672));}
 std::vector<uint8_t> pop(){uint8_t b[1030];size_t n=0;uint32_t owners[2]={owner,0};assert(bth_control_tx_next(&q,owners,&flow,b,sizeof b,&n)==BTH_OK);assert(n>=9&&bth_read_le16(b+7)==0x83);return {b+9,b+n};}
 void vendor(const std::vector<uint8_t>& req,uint8_t pdu,std::initializer_list<uint8_t> data,uint32_t now=100,uint8_t type=12,uint8_t packet=0){std::vector<uint8_t> r={uint8_t((req[0]&0xf0)|2),0x11,0x0e,type,0x48,0,0,0x19,0x58,pdu,packet,0,uint8_t(data.size())};r.insert(r.end(),data);assert(p.receive(0x43,r.data(),r.size(),true,now));}
};
static std::vector<uint8_t> prime(Fixture& f){
 f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,2,1,2},2);
 f.p.poll(true,3);auto status=f.pop();f.vendor(status,0x30,{0,0,0x20,0,0,0,0,28,1},4);
 f.p.poll(true,5);auto metadata=f.pop();f.vendor(metadata,0x20,{0},6);
 f.p.poll(true,7);auto playback=f.pop();assert(playback[9]==0x31&&playback[13]==1);f.vendor(playback,0x31,{1,1},8,15);
 f.p.poll(true,9);status=f.pop();f.vendor(status,0x30,{0,0,0x20,0,0,0,0,28,1},10);
 f.p.poll(true,11);auto track=f.pop();assert(track[9]==0x31&&track[13]==2);f.vendor(track,0x31,{2,0,0,0,0,0,0,0,1},12,15);return playback;
}
static void accepted(Fixture& f,uint32_t now=100,AvrcpCommand command=AvrcpCommand::Pause){
 uint32_t id=0;assert(f.p.avrcpCommand(command,f.p.avrcpStatus().generation,true,now-2,id)==Status::Ok);f.p.poll(true,now-1);auto down=f.pop(),up=f.pop();assert(down[6]==uint8_t(command)&&up[6]==uint8_t(uint8_t(command)|0x80));
 for(auto r:{down,up}){r[0]|=2;r[3]=9;assert(f.p.receive(0x43,r.data(),r.size(),true,now));}
 auto s=f.p.avrcpStatus();assert(s.command_state==AvrcpCommandState::Completed&&s.press_response==9&&s.release_response==9);
}
static void status_reply(Fixture& f,const std::vector<uint8_t>& request,uint8_t state,uint32_t now){assert(request[9]==0x30);f.vendor(request,0x30,{0,0,0x20,0,0,0,0,28,state},now);}
static void baseline_repro(){
 Fixture f;prime(f);accepted(f);assert(f.p.avrcpStatus().playback_status==1);
 for(uint32_t now=101;now<15101;now+=250){f.p.poll(true,now);assert(f.q.peers[0].count==0);}
 assert(f.p.avrcpStatus().playback_status==1&&f.p.avrcpStatus().song_position_ms==28);puts("BASELINE_REPRO Completed09/09;quiet target leaves received playback1/position28 stale for15000ms");
}
static void quiet_target_reconciles_only_after_real_status(){
 Fixture f;prime(f);accepted(f);assert(f.p.avrcpStatus().playback_status==1);
 watch_allocations=true;f.p.poll(true,101);watch_allocations=false;assert(poll_allocations==0);auto first=f.pop();status_reply(f,first,1,102); // target still dispatching callback
 f.p.poll(true,350);assert(f.q.peers[0].count==0);f.p.poll(true,351);auto second=f.pop();status_reply(f,second,2,352);assert(f.p.avrcpStatus().playback_status==2);unsigned reads=2;
 for(uint32_t now=601;now<3100;now+=250){f.p.poll(true,now);if(f.q.peers[0].count){auto request=f.pop();status_reply(f,request,2,now+1);++reads;}}
 assert(reads==12);f.p.poll(true,15101);assert(f.q.peers[0].count==0&&f.p.avrcpStatus().playback_status==2);
}
static void still_playing_is_not_predicted_paused(){
 Fixture f;prime(f);accepted(f);
 for(uint32_t now=101;now<3100;now+=250){f.p.poll(true,now);auto request=f.pop();status_reply(f,request,1,now+1);assert(f.p.avrcpStatus().playback_status==1);}
 f.p.poll(true,15101);assert(f.q.peers[0].count==0&&f.p.avrcpStatus().playback_status==1);
}
static void changed_authority_wins_and_rearms(){
 Fixture f;auto playback=prime(f);accepted(f);f.p.poll(true,101);auto old=f.pop();f.vendor(playback,0x31,{1,2},102,13);assert(f.p.avrcpStatus().playback_status==2);status_reply(f,old,1,103);assert(f.p.avrcpStatus().playback_status==2);
 f.p.poll(true,104);auto current=f.pop();status_reply(f,current,2,105);f.p.poll(true,106);auto rearm=f.pop();assert(rearm[9]==0x31&&rearm[13]==1);f.vendor(rearm,0x31,{1,2},107,15);assert(f.p.avrcpStatus().malformed_events==0);
}
static void pre_key_status_is_obsolete(){
 Fixture f;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,0},2);f.p.poll(true,3);auto old=f.pop();accepted(f);status_reply(f,old,1,101);assert(f.p.avrcpStatus().playback_status==0xff);
 f.p.poll(true,102);auto current=f.pop();status_reply(f,current,2,103);assert(f.p.avrcpStatus().playback_status==2);
}
static void timeout_late_read_cannot_publish_or_reuse_label(){
 Fixture f;prime(f);accepted(f);f.p.poll(true,101);auto missing=f.pop();f.p.poll(true,3101);assert(f.q.peers[0].count==0);status_reply(f,missing,2,3102);assert(f.p.avrcpStatus().playback_status==1);
 accepted(f,4000);f.p.poll(true,4001);auto current=f.pop();assert(current[0]!=missing[0]);status_reply(f,current,2,4002);assert(f.p.avrcpStatus().playback_status==2);
}
static void backpressure_security_wrap_and_disconnect(){
 Fixture f;prime(f);accepted(f);bth_control_reservation_t held[BTH_CONTROL_PEER_LIMIT];for(auto& r:held)assert(bth_control_tx_reserve(&f.q,0,&r)==BTH_OK);f.p.poll(true,101);assert(f.q.peers[0].count==0);for(auto& r:held)assert(bth_control_tx_cancel(&f.q,r.token)==BTH_OK);
 f.p.poll(false,102);assert(f.q.peers[0].count==0);f.p.poll(true,103,false);assert(f.q.peers[0].count==0);f.p.poll(true,104);auto query=f.pop();status_reply(f,query,2,105);f.p.channelClosed(0x43);f.p.poll(true,355);assert(f.q.peers[0].count==0&&!f.p.avrcpStatus().connected&&f.p.avrcpStatus().playback_status==0xff);
 Fixture wrap;prime(wrap);accepted(wrap,0xfffffff0u);wrap.p.poll(true,0xfffffff1u);query=wrap.pop();status_reply(wrap,query,1,0xfffffff2u);wrap.p.poll(true,0xebu);query=wrap.pop();status_reply(wrap,query,2,0xecu);assert(wrap.p.avrcpStatus().playback_status==2);
}
static void newer_key_and_reconnect_retire_prior_schedule(){
 Fixture f;prime(f);accepted(f);f.p.poll(true,101);auto old=f.pop();uint32_t id=0;assert(f.p.avrcpCommand(AvrcpCommand::Play,f.p.avrcpStatus().generation,true,102,id)==Status::Ok);f.p.poll(true,103);auto d=f.pop(),u=f.pop();status_reply(f,old,2,104);assert(f.p.avrcpStatus().playback_status==1);f.p.poll(true,355);assert(f.q.peers[0].count==0);
 for(auto r:{d,u}){r[0]|=2;r[3]=9;assert(f.p.receive(0x43,r.data(),r.size(),true,356));}f.p.poll(true,357);auto latest=f.pop();status_reply(f,latest,1,358);assert(f.p.avrcpStatus().playback_status==1);
 auto generation=f.p.avrcpStatus().generation;f.p.channelClosed(0x43);assert(f.p.channelOpened(0x17,0x43,0x83,672));assert(f.p.avrcpStatus().generation!=generation);f.p.poll(true,700);auto caps=f.pop();assert(caps[9]==0x10);assert(f.p.avrcpStatus().playback_status==0xff);
}
static void next_previous_have_one_bounded_copied_metadata_refresh(){
 for(auto command:{AvrcpCommand::Next,AvrcpCommand::Previous}){
  Fixture f;prime(f);accepted(f,100,command);assert(f.p.avrcpMetadata().valid_attributes==0);
  for(uint32_t now=101;now<3100;now+=250){f.p.poll(true,now);auto query=f.pop();status_reply(f,query,1,now+1);}
  f.p.poll(true,3100);auto metadata=f.pop();assert(metadata[9]==0x20);f.vendor(metadata,0x20,{1,0,0,0,1,0,106,0,3,'N','e','w'},3101);auto saved=f.p.avrcpMetadata();assert(saved.generation==f.p.avrcpStatus().generation&&saved.valid_attributes==1&&!strcmp(saved.title,"New"));
  f.p.poll(true,20000);assert(f.q.peers[0].count==0);assert(!strcmp(saved.title,"New"));
 }
}
static void newer_key_discards_unissued_metadata_refresh(){
 Fixture f;prime(f);accepted(f,100,AvrcpCommand::Next);f.p.poll(true,101);auto status=f.pop();status_reply(f,status,1,102);accepted(f,200,AvrcpCommand::Pause);
 for(uint32_t now=201;now<3200;now+=250){f.p.poll(true,now);auto query=f.pop();status_reply(f,query,2,now+1);}f.p.poll(true,7000);assert(f.q.peers[0].count==0&&f.p.avrcpMetadata().valid_attributes==0);
}
static void in_flight_prior_metadata_cannot_repopulate_after_newer_track_key(){
 Fixture f;prime(f);accepted(f,100,AvrcpCommand::Next);
 for(uint32_t now=101;now<3100;now+=250){f.p.poll(true,now);auto query=f.pop();status_reply(f,query,1,now+1);}f.p.poll(true,3100);auto old=f.pop();assert(old[9]==0x20);
 accepted(f,3200,AvrcpCommand::Previous);f.vendor(old,0x20,{1,0,0,0,1,0,106,0,3,'O','l','d'},3201);assert(f.p.avrcpMetadata().valid_attributes==0);
 for(uint32_t now=3202;now<6200;now+=250){f.p.poll(true,now);auto query=f.pop();status_reply(f,query,1,now+1);}f.p.poll(true,6200);auto current=f.pop();assert(current[9]==0x20);f.vendor(current,0x20,{1,0,0,0,1,0,106,0,3,'N','e','w'},6201);assert(!strcmp(f.p.avrcpMetadata().title,"New"));
}
static void stale_metadata_reply_and_blocked_opportunity_do_not_invent_success(){
 Fixture f;prime(f);accepted(f,100,AvrcpCommand::Next);
 for(uint32_t now=101;now<3100;now+=250){f.p.poll(true,now);auto query=f.pop();status_reply(f,query,1,now+1);}f.p.poll(true,3100);auto metadata=f.pop();f.vendor(metadata,0x20,{1,0,0,0,1,0,106,0,3,'O','l','d'},3101);assert(!strcmp(f.p.avrcpMetadata().title,"Old"));f.p.poll(true,10000);assert(f.q.peers[0].count==0);
 Fixture blocked;prime(blocked);accepted(blocked,100,AvrcpCommand::Next);bth_control_reservation_t held[BTH_CONTROL_PEER_LIMIT];for(auto& r:held)assert(bth_control_tx_reserve(&blocked.q,0,&r)==BTH_OK);blocked.p.poll(true,3100);blocked.p.poll(true,6100);for(auto& r:held)assert(bth_control_tx_cancel(&blocked.q,r.token)==BTH_OK);blocked.p.poll(true,6101);assert(blocked.q.peers[0].count==0&&blocked.p.avrcpMetadata().valid_attributes==0);
}
int main(int argc,char** argv){
 if(argc==2&&!strcmp(argv[1],"baseline_repro")){baseline_repro();return 0;}
 quiet_target_reconciles_only_after_real_status();still_playing_is_not_predicted_paused();changed_authority_wins_and_rearms();pre_key_status_is_obsolete();timeout_late_read_cannot_publish_or_reuse_label();backpressure_security_wrap_and_disconnect();newer_key_and_reconnect_retire_prior_schedule();next_previous_have_one_bounded_copied_metadata_refresh();newer_key_discards_unissued_metadata_refresh();in_flight_prior_metadata_cannot_repopulate_after_newer_track_key();stale_metadata_reply_and_blocked_opportunity_do_not_invent_success();puts("PASS eleven real status/notification/metadata/authority/timeout/wrap/allocation regressions");
}
