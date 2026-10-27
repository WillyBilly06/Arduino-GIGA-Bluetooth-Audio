// Breaks caught: missing paired key release, stale-owner admission, replay after
// cancellation/timeout, or stale metadata becoming current after track changes.
#if defined(__has_include) && __has_include("GigaAvrcp.h")
#include "GigaAvrcp.h"
#include "owned/ClassicProfiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
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
 void response(const std::vector<uint8_t>& key,uint8_t type=9){auto r=key;r[0]|=2;r[3]=type;assert(p.receive(0x43,r.data(),r.size(),true,100));}
 void vendor(const std::vector<uint8_t>& req,uint8_t pdu,std::initializer_list<uint8_t> data,uint32_t now=100,uint8_t type=12,uint8_t packet=0){std::vector<uint8_t> r={uint8_t((req[0]&0xf0)|2),0x11,0x0e,type,0x48,0,0,0x19,0x58,pdu,packet,0,uint8_t(data.size())};r.insert(r.end(),data);assert(p.receive(0x43,r.data(),r.size(),true,now));}
};
static void typed_taps_are_owned_paired_and_acknowledged(){
 const AvrcpCommand commands[]={AvrcpCommand::Play,AvrcpCommand::Pause,AvrcpCommand::Stop,AvrcpCommand::Next,AvrcpCommand::Previous};
 const uint8_t ops[]={0x44,0x46,0x45,0x4b,0x4c};
 for(unsigned i=0;i<5;++i){Fixture f;auto gen=f.p.avrcpStatus().generation;assert(gen);uint32_t id=0;
  assert(f.p.avrcpCommand(commands[i],gen,true,10,id)==Status::Ok&&id);assert(f.q.peers[0].count==0);
  uint32_t untouched=77;assert(f.p.avrcpCommand(commands[i],gen,true,11,untouched)==Status::Busy&&untouched==77);
  f.p.poll(true,12);auto down=f.pop(),up=f.pop();assert(down.size()==8&&up.size()==8);
  assert(down[1]==0x11&&down[2]==0x0e&&down[3]==0&&down[4]==0x48&&down[5]==0x7c&&down[6]==ops[i]&&down[7]==0);
  assert(up[6]==uint8_t(ops[i]|0x80)&&up[0]!=down[0]);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Pending);
  f.response(up);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Pending);
  f.response(down);auto s=f.p.avrcpStatus();assert(s.command_state==AvrcpCommandState::Completed&&s.command_id==id&&s.command_generation==gen);
  assert(s.press_response==9&&s.release_response==9);
 }
}
static void cancellation_never_leaves_a_committed_press_without_release(){
 Fixture f;uint32_t gen=f.p.avrcpStatus().generation,id=0;
 assert(f.p.avrcpCommand(AvrcpCommand::Play,gen,true,1,id)==Status::Ok);assert(f.p.avrcpCancel(gen,id)==Status::Ok);
 assert(f.p.avrcpCancel(gen,id)==Status::Ok);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Cancelled);
 f.p.poll(true,2);assert(f.pop().size()>8); // Ordinary capabilities work, no key pair.
 uint32_t next=0;assert(f.p.avrcpCommand(AvrcpCommand::Pause,gen,true,3,next)==Status::Ok&&next!=id);
 f.p.poll(true,4);auto down=f.pop(),up=f.pop();assert(down[6]==0x46&&up[6]==0xc6);
 assert(f.p.avrcpCancel(gen,next)==Status::Ok);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Pending);
 f.response(down);f.response(up);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Cancelled);
}
static void backpressure_retains_pair_and_timeout_does_not_replay(){
 Fixture f;uint32_t gen=f.p.avrcpStatus().generation,id=0;
 bth_control_reservation_t held[2];for(auto& r:held)assert(bth_control_tx_reserve(&f.q,0,&r)==BTH_OK);
 assert(f.p.avrcpCommand(AvrcpCommand::Next,gen,true,0xfffffff0u,id)==Status::Ok);
 f.p.poll(true,0xfffffff1u);assert(f.q.peers[0].count==0&&f.p.avrcpStatus().command_state==AvrcpCommandState::Queued);
 for(auto& r:held)assert(bth_control_tx_cancel(&f.q,r.token)==BTH_OK);
 f.p.poll(true,0xfffffff2u);auto down=f.pop(),up=f.pop();assert(down[6]==0x4b&&up[6]==0xcb);
 f.p.poll(true,0x00000bb0u,false);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::TimedOut);
 f.response(down);f.response(up);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::TimedOut);
 uint32_t second=0;assert(f.p.avrcpCommand(AvrcpCommand::Stop,gen,true,0xbb1,second)==Status::Ok);
 f.p.poll(true,0xbb2);auto d2=f.pop(),u2=f.pop();assert(d2[0]!=down[0]&&d2[0]!=up[0]&&u2[0]!=down[0]&&u2[0]!=up[0]);
 f.response(d2);f.response(u2);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Completed);
}
static void generation_reconnect_and_security_cannot_act_on_old_owner(){
 Fixture f;uint32_t old=f.p.avrcpStatus().generation,id=55;
 assert(f.p.avrcpCommand(AvrcpCommand::Play,old,false,1,id)==Status::Busy&&id==55);
 assert(f.p.avrcpCommand(AvrcpCommand::Play,old,true,2,id)==Status::Ok);f.p.channelClosed(0x43);
 assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Disconnected&&!f.p.avrcpStatus().generation);
 assert(f.p.channelOpened(0x17,0x43,0x83,672));auto current=f.p.avrcpStatus().generation;assert(current&&current!=old);
 assert(f.p.avrcpCommand(AvrcpCommand::Play,old,true,3,id)==Status::InvalidArgument);
 assert(f.p.avrcpCancel(old,id)==Status::InvalidArgument);
}
static void metadata_status_snapshots_clear_and_retain_copied_values(){
 Fixture f;auto gen=f.p.avrcpStatus().generation;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,0});
 f.p.poll(true,2);auto status=f.pop();f.vendor(status,0x30,{0,0,0x13,0x88,0,0,0,100,1});
 assert(f.p.avrcpStatus().playback_status==1&&f.p.avrcpStatus().song_length_ms==5000);
 f.p.poll(true,3);auto meta=f.pop();f.vendor(meta,0x20,{2,0,0,0,1,0,106,0,5,'T','i','t','l','e',0,0,0,2,0,106,0,6,'A','r','t','i','s','t'});
 auto saved=f.p.avrcpMetadata();assert(saved.generation==gen&&(saved.valid_attributes&3)==3);
 assert(!strcmp(saved.title,"Title")&&!strcmp(saved.artist,"Artist"));f.p.channelClosed(0x43);
 assert(!f.p.avrcpMetadata().generation&&!f.p.avrcpMetadata().valid_attributes);assert(!strcmp(saved.title,"Title"));
}

static void rejected_release_and_exact_zero_deadline_are_not_success(){
 Fixture f;uint32_t gen=f.p.avrcpStatus().generation,id=0;
 assert(f.p.avrcpCommand(AvrcpCommand::Previous,gen,true,1,id)==Status::Ok);f.p.poll(true,2);auto down=f.pop(),up=f.pop();
 f.response(down);f.response(up,8);assert(f.p.avrcpStatus().command_state==AvrcpCommandState::Rejected);assert(f.p.avrcpStatus().rejected_commands==1);
 Fixture z;gen=z.p.avrcpStatus().generation;assert(z.p.avrcpCommand(AvrcpCommand::Play,gen,true,0xfffff448u,id)==Status::Ok); // +3000 ==0.
 z.p.poll(true,0,false);assert(z.p.avrcpStatus().command_state==AvrcpCommandState::TimedOut&&z.q.peers[0].count==0);
}
static void a_previous_track_response_cannot_repopulate_current_metadata(){
 Fixture f;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,1,2});
 f.p.poll(true,2);auto status=f.pop();f.vendor(status,0x30,{0,0,0,10,0,0,0,1,1});
 f.p.poll(true,3);auto old=f.pop();
 f.p.poll(true,4);auto track=f.pop();assert(track[9]==0x31&&track[13]==2);
 f.vendor(track,0x31,{2,0,0,0,0,0,0,0,1},5,15);f.vendor(track,0x31,{2,0,0,0,0,0,0,0,2},6,13);
 f.vendor(old,0x20,{1,0,0,0,1,0,106,0,3,'O','l','d'});assert(!f.p.avrcpMetadata().valid_attributes);
 f.p.poll(true,7);status=f.pop();f.vendor(status,0x30,{0,0,0,20,0,0,0,2,1});
 f.p.poll(true,8);auto current=f.pop();f.vendor(current,0x20,{1,0,0,0,1,0,106,0,3,'N','e','w'});
 assert(f.p.avrcpMetadata().valid_attributes==1&&!strcmp(f.p.avrcpMetadata().title,"New"));
}
static void late_key_responses_expire_before_receive_without_poll(){
 Fixture f;uint32_t id=0;assert(f.p.avrcpCommand(AvrcpCommand::Play,f.p.avrcpStatus().generation,true,1,id)==Status::Ok);
 f.p.poll(true,2);auto down=f.pop(),up=f.pop();
 for(auto key:{down,up}){key[0]|=2;key[3]=9;assert(f.p.receive(0x43,key.data(),key.size(),true,3002));}
 assert(f.p.avrcpStatus().command_state==AvrcpCommandState::TimedOut);
}
static void a_previous_status_response_cannot_override_newer_notification(){
 Fixture f;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,1,1});
 f.p.poll(true,2);auto old=f.pop();f.p.poll(true,3);auto meta=f.pop();(void)meta;
 f.p.poll(true,4);auto playback=f.pop();assert(playback[9]==0x31&&playback[13]==1);
 f.vendor(playback,0x31,{1,2},5,15);assert(f.p.avrcpStatus().playback_status==2);
 f.vendor(old,0x30,{0,0,0,10,0,0,0,1,1},6);assert(f.p.avrcpStatus().playback_status==2);
}
static void metadata_abort_acceptance_retires_without_malformed_event(){
 Fixture f;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,0},1);
 f.p.poll(true,2);auto status=f.pop();f.vendor(status,0x30,{0,0,0,10,0,0,0,1,1},2);
 f.p.poll(true,3);auto meta=f.pop();f.vendor(meta,0x20,{1},4,12,1);
 f.p.poll(true,3004);auto abort=f.pop();assert(abort[9]==0x41&&abort[13]==0x20);
 const auto malformed=f.p.avrcpStatus().malformed_events;f.vendor(abort,0x41,{},3005,9);
 assert(f.p.avrcpStatus().malformed_events==malformed);
}
static void status_rejection_does_not_cancel_unrelated_metadata_continuation(){
 Fixture f;f.p.poll(true,1);auto caps=f.pop();f.vendor(caps,0x10,{3,0},1);
 f.p.poll(true,2);auto status=f.pop();f.p.poll(true,3);auto meta=f.pop();
 f.vendor(meta,0x20,{1,0,0,0,1,0,106,0,5,'T'},4,12,1);
 f.vendor(status,0x30,{3},5,10);f.p.poll(true,6);auto continuation=f.pop();assert(continuation[9]==0x40&&continuation[13]==0x20);
 f.vendor(continuation,0x20,{'i','t','l','e'},7,12,3);
 assert(f.p.avrcpMetadata().valid_attributes==1&&!strcmp(f.p.avrcpMetadata().title,"Title"));
}
int main(int argc,char** argv){
 if(argc==2){
  if(!strcmp(argv[1],"late_ack")){late_key_responses_expire_before_receive_without_poll();return 0;}
  if(!strcmp(argv[1],"stale_status")){a_previous_status_response_cannot_override_newer_notification();return 0;}
  if(!strcmp(argv[1],"metadata_abort")){metadata_abort_acceptance_retires_without_malformed_event();return 0;}
  if(!strcmp(argv[1],"metadata_overlap")){status_rejection_does_not_cancel_unrelated_metadata_continuation();return 0;}
  return 2;
 }
 metadata_abort_acceptance_retires_without_malformed_event();status_rejection_does_not_cancel_unrelated_metadata_continuation();a_previous_status_response_cannot_override_newer_notification();late_key_responses_expire_before_receive_without_poll();rejected_release_and_exact_zero_deadline_are_not_success();a_previous_track_response_cannot_repopulate_current_metadata();typed_taps_are_owned_paired_and_acknowledged();cancellation_never_leaves_a_committed_press_without_release();backpressure_retains_pair_and_timeout_does_not_replay();generation_reconnect_and_security_cannot_act_on_old_owner();metadata_status_snapshots_clear_and_retain_copied_values();puts("PASS typed AVRCP ownership, paired keys, cancellation, wrap, timeout and copied snapshots");}
#else
#include <stdio.h>
int main(){puts("MISSING_TYPED_AVRCP_IMPLEMENTATION");return 1;}
#endif
