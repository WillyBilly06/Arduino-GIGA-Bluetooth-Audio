#define private public
#include "owned/OwnedHost.h"
#undef private
#define main unused_fixture_main
#include "test_owned_host.cpp"
#undef main

static uint16_t be16(const uint8_t* p){return uint16_t(uint16_t(p[0])<<8|p[1]);}
static uint16_t cache(const Fixture& f,uint16_t cid){for(const auto& c:f.host.classic_.channels_)if(c.local==cid)return c.mtu;return 0;}
static const bth_l2cap_channel_t& channel(const Fixture& f,uint16_t cid){auto* c=bth_l2cap_sig_find(&f.host.signaling_,cid);CHECK(c);return *c;}
static Bytes media(unsigned n){return {0x80,0x60,0,uint8_t(n),1,2,3,4,5,6,7,8,1,0xaa};}
static uint16_t audio(Fixture& f){f.boot();f.classicConnect();auto signal=f.channel(0x19,0x81);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();auto cid=f.channel(0x19,0x82);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing);return cid;}
static Bytes config(uint16_t cid,uint16_t mtu,uint8_t id=0x56,bool reject=false){Bytes p={4,id,8,0,uint8_t(cid),uint8_t(cid>>8),0,0,1,2,uint8_t(mtu),uint8_t(mtu>>8)};if(reject){p[2]+=4;p.insert(p.end(),{0x3e,2,1,2});}return p;}
static Bytes attributes(uint16_t tid,const Bytes& continuation={0}){Bytes p={4,uint8_t(tid>>8),uint8_t(tid),0,uint8_t(13+continuation.size()),0,1,0,1,0xff,0xff,0x35,5,0x0a,0,0,0xff,0xff};p.insert(p.end(),continuation.begin(),continuation.end());return p;}
struct Wire {
 Fixture& f;bth_l2cap_rx_t rx={};uint8_t storage[4096]={};bool return_credits=true,hold_security_read=false;unsigned sequence=0,current_first=0;uint16_t current_cid=0;std::vector<Bytes> held_security;
 struct Pdu{uint16_t cid;Bytes bytes;unsigned first,last;};std::vector<Pdu> sent;
 explicit Wire(Fixture& v):f(v){CHECK(bth_l2cap_rx_init(&rx,0x25,storage,sizeof storage));}
 void collect(){while(!f.tx.empty()){
  auto packet=f.pop();if(packet[0]!=2){if(hold_security_read&&Fixture::op(packet)==0x1408)held_security.push_back(packet);else f.acknowledge(packet);continue;}bth_h4_frame_t h={};CHECK(bth_h4_decode(packet.data(),packet.size(),&h)==BTH_OK&&h.acl.handle==0x25);++sequence;
  if(h.acl.pb!=1){CHECK(h.acl.payload.len>=4);current_first=sequence;current_cid=bth_read_le16(h.acl.payload.data+2);}
  bth_l2cap_pdu_t p={};auto status=bth_l2cap_rx_feed(&rx,&h.acl,&p);CHECK(status==BTH_MORE||status==BTH_OK);if(status==BTH_OK){CHECK(p.cid==current_cid);sent.push_back({p.cid,Bytes(p.payload.data,p.payload.data+p.payload.len),current_first,sequence});}
  if(return_credits)f.event(0x13,{1,0x25,0,1,0});
 }}
 void poll(unsigned ticks=1){for(unsigned i=0;i<ticks;++i){f.poll();collect();}}
 void finish(){for(unsigned i=0;i<1500;++i){poll();if(!f.host.control_.used&&!f.host.peers_[0].has_pending&&!f.host.classic_deferred_count_&&!f.host.rx_.outstanding)return;}CHECK(false);}
 const Pdu& one(uint16_t cid,uint16_t tid)const{const Pdu* found=nullptr;for(const auto& p:sent)if(p.cid==cid&&p.bytes.size()>=3&&be16(p.bytes.data()+1)==tid){CHECK(!found);found=&p;}CHECK(found);return *found;}
 const Pdu& label(uint16_t cid,uint8_t label,uint8_t type)const{const Pdu* found=nullptr;for(const auto& p:sent)if(p.cid==cid&&p.bytes.size()>=4&&(p.bytes[0]>>4)==label&&p.bytes[3]==type){CHECK(!found);found=&p;}CHECK(found);return *found;}
};
static void accept(Fixture& f,Wire& w,uint16_t cid,uint16_t mtu,uint8_t id=0x56){f.acl(0x25,1,config(cid,mtu,id));w.finish();CHECK(channel(f,cid).state==BTH_CHANNEL_OPEN&&channel(f,cid).remote_mtu==mtu);bool seen=false;for(const auto& p:w.sent)if(p.cid==1&&p.bytes.size()==10&&p.bytes[0]==5&&p.bytes[1]==id){CHECK(!be16(p.bytes.data()+8));seen=true;}CHECK(seen);printf("MTU_REFRESH negotiated=%u cache=%u\n",mtu,cache(f,cid));CHECK(cache(f,cid)==mtu);}
static Bytes full_attributes(Fixture& f,Wire& w,uint16_t local,uint16_t remote,uint16_t mtu,uint16_t first_tid){
 Bytes output,continuation={0};unsigned fragments=0;
 do{auto tid=uint16_t(first_tid+fragments);f.acl(0x25,local,attributes(tid,continuation));w.finish();const auto& p=w.one(remote,tid);CHECK(p.bytes.size()<=mtu&&p.bytes[0]==5&&be16(p.bytes.data()+3)==p.bytes.size()-5);auto count=be16(p.bytes.data()+5);CHECK(p.bytes.size()>=8u+count);output.insert(output.end(),p.bytes.begin()+7,p.bytes.begin()+7+count);auto bytes=p.bytes[7+count];CHECK(bytes==0||bytes==2);CHECK(p.bytes.size()==8u+count+bytes);continuation=Bytes(p.bytes.begin()+7+count,p.bytes.end());++fragments;CHECK(fragments<20);}while(continuation[0]);
 size_t count=0;const auto* records=bth_sdp_speaker_records(&count);CHECK(count==3&&records[0].handle==0x10001&&output.size()==records[0].attributes.len+2&&output[0]==0x35&&output[1]==records[0].attributes.len&&!memcmp(output.data()+2,records[0].attributes.data,records[0].attributes.len));
 CHECK(fragments>1);printf("SDP_MTU%u continuation_responses=%u exact_attribute_bytes=%zu\n",mtu,fragments,output.size());return output;
}
static void sdp_continuation(){Fixture f(false);const auto media_cid=audio(f);const auto sdp=f.channel(1,0x83);Wire w(f);const auto stops=f.stops,prepared=f.configured,owner=f.host.peers_[0].owner;accept(f,w,sdp,48);full_attributes(f,w,sdp,0x83,48,0x100);f.acl(0x25,media_cid,media(1));w.finish();CHECK(f.media==1&&f.stops==stops&&f.configured==prepared&&f.host.peers_[0].owner==owner&&f.host.radioStats().streaming&&!f.host.radioStats().transport_errors);puts("PUBLIC_ACTOR_MTU48_SDP_CONTINUATION_MEDIA_STATE_PASS");}
static void failure_preserves_cache(){Fixture f(false);audio(f);const auto sdp=f.channel(1,0x83);Wire w(f);const auto before=channel(f,sdp);f.acl(0x25,1,config(sdp,48,0x61,true));w.finish();CHECK(channel(f,sdp).remote_mtu==672&&cache(f,sdp)==672&&!memcmp(&before,&channel(f,sdp),sizeof before));accept(f,w,sdp,48);const auto after=channel(f,sdp);f.acl(0x25,1,config(sdp,96,0x62,true));w.finish();CHECK(cache(f,sdp)==48&&!memcmp(&after,&channel(f,sdp),sizeof after)&&f.playing);puts("REJECTED_CONFIGURATION_PRESERVES_CANONICAL_CACHE_STATE_PASS");}
static void queued_reply_order(){
 Fixture f(false);const auto media_cid=audio(f);const auto sdp=f.channel(1,0x83);CHECK(bth_hci_flow_set_buffers(&f.host.flow_,27,2,0,0));Wire w(f);w.return_credits=false;
 f.acl(0x25,sdp,attributes(0x200));for(unsigned i=0;i<20&&f.host.flow_.classic.available;++i)w.poll();CHECK(!f.host.flow_.classic.available&&f.host.control_.peers[0].tx.active&&w.sent.empty()&&w.sequence==2);
 f.acl(0x25,1,config(sdp,48));w.poll(4);CHECK(cache(f,sdp)==48&&f.host.control_.peers[0].count==2);
 f.acl(0x25,sdp,attributes(0x201));w.poll(4);CHECK(f.host.control_.peers[0].count==3&&!f.host.peers_[0].has_pending);
 f.acl(0x25,media_cid,media(1));w.poll(4);CHECK(f.media==1&&!f.host.rx_.outstanding&&f.playing);
 f.event(0x13,{1,0x25,0,2,0});w.return_credits=true;w.finish();const auto& old=w.one(0x83,0x200);const auto& fresh=w.one(0x83,0x201);const Wire::Pdu* accepted=nullptr;for(const auto& p:w.sent)if(p.cid==1&&p.bytes.size()==10&&p.bytes[0]==5&&p.bytes[1]==0x56)accepted=&p;CHECK(accepted&&old.bytes.size()==100&&old.first==1&&old.last<accepted->first&&accepted->last<fresh.first&&fresh.bytes.size()<=48&&fresh.bytes[0]==5);CHECK(old.last>old.first&&f.host.flow_.classic.available==2&&!f.host.radioStats().transport_errors);
 printf("H4_FIFO old_sdu=%zu fragments=%u..%u config_accept=%u..%u new_sdu=%zu fragments=%u..%u PASS\n",old.bytes.size(),old.first,old.last,accepted->first,accepted->last,fresh.bytes.size(),fresh.first,fresh.last);
}
static unsigned volumes=0;static uint8_t last_volume=0;
static Bytes passthrough(uint8_t label,unsigned operands){Bytes p={uint8_t(label<<4),0x11,0x0e,0,0x48,0x7c,0x44,uint8_t(operands)};for(unsigned i=0;i<operands;++i)p.push_back(uint8_t(i*7));return p;}
static void avrcp_target_and_large_failure(){
 Fixture f(false);f.host.end();f.config.callbacks.volume=[](uint8_t v,void*){++volumes;last_volume=v;};CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);const auto media_cid=audio(f);const auto avrcp=f.channel(0x17,0x83);Wire w(f);accept(f,w,avrcp,48);w.sent.clear();
 f.acl(0x25,avrcp,{0xa0,0x11,0x0e,1,0x48,0,0,0x19,0x58,0x10,0,0,1,2});w.finish();CHECK(w.label(0x83,10,12).bytes.size()==18);
 f.acl(0x25,avrcp,{0xb0,0x11,0x0e,1,0xff,0x30,0xff,0xff,0xff,0xff,0xff});w.finish();CHECK(w.label(0x83,11,12).bytes.size()==11);
 f.acl(0x25,avrcp,{0xc0,0x11,0x0e,0,0x48,0,0,0x19,0x58,0x50,0,0,1,64});w.finish();CHECK(w.label(0x83,12,9).bytes.size()==14&&last_volume==64);
 f.acl(0x25,avrcp,{0xd0,0x11,0x0e,3,0x48,0,0,0x19,0x58,0x31,0,0,5,13,0,0,0,0});w.finish();CHECK(w.label(0x83,13,15).bytes.size()==15);CHECK(f.host.setVolume(72)==Status::Ok);w.finish();CHECK(w.label(0x83,13,13).bytes.size()==15&&last_volume==72);
 auto boundary=passthrough(14,40);CHECK(boundary.size()==48);f.acl(0x25,avrcp,boundary);w.finish();auto expected=boundary;expected[0]|=2;expected[3]=8;CHECK(w.label(0x83,14,8).bytes==expected);
 const auto before=w.sent.size();const auto malformed=f.host.classic_.control_status_.malformed_events,stops=f.stops,prepared=f.configured;auto large=passthrough(15,52);CHECK(large.size()==60);f.acl(0x25,avrcp,large);w.finish();CHECK(w.sent.size()==before&&f.host.classic_.control_status_.malformed_events==malformed+1&&!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding);f.acl(0x25,media_cid,media(1));w.finish();CHECK(f.media==1&&f.stops==stops&&f.configured==prepared&&f.playing&&!f.host.radioStats().transport_errors);for(const auto& p:w.sent)if(p.cid==0x83)CHECK(p.bytes.size()<=48);
 printf("AVRCP_TARGET caps18_status11_volume14_notify15_boundary48_large60_no_reply_media_progress PASS volume_callbacks=%u\n",volumes);
}
static void reuse_and_identity(){
 Fixture f(false);audio(f);auto sdp=f.channel(1,0x83);Wire w(f);accept(f,w,sdp,48);auto old_owner=f.host.peers_[0].owner;
 CHECK(!f.host.classic_.channelMtuUpdated(old_owner+1,1,sdp,0x83,96));CHECK(!f.host.classic_.channelMtuUpdated(old_owner,0x17,sdp,0x83,96));CHECK(!f.host.classic_.channelMtuUpdated(old_owner,1,sdp,0x84,96));CHECK(!f.host.classic_.channelMtuUpdated(old_owner,1,sdp,0x83,47));CHECK(cache(f,sdp)==48);
 f.acl(0x25,1,{6,0x70,4,0,uint8_t(sdp),uint8_t(sdp>>8),0x83,0});w.finish();CHECK(!cache(f,sdp));auto reopened=f.channel(1,0x84);CHECK(reopened==sdp&&cache(f,reopened)==672);f.acl(0x25,1,config(reopened,96,0x71));f.settle();CHECK(cache(f,reopened)==96);
 f.event(5,{0,0x25,0,0x13});f.settle();CHECK(!cache(f,reopened));f.classicConnect();CHECK(f.host.peers_[0].owner!=old_owner);auto fresh=f.channel(1,0x83);CHECK(cache(f,fresh)==672);
 CHECK(!f.host.classic_.channelMtuUpdated(old_owner,1,fresh,0x83,48));CHECK(cache(f,fresh)==672);
 CHECK(!f.host.radioStats().transport_errors);puts("CHANNEL_CID_REUSE_GENERATION_PSM_REMOTE_MTU_IDENTITY_GATES_PASS");
}
static void secure_refresh_deferred(){
 Fixture f(false);f.host.end();f.config.callbacks.volume=[](uint8_t,void*){};CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);audio(f);const auto avrcp=f.channel(0x17,0x83);Wire w(f);w.hold_security_read=true;const auto owner=f.host.peers_[0].owner,prepared=f.configured;
 f.event(0x30,{0,0x25,0});w.poll(3);CHECK(w.held_security.size()==1&&!bth_links_secure(&f.host.links_,0x25)&&!f.playing&&cache(f,avrcp)==672);
 f.acl(0x25,1,config(avrcp,48));w.poll(4);CHECK(channel(f,avrcp).remote_mtu==48&&cache(f,avrcp)==672&&!f.playing);
 w.hold_security_read=false;f.complete(0x1408,{0,0x25,0,16});w.poll(4);CHECK(bth_links_secure(&f.host.links_,0x25)&&f.playing&&cache(f,avrcp)==48&&f.host.peers_[0].owner==owner&&f.configured==prepared+1);
 auto request=passthrough(14,40);f.acl(0x25,avrcp,request);w.finish();CHECK(w.label(0x83,14,8).bytes.size()==48&&!f.host.radioStats().transport_errors);puts("KEY_VERIFICATION_GATE_DEFERRED_MTU_REFRESH_ON_SECURE_RECOVERY_PASS");
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);sdp_continuation();failure_preserves_cache();queued_reply_order();avrcp_target_and_large_failure();reuse_and_identity();secure_refresh_deferred();printf("MTU_REFRESH_PASS checks=%u\n",checks);}
