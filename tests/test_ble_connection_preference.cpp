#include <algorithm>
#define private public
#include "owned/OwnedHost.h"
#undef private
#define main original_fixture_main
#include "test_owned_host.cpp"
#undef main
#undef CHECK
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"PREFERENCE line%d: %s\n",__LINE__,#x);exit(3);}}while(0)
static const uint8_t actual_local[8]={0x0b,0x11,0x22,0x33,0x44,0x55,0x66,0x77};
struct Wire {
 Fixture& f;std::vector<Bytes> commands;unsigned updateCount=0,featureCount=0;uint8_t updateStatus=0;bool updateMeta=true,featureRespond=true,updateRespond=true,replyRespond=true,returnAclCredits=true;uint8_t remote=3,localLow=0x0b;
 struct Sent {uint32_t at;uint16_t handle,cid,payload;};std::vector<Sent> sent;std::vector<Fixture::Pdu> pdus;
 bth_l2cap_rx_t rx[2];uint8_t buffers[2][2048]={};uint16_t currentCid[2]={};
 explicit Wire(Fixture& p):f(p){CHECK(bth_l2cap_rx_init(&rx[0],0x25,buffers[0],sizeof buffers[0]));CHECK(bth_l2cap_rx_init(&rx[1],0x26,buffers[1],sizeof buffers[1]));}
 void ack(Bytes p){
  if(p[0]==2){bth_h4_frame_t h={};CHECK(bth_h4_decode(p.data(),p.size(),&h)==BTH_OK);unsigned i=h.acl.handle==0x26;
   if(h.acl.pb!=1&&h.acl.payload.len>=4)currentCid[i]=bth_read_le16(h.acl.payload.data+2);
   sent.push_back({f.now,h.acl.handle,currentCid[i],uint16_t(h.acl.payload.len)});bth_l2cap_pdu_t s={};auto result=bth_l2cap_rx_feed(&rx[i],&h.acl,&s);CHECK(result==BTH_MORE||result==BTH_OK);
   if(result==BTH_OK)pdus.push_back({s.handle,s.cid,Bytes(s.payload.data,s.payload.data+s.payload.len)});
   if(returnAclCredits)f.event(0x13,{1,uint8_t(h.acl.handle),uint8_t(h.acl.handle>>8),1,0});return;}
  auto op=Fixture::op(p);commands.push_back(p);
  if(op==0x2016){++featureCount;if(featureRespond){f.status(op);f.event(0x3e,{4,0,p[4],p[5],remote,0x21,0x32,0x43,0x54,0x65,0x76,0x87});}}
  else if(op==0x2013){++updateCount;printf("ACTOR_2013 tuple=%u/%u latency=%u timeout=%u CE=%u/%u\n",bth_read_le16(p.data()+6),bth_read_le16(p.data()+8),bth_read_le16(p.data()+10),bth_read_le16(p.data()+12),bth_read_le16(p.data()+14),bth_read_le16(p.data()+16));if(updateRespond){f.status(op,updateStatus);if(!updateStatus&&updateMeta)f.event(0x3e,{3,0,p[4],p[5],80,0,2,0,0xf4,1});}}
  else if(op==0x201b || op==0x2020 || op==0x2021){if(replyRespond)f.complete(op,{0,p[4],p[5]});}
  else f.acknowledge(p);
 }
 void poll(unsigned add=1){f.poll(add);while(!f.tx.empty())ack(f.pop());}
 void boot(){unsigned count=0;while(!f.host.radioStats().controller_ready&&count++<1400){f.poll();while(!f.tx.empty()){auto p=f.pop();auto op=Fixture::op(p);auto reply=f.response(op);if(op==0x2003){reply={0};reply.insert(reply.end(),actual_local,actual_local+8);reply[1]=localLow;}f.complete(op,reply);}}CHECK(f.host.radioStats().controller_ready);for(unsigned i=0;i<35;++i)poll();}
};
static uint16_t audio(Fixture& f){f.classicConnect();auto s=f.channel(0x19,0x81);f.acl(0x25,s,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,s,{0x20,6,4});f.settle();auto media=f.channel(0x19,0x82);f.acl(0x25,s,{0x30,7,4});f.settle();CHECK(f.host.radioStats().streaming);return media;}
static void actor_preference(){
 Fixture f;Wire w(f);w.boot();auto cid=audio(f);f.leConnect();w.commands.clear();
 for(unsigned i=0;i<20;++i)w.poll();CHECK(!w.featureCount&&!w.updateCount);
 // Real connection age advances; Classic packets continue while the preference
 // proceeds. The before source has no procedure wired into this actor.
 for(unsigned i=0;i<55;++i){f.acl(0x25,cid,{0x80,0x60,0,uint8_t(i),1,2,3,4,5,6,7,8,1,0xaa});w.poll(100);}
 CHECK(w.featureCount==1 && w.updateCount==1);
 CHECK(f.media==55 && f.host.radioStats().streaming && !f.host.radioStats().transport_errors);
 auto it=std::find_if(w.commands.begin(),w.commands.end(),[](const Bytes& p){return Fixture::op(p)==0x2013;});CHECK(it!=w.commands.end());
 const uint8_t expected[]={0x26,0,64,0,96,0,2,0,0xf4,1,0,0,0,0};CHECK(it->size()==18&&!memcmp(it->data()+4,expected,14));
 puts("ACTUAL_ACTOR_CONFIGURED_PREFERENCE_WIRE_PASS");
}
#ifndef PROBE_BEFORE
static void enable_notify(Fixture& f){f.acl(0x26,4,{2,5,2});f.settle();f.acl(0x26,4,{0x12,12,0,1,0});f.settle();}
static void optional_prefix(){
 Fixture f;Wire w(f);w.localLow=1;w.boot();audio(f);f.leConnect();enable_notify(f);w.sent.clear();w.pdus.clear();
 f.ready=false;uint8_t value[512]={};for(unsigned i=0;i<2;++i){CHECK(f.host.bleNotify(value,sizeof value)==Status::Ok);w.poll(0);}
 CHECK(f.host.control_.peers[1].count==2);w.poll(5000);f.ready=true;
 for(unsigned i=0;i<1000;++i){w.poll();if(std::any_of(w.pdus.begin(),w.pdus.end(),[](const Fixture::Pdu& p){return p.cid==5;}))break;}
 unsigned data=0,signal=0;bool have=false;uint32_t previous=0;
 for(auto s:w.sent)if(s.handle==0x26){if(s.cid==4){if(have&&uint32_t(s.at-previous)<10)printf("OPTIONAL_PREFIX_RED gap=%lu fragment=%u\n",(unsigned long)(s.at-previous),data);CHECK(!have||uint32_t(s.at-previous)>=10);previous=s.at;have=true;++data;CHECK(s.payload<=27);}if(s.cid==5){++signal;CHECK(data==40);}}
 CHECK(data==40&&signal==1&&!f.host.radioStats().transport_errors);puts("OPTIONAL_CID5_PRESERVES_TWO_FULL_ATT_PREFIX_TIMING_PASS");
}
static void optional_cts(){
 Fixture f;Wire w(f);w.localLow=1;w.boot();audio(f);f.leConnect();f.ready=false;w.poll(5000);
 CHECK(f.host.le_.transport_.stage==BTH_LE_TRANSPORT_SIGNAL && !f.host.control_.peers[1].count && !f.host.bleStats().connection.signaling_requests_queued);
 f.acl(0x25,f.host.classic_.signalCid(),{0x40,9,4});w.poll(0);f.ready=true;for(unsigned i=0;i<20;++i)w.poll();
 CHECK(!std::any_of(w.pdus.begin(),w.pdus.end(),[](const Fixture::Pdu& p){return p.cid==5;})&&!f.host.radioStats().transport_errors);
 puts("OPTIONAL_CID5_CTS_BUSY_AUDIO_OFF_NO_QUEUE_PASS");
}
static void fallback_observation(){
 Fixture f;Wire w(f);w.localLow=1;w.boot();audio(f);f.leConnect();w.poll(5000);
 for(unsigned i=0;i<20&&w.pdus.empty();++i)w.poll();
 auto it=std::find_if(w.pdus.begin(),w.pdus.end(),[](const Fixture::Pdu& p){return p.cid==5;});
 CHECK(it!=w.pdus.end()&&it->bytes.size()==12&&it->bytes[0]==0x12&&f.host.le_.transport_.stage==BTH_LE_TRANSPORT_SIGNAL_WAIT);
 CHECK(f.host.bleStats().connection.source==1&&f.host.bleStats().connection.interval==24&&!f.host.bleStats().connection.update_reported);
 f.acl(0x26,5,{0x13,it->bytes[1],2,0,0,0});w.poll();
 CHECK(f.host.le_.transport_.stage==BTH_LE_TRANSPORT_UPDATE_WAIT&&f.host.bleStats().connection.source==1&&f.host.bleStats().connection.interval==24&&!f.host.bleStats().connection.update_reported);
 f.event(0x3e,{3,0,0x26,0,80,0,2,0,0xf4,1});w.poll();auto actual=f.host.bleStats().connection;
 CHECK(actual.source==3&&actual.interval==80&&actual.latency==2&&actual.timeout==500&&actual.matches_preference&&actual.update_reported&&!actual.update_status&&f.host.le_.transport_.stage==BTH_LE_TRANSPORT_DONE&&!f.host.radioStats().transport_errors);
 puts("EXPLICIT_CID5_ACCEPT_NOT_ACTUAL_UNTIL_UPDATE_COMPLETE_PASS");
}
static void optional_clock(){for(bool expiry:{false,true}){
 Fixture f;Wire w(f);w.boot();audio(f);f.leConnect();f.ready=false;w.poll(5000);CHECK(f.host.commands_.used==1);
 for(unsigned i=0;i<5;++i)CHECK(bth_cmdq_push(&f.host.commands_,0xfc01,nullptr,0,false)==BTH_OK);
 CHECK(bth_cmdq_available(&f.host.commands_)==2);
 if(!expiry)f.acl(0x25,f.host.classic_.signalCid(),{0x40,9,4});w.poll(expiry?5001:0);
 CHECK(f.host.le_.transport_.stage==BTH_LE_TRANSPORT_DONE);
 f.ready=true;for(unsigned i=0;i<20;++i)w.poll();CHECK(!w.featureCount&&!w.updateCount&&!f.host.radioStats().transport_errors);
 }puts("OPTIONAL_CLOCK_WITH_COMMAND_CAPACITY_AUDIO_OFF_EXPIRY_PASS");}
static void reconnect_metadata(){
 Fixture f;Wire w(f);w.boot();audio(f);f.leConnect();w.poll(5000);for(unsigned i=0;i<15;++i)w.poll();
 auto before=f.host.bleStats();CHECK(before.connection.update_submissions==1&&before.connection.submitted_min==64&&before.connection.command_status==0);
 f.event(5,{0,0x26,0,0x13});w.poll();auto disconnected=f.host.bleStats();
 CHECK(disconnected.connection.command_status==0xff&&!disconnected.connection.submitted_min&&!disconnected.connection.submitted_max&&!disconnected.connection.submitted_latency&&!disconnected.connection.submitted_timeout);
 f.leConnect();auto after=f.host.bleStats();CHECK(after.generation!=before.generation&&after.connection.known&&after.connection.source==1&&after.connection.interval==24);
 CHECK(after.connection.command_status==0xff&&!after.connection.submitted_min&&!after.connection.submitted_max&&!after.connection.submitted_latency&&!after.connection.submitted_timeout);
 CHECK(after.connection.update_submissions==before.connection.update_submissions);puts("RECONNECT_CLEARS_PER_GENERATION_TUPLE_STATUS_PRESERVES_COUNTERS_PASS");
}
static void disabled_and_observed(){
 for(unsigned disabled=0;disabled<3;++disabled){
  Fixture f;f.host.end();if(disabled==0)f.config.ble.audio_connection_preference.enabled=false;if(disabled==1)f.config.ble.audio_priority=false;
  CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);Wire w(f);w.boot();if(disabled<2)audio(f);f.leConnect();
  CHECK(f.host.bleStats().connection.known&&f.host.bleStats().connection.source==1&&f.host.bleStats().connection.interval==24&&!f.host.bleStats().connection.update_reported);
  w.poll(5000);for(unsigned i=0;i<20;++i)w.poll();CHECK(!w.featureCount&&!w.updateCount);
  f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,200,0});for(unsigned i=0;i<10;++i)w.poll();
  auto it=std::find_if(w.commands.begin(),w.commands.end(),[](const Bytes& p){return Fixture::op(p)==0x2020;});CHECK(it!=w.commands.end()&&bth_read_le16(it->data()+6)==24&&bth_read_le16(it->data()+8)==40);
  CHECK(f.host.bleStats().connection.interval==24&&f.host.bleStats().connection.timeout==500&&!f.host.bleStats().connection.update_reported);
 }
 Fixture f;Wire w(f);w.boot();audio(f);f.leConnect();w.updateMeta=false;w.poll(5000);for(unsigned i=0;i<10;++i)w.poll();
 auto accepted=f.host.bleStats().connection;CHECK(w.featureCount==1&&w.updateCount==1&&accepted.interval==24&&accepted.source==1&&!accepted.update_reported&&accepted.submitted_min==64);
 f.event(0x3e,{3,0,0x26,0,24,0,0,0,200,0});auto observed=f.host.bleStats().connection;
 CHECK(observed.known&&observed.source==3&&observed.interval==24&&!observed.matches_preference&&observed.update_reported&&!observed.update_status);
 for(unsigned i=0;i<100;++i)w.poll(100);CHECK(w.featureCount==1&&w.updateCount==1&&!f.host.radioStats().transport_errors);
 puts("DISABLED_IDLE_LEGACY_ACCEPT_OBSERVED_NOT_ACK_NO_REASSERT_PASS");
}
static void optional_rejections(){for(uint8_t error:{uint8_t(0x3b),uint8_t(0x23),uint8_t(0x2a)}){
 Fixture f;Wire w(f);w.updateStatus=error;w.updateMeta=false;w.boot();auto cid=audio(f);f.leConnect();w.poll(5000);for(unsigned i=0;i<20;++i)w.poll();
 auto s=f.host.bleStats().connection;CHECK(w.updateCount==1&&s.preference_error==error&&s.command_status==error&&s.source==1&&s.interval==24&&!s.update_reported);
 for(unsigned i=0;i<50;++i){f.acl(0x25,cid,{0x80,0x60,0,uint8_t(i),1,2,3,4,5,6,7,8,1,0xaa});w.poll(100);}
 CHECK(f.media==50&&w.updateCount==1&&f.host.radioStats().streaming&&!f.host.radioStats().transport_errors);
 }puts("OPTIONAL_REJECT_COLLISION_CLASSIC_PROGRESS_NO_REASSERT_PASS");}
static void queue_control(Fixture& f,unsigned peer,uint16_t cid,unsigned bytes){bth_control_reservation_t slot;CHECK(bth_control_tx_reserve(&f.host.control_,peer,&slot)==BTH_OK);memset(slot.data,0x5a,bytes);CHECK(bth_control_tx_commit(&f.host.control_,slot.token,cid,bytes,672)==BTH_OK);}
static void optional_admission(){
 for(unsigned pool=0;pool<3;++pool){Fixture f;Wire w(f);w.localLow=1;w.boot();audio(f);f.leConnect();
  if(pool)CHECK(bth_hci_flow_set_buffers(&f.host.flow_,1021,pool==1?4:1,0,0));
  f.ready=false;w.poll(5000);for(unsigned i=0;i<3;++i)queue_control(f,0,0x40,1);f.ready=true;
  for(unsigned i=0;i<20;++i)w.poll();CHECK(w.sent.size()>=4);
  CHECK(w.sent[0].handle==0x25&&w.sent[1].handle==0x25&&w.sent[2].handle==0x25&&w.sent[3].handle==0x26&&w.sent[3].cid==5);
  CHECK(!f.host.radioStats().transport_errors);
 }
 // Real outstanding packets exhaust a pool; no fake completion is invented.
 Fixture f;Wire w(f);w.localLow=1;w.boot();audio(f);f.leConnect();
 CHECK(bth_hci_flow_set_buffers(&f.host.flow_,1021,4,0,0));w.returnAclCredits=false;
 for(unsigned i=0;i<3;++i)queue_control(f,1,4,1);for(unsigned i=0;i<30;++i)w.poll();
 CHECK(w.sent.size()==3&&f.host.flow_.classic.available==1&&!f.host.control_.peers[1].count&&f.host.flow_.links[1].outstanding==3);
 f.ready=false;w.poll(5000);f.ready=true;
 for(unsigned i=0;i<10;++i)w.poll();CHECK(f.host.le_.transport_.stage==BTH_LE_TRANSPORT_SIGNAL&&!f.host.control_.peers[1].count&&w.sent.size()==3);
 f.event(0x13,{1,0x26,0,1,0});w.returnAclCredits=true;for(unsigned i=0;i<10;++i)w.poll();CHECK(w.sent.size()==4&&w.sent[3].cid==5&&f.host.flow_.classic.available==2&&f.host.flow_.links[1].outstanding==2);
 // A too-small controller payload terminates this optional attempt; no partial
 // signaling SDU or controller packet credit is consumed.
 Fixture tiny;Wire small(tiny);small.localLow=1;small.boot();audio(tiny);tiny.leConnect();CHECK(bth_hci_flow_set_buffers(&tiny.host.flow_,1021,8,8,4));small.poll(5000);for(unsigned i=0;i<10;++i)small.poll();
 CHECK(!tiny.host.control_.peers[1].count&&tiny.host.le_.transport_.stage==BTH_LE_TRANSPORT_DONE&&tiny.host.bleStats().connection.preference_error==0xfe&&tiny.host.flow_.le.available==4);
 puts("OPTIONAL_ADMISSION_CLASSIC_COMPETITION_SHARED_SINGLE_RESERVE_WHOLE_SDU_PASS");
}
static void pending_parameters(){
 Fixture f;Wire w(f);w.boot();auto media=audio(f);f.leConnect();f.ready=false;
 for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push(&f.host.commands_,0xfc01,nullptr,0,false)==BTH_OK);
 f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,200,0});f.event(0x3e,{6,0x26,0,80,0,120,0,0,0,0xf4,1});
 CHECK(f.host.le_.parameters_pending_&&bth_read_le16(f.host.le_.pending_parameters_+3)==24&&f.host.le_.parameter_count_==1&&!f.host.deferred_count_&&f.playing);
 f.acl(0x25,media,{0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa});f.ready=true;w.poll();CHECK(f.media==1&&f.playing&&f.host.radioStats().streaming);for(unsigned i=0;i<60;++i)w.poll();
 std::vector<Bytes> replies;for(auto& p:w.commands)if(Fixture::op(p)==0x2020||Fixture::op(p)==0x2021)replies.push_back(p);
 CHECK(replies.size()==2&&Fixture::op(replies[0])==0x2021&&replies[0].size()==7&&replies[0][6]==0x3b);
 CHECK(Fixture::op(replies[1])==0x2020&&bth_read_le16(replies[1].data()+6)==80&&bth_read_le16(replies[1].data()+8)==96);
 CHECK(f.media==1&&!f.host.le_.parameters_pending_&&!f.host.deferred_count_&&!f.host.rx_.outstanding&&!f.host.radioStats().transport_errors);
 // A remote procedure must get a quiet window before our optional chain.
 w.poll(5000);CHECK(!w.featureCount&&!w.updateCount);
 puts("PENDING06_OLDEST_RETAINED_INTERSECTION_REPLIES_CLASSIC_PROGRESS_PASS");
}
static void remote_preempts_optional(){for(unsigned mode=0;mode<3;++mode){
 Fixture f;Wire w(f);if(mode==2)w.localLow=1;w.boot();audio(f);f.leConnect();
 if(mode<2){w.poll(5000);CHECK(w.featureCount==1&&!w.updateCount);}
 f.ready=false;w.poll(mode==2?5000:0);
 CHECK(f.host.le_.transport_.stage==(mode==2?BTH_LE_TRANSPORT_SIGNAL:BTH_LE_TRANSPORT_UPDATE_WAIT));
 for(unsigned i=0;i<5;++i)CHECK(bth_cmdq_push(&f.host.commands_,0xfc01,nullptr,0,false)==BTH_OK);
 // Both semantically invalid and disjoint requests require a negative reply.
 f.event(0x3e,{6,0x26,0,uint8_t(mode==0?5:24),0,40,0,0,0,200,0});
 CHECK(f.host.le_.parameters_pending_&&f.host.le_.transport_.stage==BTH_LE_TRANSPORT_DONE);
 f.ready=true;for(unsigned i=0;i<30;++i)w.poll();
 CHECK(!w.updateCount&&!std::any_of(w.pdus.begin(),w.pdus.end(),[](const Fixture::Pdu& p){return p.cid==5;}));
 auto it=std::find_if(w.commands.begin(),w.commands.end(),[](const Bytes& p){return Fixture::op(p)==0x2021;});CHECK(it!=w.commands.end()&&(*it)[6]==0x3b&&!f.host.radioStats().transport_errors);
 }puts("INCOMING06_IMMEDIATE_YIELD_CANCELS_QUEUED_UPDATE_OR_FALLBACK_PASS");}
static void overflow_le_only(){
 Fixture f;Wire w(f);w.boot();auto cid=audio(f);f.leConnect();f.ready=false;
 for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push(&f.host.commands_,0xfc01,nullptr,0,false)==BTH_OK);
 for(unsigned i=0;i<9;++i)f.event(0x3e,{6,0x26,0,uint8_t(64+i),0,96,0,0,0,0xf4,1});
 CHECK(f.host.le_.closeRequested()&&f.host.le_.parameter_count_==7&&bth_read_le16(f.host.le_.pending_parameters_+3)==64&&!f.host.deferred_count_&&f.playing);
 f.acl(0x25,cid,{0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa});w.poll(0);CHECK(f.media==1&&f.playing);
 f.ready=true;for(unsigned i=0;i<30;++i)w.poll();
 auto it=std::find_if(w.commands.begin(),w.commands.end(),[](const Bytes& p){return Fixture::op(p)==0x0406;});CHECK(it!=w.commands.end()&&bth_read_le16(it->data()+4)==0x26&&f.host.radioStats().connected&&f.host.radioStats().streaming&&!f.host.radioStats().transport_errors);
 f.event(5,{0,0x26,0,0x13});for(unsigned i=0;i<5;++i)w.poll();CHECK(!f.host.bleStats().connected&&f.host.radioStats().streaming);
 puts("EIGHT06_FIFO_OVERFLOW_LE_ONLY_CLOSE_CLASSIC_CALLBACK_CONTINUES_PASS");
}
static void preflight(){
 static_assert(sizeof(bth_command_sent_t)==8,"sent handle must occupy existing padding");
 Fixture f;Wire w(f);w.featureRespond=false;w.boot();audio(f);f.leConnect();w.poll(5000);CHECK(w.featureCount==1&&f.host.commands_.sent_count==1&&f.host.flow_.command_credits==0);
 CHECK(f.host.commands_.sent[0].handle==0x26);auto pending=f.host.le_.transport_;auto malformed=f.host.bleStats().malformed_pdus;
 f.complete(0x2016,{0},7);f.event(0x0f,{0,7,0x16,0x20,0});f.complete(0x2020,{0,0x26,0},7);
 CHECK(f.host.commands_.sent_count==1&&f.host.flow_.command_credits==0&&!memcmp(&pending,&f.host.le_.transport_,sizeof pending)&&f.host.bleStats().malformed_pdus==malformed+3);
 f.status(0x2016);CHECK(f.host.commands_.sent_count==0&&f.host.flow_.command_credits==1);
 f.event(0x3e,{4,0,0x26,0,3,1,2,3,4,5,6,7});w.updateRespond=false;w.poll();CHECK(w.updateCount==1&&f.host.commands_.sent_count==1&&!f.host.flow_.command_credits);
 f.complete(0x2013,{0},7);CHECK(f.host.commands_.sent_count==1&&!f.host.flow_.command_credits);
 f.status(0x2013);CHECK(!f.host.commands_.sent_count&&f.host.flow_.command_credits==1);
 w.replyRespond=false;f.event(0x3e,{6,0x26,0,64,0,96,0,0,0,0xf4,1});for(unsigned i=0;i<4;++i)w.poll();CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].opcode==0x2020&&!f.host.flow_.command_credits);
 // Duplicate old status cannot regrant a command credit already consumed by2020.
 f.event(0x0f,{0,7,0x13,0x20});f.complete(0x2020,{0},7);f.complete(0x2020,{0,0x26,0,0},7);f.complete(0x2020,{0,0x27,0},7);
 CHECK(f.host.commands_.sent_count==1&&!f.host.flow_.command_credits);
 f.complete(0x2020,{0,0x26,0});CHECK(!f.host.commands_.sent_count&&f.host.flow_.command_credits==1);
 f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,200,0});for(unsigned i=0;i<4;++i)w.poll();CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].opcode==0x2021&&!f.host.flow_.command_credits);
 f.status(0x2021);CHECK(f.host.commands_.sent_count==1&&!f.host.flow_.command_credits);f.complete(0x2021,{0x0c});CHECK(!f.host.commands_.sent_count&&f.host.flow_.command_credits==1);
 f.event(0x3e,{5,0x26,0,1,2,3,4,5,6,7,8,9,10});for(unsigned i=0;i<4;++i)w.poll();CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].opcode==0x201b);
 f.complete(0x201b,{0x0c,0xff,0xff});CHECK(!f.host.commands_.sent_count&&f.host.flow_.command_credits==1&&!f.host.radioStats().transport_errors);
 printf("PREFLIGHT_OWNER_KIND_LENGTH_HANDLE_NO_DOUBLE_CREDIT_PASS sent=%zu cmdq=%zu stats=%zu config=%zu host=%zu\n",sizeof(bth_command_sent_t),sizeof(bth_cmdq_t),sizeof(BleStats),sizeof(BleConfig),sizeof(OwnedHost));
}
static void oldest_owner(){
 Fixture f;Wire w(f);w.replyRespond=false;w.boot();f.leConnect();auto old=f.host.bleStats().generation;
 f.event(0x3e,{5,0x26,0,1,2,3,4,5,6,7,8,9,10});for(unsigned i=0;i<4;++i)w.poll();CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].opcode==0x201b);
 f.complete(0x201b,{0,0x26,0},2);CHECK(f.host.flow_.command_credits==2);
 f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,200,0});for(unsigned i=0;i<2;++i)w.poll();CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].owner==old);
 f.event(5,{0,0x26,0,0x13});w.poll();f.leConnect();auto fresh=f.host.bleStats().generation;CHECK(fresh!=old);
 f.event(0x3e,{6,0x26,0,24,0,40,0,0,0,200,0});for(unsigned i=0;i<3;++i)w.poll();CHECK(f.host.commands_.sent_count==2&&f.host.commands_.sent[0].owner==old&&f.host.commands_.sent[1].owner==fresh);
 const auto state=f.host.le_.transport_;const auto actual=f.host.bleStats().connection;
 f.complete(0x2020,{0,0x26,0});CHECK(f.host.commands_.sent_count==1&&f.host.commands_.sent[0].owner==fresh&&!memcmp(&state,&f.host.le_.transport_,sizeof state));
 CHECK(f.host.bleStats().connection.command_status_reports==actual.command_status_reports&&f.host.bleStats().connection.source==actual.source);
 f.complete(0x2020,{0,0x26,0});CHECK(!f.host.commands_.sent_count&&!f.host.radioStats().transport_errors);
 puts("SAME_HANDLE_REUSE_OLDEST_SENT_OWNER_TOMBSTONE_RETIRED_ONCE_PASS");
}
static void wrap_features_and_watchdog(){
 Fixture f;Wire w(f);w.localLow=3;w.boot();audio(f);f.now=0xffffff00u;f.leConnect();w.poll(4964);CHECK(!w.featureCount&&!w.updateCount&&!f.host.bleStats().connection.preference_started);w.poll();
 CHECK(f.host.bleStats().connection.preference_started&&!w.featureCount&&!w.updateCount);
 f.event(0x3e,{4,0,0x26,0,3,1,2,3,4,5,6,7});for(unsigned i=0;i<5;++i)w.poll();CHECK(!w.featureCount&&w.updateCount==1&&f.host.bleStats().connection.source==3);
 Fixture passive;Wire wait(passive);wait.localLow=3;wait.boot();audio(passive);passive.leConnect();wait.poll(5000);wait.poll(5001);CHECK(!wait.featureCount&&!wait.updateCount&&passive.host.bleStats().connection.preference_error==0xff&&!passive.host.radioStats().transport_errors);
 Fixture missing;Wire noack(missing);noack.featureRespond=false;noack.boot();audio(missing);missing.leConnect();noack.poll(5000);CHECK(noack.featureCount==1);noack.poll(5001);CHECK(missing.host.radioStats().transport_errors==1&&!missing.host.radioStats().controller_ready);
 puts("WRAP_5S_FEATURE_BIT1_VS3_PASSIVE_TIMEOUT_REAL_ACK_WATCHDOG_PASS");
}
static void enhanced_and_config(){
 const BleConnectionPreference defaults;CHECK(defaults.enabled&&defaults.interval_min==64&&defaults.interval_max==96&&defaults.latency==2&&defaults.timeout==500);
 for(unsigned invalid=0;invalid<5;++invalid){Fixture bad;bad.host.end();auto& p=bad.config.ble.audio_connection_preference;if(invalid==0)p.interval_min=5;if(invalid==1)p.interval_max=3201;if(invalid==2)p.latency=500;if(invalid==3)p.timeout=9;if(invalid==4){p.interval_min=p.interval_max=40;p.latency=0;p.timeout=10;}CHECK(bad.host.begin(bad.config,bad.hooks)==Status::InvalidArgument&&bad.tx.empty());}
 Fixture f;Wire w(f);w.boot();audio(f);Bytes e(31,0);e[0]=0x0a;e[2]=0x26;e[4]=1;e[24]=80;e[26]=2;e[28]=0xf4;e[29]=1;f.event(0x3e,e);for(unsigned i=0;i<5;++i)w.poll();
 CHECK(f.host.bleStats().connection.known&&f.host.bleStats().connection.source==0x0a&&f.host.bleStats().connection.matches_preference);
 w.poll(5000);for(unsigned i=0;i<5;++i)w.poll();CHECK(!w.featureCount&&!w.updateCount&&f.host.bleStats().connection.preference_started&&!f.host.bleStats().connection.preference_error);
 CHECK(!memcmp(f.host.le_features_,actual_local,8)&&!memcmp(f.host.le_.transport_.local_feature_set,actual_local,8));
 puts("DEFAULTS_VALIDATION_ACTUAL_BOOT8_ENHANCED_INITIAL_SKIP_REDUNDANT_QUERY_PASS");
}
#endif
int main(int argc,char** argv){setvbuf(stdout,nullptr,_IONBF,0);const char* which=argc>1?argv[1]:"all";
#ifndef PROBE_BEFORE
 if(!strcmp(which,"prefix"))optional_prefix();else if(!strcmp(which,"cts"))optional_cts();else if(!strcmp(which,"fallback"))fallback_observation();else if(!strcmp(which,"clock"))optional_clock();else if(!strcmp(which,"reconnect"))reconnect_metadata();else if(!strcmp(which,"pending"))pending_parameters();else if(!strcmp(which,"admission"))optional_admission();else if(!strcmp(which,"disabled"))disabled_and_observed();else if(!strcmp(which,"reject"))optional_rejections();else if(!strcmp(which,"preflight"))preflight();else if(!strcmp(which,"owner"))oldest_owner();else if(!strcmp(which,"wrap"))wrap_features_and_watchdog();else if(!strcmp(which,"enhanced"))enhanced_and_config();else if(!strcmp(which,"overflow"))overflow_le_only();else if(!strcmp(which,"preempt"))remote_preempts_optional();else {actor_preference();optional_prefix();optional_cts();fallback_observation();optional_clock();reconnect_metadata();disabled_and_observed();optional_rejections();optional_admission();pending_parameters();remote_preempts_optional();overflow_le_only();preflight();oldest_owner();wrap_features_and_watchdog();enhanced_and_config();}
#else
 (void)which;actor_preference();
#endif
 printf("PREFERENCE_ACTOR_PASS checks=%u\n",checks);}
