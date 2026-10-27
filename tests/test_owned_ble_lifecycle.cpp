#define private public
#include "owned/OwnedHost.h"
#undef private
#define main old_fixture_main
#include "test_owned_host.cpp"
#undef main

static Bytes mediaPacket(unsigned n){return {0x80,0x60,0,uint8_t(n),1,2,3,4,5,6,7,8,1,0xaa};}
static uint16_t openAudio(Fixture& f){
 f.classicConnect();auto signal=f.channel(0x19,0x81);
 f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();
 f.acl(0x25,signal,{0x20,6,4});f.settle();auto cid=f.channel(0x19,0x82);
 f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing);return cid;
}
static uint16_t startAudio(Fixture& f){f.boot();return openAudio(f);}
static void leEvent(Fixture& f){f.event(0x3e,{1,0,0x26,0,1,0,6,5,4,3,2,1,24,0,0,0,0xf4,1,0});}
static unsigned opcodeCount(const bth_cmdq_t& q,uint16_t op,uint32_t owner){unsigned n=0;for(unsigned i=0;i<8;++i)if(!(q.free_mask&(1u<<i))&&q.slots[i].opcode==op&&q.slots[i].owner==owner)++n;return n;}
static Bytes snapshot(const void* p,size_t n){auto b=static_cast<const uint8_t*>(p);return Bytes(b,b+n);}
static void fill(Fixture& f,unsigned available){uint8_t peer[]={1,2,3,4,5,6};while(bth_cmdq_available(&f.host.commands_)>available)CHECK(bth_cmdq_push_owned(&f.host.commands_,0x040c,peer,sizeof peer,true,0x1234)==BTH_OK);}

static void admission_and_completion(){
 Fixture f;auto cid=startAudio(f);f.leConnect();auto old=f.host.bleStats().generation;f.tx.clear();
 auto classic=snapshot(&f.host.links_.classic,sizeof f.host.links_.classic);auto stats=f.host.radioStats();
 CHECK(f.host.bleDisconnect(0,false)==Status::InvalidArgument);
 CHECK(f.host.bleDisconnect(old+1,false)==Status::InvalidArgument);
 f.ready=false;CHECK(f.host.bleDisconnect(old,false)==Status::Ok);
 CHECK(f.host.bleStats().connected&&f.host.bleStats().generation==old&&!f.host.bleStats().advertising);
 CHECK(f.host.le_advertising_held_&&f.host.links_.le.disconnecting);
 CHECK(snapshot(&f.host.links_.classic,sizeof f.host.links_.classic)==classic&&f.playing&&f.host.radioStats().streaming);
 CHECK(f.host.radioStats().transport_errors==stats.transport_errors);
 auto deadline=f.host.peers_[1].disconnect_deadline;auto q=snapshot(&f.host.commands_,sizeof f.host.commands_);
 CHECK(opcodeCount(f.host.commands_,0x0406,old)==1);
 CHECK(f.host.bleDisconnect(old,false)==Status::Ok&&f.host.peers_[1].disconnect_deadline==deadline);
 CHECK(snapshot(&f.host.commands_,sizeof f.host.commands_)==q);
 CHECK(f.host.bleDisconnect(old,true)==Status::Busy&&f.host.bleResumeAdvertising()==Status::Busy);
 // Real Classic RX/HNC continues even with no ordinary HCI command credit.
 auto received=f.media;f.host.flow_.command_credits=0;f.acl(0x25,cid,mediaPacket(1));f.ready=true;f.poll();
 CHECK(f.media==received+1&&f.playing&&f.host.bleStats().generation==old);
 CHECK(!f.tx.empty()&&Fixture::op(f.tx.front())==0x0c35);f.acknowledge(f.pop());
 CHECK(opcodeCount(f.host.commands_,0x0406,old)==1);
 f.host.flow_.command_credits=1;f.poll();auto disconnect=f.pop();
 CHECK(disconnect==Bytes({1,6,4,3,0x26,0,0x13}));
 CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].owner==old);
 f.status(0x0406);CHECK(f.host.bleStats().generation==old&&f.host.bleStats().connected);
 f.event(5,{0,0x26,0,0x13});CHECK(!f.host.bleStats().connected&&f.host.bleStats().generation==0);
 CHECK(f.playing&&f.host.radioStats().streaming&&snapshot(&f.host.links_.classic,sizeof f.host.links_.classic)==classic);
 f.settle();CHECK(!f.host.bleStats().advertising&&f.host.le_advertising_held_);
 CHECK(f.host.bleDisconnect(old,false)==Status::InvalidArgument);
 CHECK(f.host.bleResumeAdvertising()==Status::Ok&&f.host.bleResumeAdvertising()==Status::Ok);
 CHECK(!f.host.bleStats().advertising);f.settle();CHECK(f.host.bleStats().advertising);
 f.leConnect();auto replacement=f.host.bleStats().generation;CHECK(replacement>old);
 CHECK(f.host.bleDisconnect(old,false)==Status::InvalidArgument&&!f.host.le_advertising_held_);
 CHECK(f.host.bleDisconnect(replacement,true)==Status::Ok);f.settle();f.event(5,{0,0x26,0,0x13});f.settle();
 CHECK(f.host.bleStats().advertising&&f.playing);
 unsigned attached=0,detached=0;for(const auto& m:f.messages){attached+=m.find("Owned LE attached")!=std::string::npos;detached+=m.find("Owned LE detached")!=std::string::npos;}
 CHECK(attached==2&&detached==2);
 puts("OWNER_HCI0406_STATUS_NOT_DETACH_HNC_CLASSIC_MEDIA_HOLD_RESUME_REUSE_PASS");
}
static void transactional_capacity_and_fifo(){
 Fixture f;f.boot();f.classicConnect();f.leConnect();auto old=f.host.bleStats().generation;f.ready=false;fill(f,2);
 auto q=snapshot(&f.host.commands_,sizeof f.host.commands_);auto le=snapshot(&f.host.links_.le,sizeof f.host.links_.le);
 CHECK(f.host.bleDisconnect(old,false)==Status::Busy&&!f.host.le_advertising_held_);
 CHECK(snapshot(&f.host.commands_,sizeof f.host.commands_)==q&&snapshot(&f.host.links_.le,sizeof f.host.links_.le)==le);
 CHECK(!f.host.peers_[1].disconnect_deadline);
 CHECK(bth_cmdq_cancel_owner(&f.host.commands_,0x1234)==6);fill(f,3);
 CHECK(f.host.bleDisconnect(old,false)==Status::Ok&&bth_cmdq_available(&f.host.commands_)==2);
 CHECK(f.host.commands_.count[1]==6);
 for(unsigned i=0;i<5;++i){auto index=f.host.commands_.order[1][(f.host.commands_.read[1]+i)&7];CHECK(f.host.commands_.slots[index].opcode==0x040c);}
 auto last=f.host.commands_.order[1][(f.host.commands_.read[1]+5)&7];CHECK(f.host.commands_.slots[last].opcode==0x0406);
 uint8_t addr[]={1,2,3,4,5,6};CHECK(bth_cmdq_push_owned(&f.host.commands_,0x040c,addr,sizeof addr,true,f.host.peers_[0].owner)==BTH_OK);
 CHECK(bth_cmdq_push_owned(&f.host.commands_,0x040e,addr,sizeof addr,true,f.host.peers_[0].owner)==BTH_OK);
 CHECK(bth_cmdq_available(&f.host.commands_)==0&&f.host.bleDisconnect(old,false)==Status::Ok);
 CHECK(bth_cmdq_cancel_owner(&f.host.commands_,0x1234)==5);f.ready=true;f.settle();
 CHECK(f.host.radioStats().controller_ready&&!f.host.radioStats().transport_errors);
 puts("REAL_CMDQ_CAPACITY_TRANSACTION_TWO_SECURITY_SLOTS_URGENT_FIFO_DUPLICATE_PASS");
}
static void lifecycle_barrier_and_auto_close(){
 Fixture f;f.boot();f.leConnect();auto old=f.host.bleStats().generation;f.ready=false;fill(f,1);
 f.event(5,{0,0x26,0,0x13});CHECK(f.host.deferred_count_==1&&f.host.bleStats().generation==old);
 auto q=snapshot(&f.host.commands_,sizeof f.host.commands_);
 CHECK(f.host.bleDisconnect(old,false)==Status::Busy&&f.host.bleResumeAdvertising()==Status::Busy);
 CHECK(!f.host.le_advertising_held_&&snapshot(&f.host.commands_,sizeof f.host.commands_)==q);
 bth_cmdq_cancel_owner(&f.host.commands_,0x1234);f.ready=true;f.settle();CHECK(f.host.bleStats().generation==0);
 f.leConnect();old=f.host.bleStats().generation;f.ready=false;f.host.le_.close_requested_=true;f.poll();
 CHECK(f.host.bleDisconnect(old,false)==Status::Busy&&!f.host.le_advertising_held_);
 CHECK(f.host.bleResumeAdvertising()==Status::Ok);f.ready=true;f.settle();f.event(5,{0,0x26,0,0x13});f.settle();CHECK(f.host.bleStats().advertising);
 puts("RETAINED_CONTROLLER_EVENT_BARRIER_AUTOCLOSE_NO_API_OWNERSHIP_PASS");
}
static void pending_gap_race(){
 Fixture f;f.boot();f.leConnect();auto old=f.host.bleStats().generation;
 CHECK(f.host.bleDisconnect(old,true)==Status::Ok);f.settle();f.event(5,{0,0x26,0,0x13});
 Bytes enable;for(unsigned i=0;i<20&&enable.empty();++i){f.poll();while(!f.tx.empty()){auto p=f.pop();if(p[0]==1&&Fixture::op(p)==0x200a){enable=p;break;}f.acknowledge(p);}}
 CHECK(!enable.empty()&&enable[4]==1&&f.host.le_.controlState().pending);
 leEvent(f);auto replacement=f.host.bleStats().generation;CHECK(replacement>old&&f.host.bleStats().connected);
 auto q=snapshot(&f.host.commands_,sizeof f.host.commands_);
 CHECK(f.host.bleDisconnect(replacement,false)==Status::Busy&&!f.host.le_advertising_held_);
 CHECK(snapshot(&f.host.commands_,sizeof f.host.commands_)==q);
 f.complete(0x200a);CHECK(!f.host.le_.controlState().pending&&!f.host.bleStats().advertising);
 CHECK(f.host.bleDisconnect(replacement,false)==Status::Ok);f.settle();f.event(5,{0,0x26,0,0x13});f.settle();CHECK(!f.host.bleStats().advertising);
 puts("PENDING_GAP_ENABLE_BUSY_NO_OLD_COMPLETION_ADVERTISING_HOLD_RACE_PASS");
}
static void receive_reservation_and_watchdog(){
 Fixture f;f.boot();f.leConnect();auto old=f.host.bleStats().generation;bth_rx_lease_t lease={};
 CHECK(bth_rx_flow_received(&f.host.rx_,7,0x26,old,&lease)==BTH_OK);
 CHECK(f.host.bleDisconnect(old,false)==Status::Ok);f.settle();f.event(5,{0,0x26,0,0x13});f.settle();
 CHECK(!f.host.reserved_[1]&&f.host.bleResumeAdvertising()==Status::Ok);f.settle();CHECK(!f.host.bleStats().advertising);
 CHECK(bth_rx_flow_release(&f.host.rx_,lease)==BTH_IGNORED);f.settle();CHECK(f.host.reserved_[1]&&f.host.bleStats().advertising);
 Fixture timeout;timeout.boot();timeout.leConnect();auto owner=timeout.host.bleStats().generation;
 CHECK(timeout.host.bleDisconnect(owner,false)==Status::Ok);timeout.settle();CHECK(timeout.host.bleStats().generation==owner);
 timeout.poll(5000);CHECK(!timeout.host.radioStats().controller_ready&&timeout.host.radioStats().transport_errors==1);
 CHECK(!timeout.host.le_advertising_held_&&timeout.host.le_disconnect_owner_==0);
 CHECK(timeout.host.bleResumeAdvertising()==Status::Unsupported);
 puts("REAL_RX_TICKET_RESUME_GATE_EXISTING_FIVE_SECOND_CONTROLLER_WATCHDOG_PASS");
}
static void begin_scope(){
 Fixture f(false);CHECK(f.host.bleDisconnect(1,false)==Status::Unsupported&&f.host.bleResumeAdvertising()==Status::Unsupported);f.host.end();
 Fixture live;CHECK(live.host.bleResumeAdvertising()==Status::Busy);live.boot();live.leConnect();auto old=live.host.bleStats().generation;
 CHECK(live.host.bleDisconnect(old,false)==Status::Ok);live.host.end();CHECK(!live.host.le_advertising_held_&&!live.host.le_disconnect_owner_);
 CHECK(live.host.bleDisconnect(old,false)==Status::Unsupported);CHECK(live.host.begin(live.config,live.hooks)==Status::Ok);live.baud_changes=0;live.boot();
 CHECK(live.host.bleDisconnect(old,false)==Status::InvalidArgument&&!live.host.le_advertising_held_);
 // Scalars are same-begin tokens. Discard after end/begin, as documented for
 // BleWrite; a future begin can legitimately reuse a numerical generation.
 puts("DISABLED_STOPPED_NEW_BEGIN_HOLD_RESET_SAME_BEGIN_TOKEN_CONTRACT_PASS");
}
static void pending_optional_owner(){
 Fixture f;unsigned boots=0;
 while(!f.host.radioStats().controller_ready&&boots++<1400){f.poll();if(!f.tx.empty()){auto packet=f.pop();auto op=Fixture::op(packet);auto reply=f.response(op);if(op==0x2003)reply={0,0x0b,0,0,0,0,0,0,0};f.complete(op,reply);}}
 CHECK(f.host.radioStats().controller_ready);f.settle();auto cid=openAudio(f);f.leConnect();auto old=f.host.bleStats().generation;
 Bytes feature;f.poll(5000);
 for(unsigned i=0;i<20&&feature.empty();++i){while(!f.tx.empty()){auto p=f.pop();if(p[0]==1&&Fixture::op(p)==0x2016){feature=p;break;}f.acknowledge(p);}if(feature.empty())f.poll();}
 CHECK(!feature.empty()&&f.host.commands_.sent_count==1&&f.host.commands_.sent[0].owner==old);
 CHECK(f.host.bleDisconnect(old,false)==Status::Ok&&f.host.bleStats().generation==old);
 f.status(0x2016);f.poll();CHECK(f.pop()==Bytes({1,6,4,3,0x26,0,0x13}));f.status(0x0406);
 f.event(5,{0,0x26,0,0x13});f.settle();CHECK(f.host.bleStats().generation==0&&!f.host.bleStats().advertising);
 f.event(0x3e,{4,0,0x26,0,3,0,0,0,0,0,0,0});f.settle();CHECK(f.host.bleStats().generation==0&&!f.host.commands_.used);
 CHECK(f.host.bleResumeAdvertising()==Status::Ok);f.settle();f.leConnect();CHECK(f.host.bleStats().generation>old);
 f.event(0x3e,{4,0,0x26,0,3,0,0,0,0,0,0,0});auto received=f.media;f.acl(0x25,cid,mediaPacket(9));f.settle();
 CHECK(f.media==received+1&&f.playing&&!f.host.radioStats().transport_errors);
 CHECK(f.host.bleDisconnect(old,false)==Status::InvalidArgument);
 puts("OPTIONAL_LE_FEATURE_INFLIGHT_OWNERSHIP_CREDIT_LATE_EVENT_RECONNECT_MEDIA_PASS");
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);admission_and_completion();transactional_capacity_and_fifo();lifecycle_barrier_and_auto_close();pending_gap_race();receive_reservation_and_watchdog();begin_scope();pending_optional_owner();printf("BLE_LIFECYCLE_PASS checks=%u host_bytes=%zu\n",checks,sizeof(OwnedHost));}
