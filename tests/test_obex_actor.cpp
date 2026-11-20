// Real OwnedHost actor + OBEX: SDP records/EIR, disabled-default identity, an
// OPP push over a phone-opened PSM 3 channel, and PBAP pulls / MAP listings in
// which the actor opens its own SDP-client (PSM 1) and RFCOMM (PSM 3) channels
// to a scripted phone (real SDP server with a PSE or MAS record, real RFCOMM
// responder).
// Synthetic HCI fixture; no radio or phone.
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#include "../src/owned/OwnedHost.cpp"
#define main original_host_fixture_main
#include "test_owned_host.cpp"
#undef main
namespace {
const Bytes everything={6,0,9,0,15,0x35,3,0x19,0x10,0x02,0xff,0xff,0x35,5,0x0a,0,0,0xff,0xff,0};
Bytes search(uint16_t uuid){return {2,0,1,0,8,0x35,3,0x19,uint8_t(uuid>>8),uint8_t(uuid),0,4,0};}
Bytes sdpExchange(Fixture& f,uint16_t cid,uint16_t remote,const Bytes& request){
 f.output.clear();f.acl(0x25,cid,request);f.settle();for(const auto& p:f.output)if(p.cid==remote)return p.bytes;CHECK(false);return {};
}
Bytes reference(std::vector<bth_sdp_record_t> extra){
 size_t count=0;const auto* speaker=bth_sdp_speaker_records(&count);CHECK(count==3);
 std::vector<bth_sdp_record_t> records={speaker[0],speaker[2]};records.insert(records.end(),extra.begin(),extra.end()); // Fixture: no volume callback.
 bth_sdp_t server{};CHECK(bth_sdp_init(&server,records.data(),records.size(),77));bth_sdp_cursor_t cursor{};uint8_t out[672];size_t written=0;
 CHECK(bth_sdp_process_compact(&server,&cursor,everything.data(),everything.size(),out,sizeof out,&written)==BTH_OK);return Bytes(out,out+written);
}
bool psm3Refused(Fixture& f){
 f.output.clear();f.acl(0x25,1,{2,7,4,0,3,0,0x90,0});f.settle();
 for(const auto& p:f.output)if(p.cid==1)for(size_t at=0;at+4<=p.bytes.size();at+=size_t(bth_read_le16(p.bytes.data()+at+2))+4)
  if(p.bytes[at]==3&&p.bytes.size()>=at+12)return bth_read_le16(p.bytes.data()+at+8)!=0&&bth_read_le16(p.bytes.data()+at+8)!=1;
 return false;
}
const uint8_t PseChannel=19,MasChannel=17;
const uint8_t mas_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x0a, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x32,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,MasChannel,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x34,0x09,0x01,0x04, 0x09,0x01,0x00,0x25,0x03,'M','A','S',
 0x09,0x03,0x15,0x08,0x00, 0x09,0x03,0x16,0x08,0x0e, 0x09,0x03,0x17,0x0a,0x00,0x00,0x00,0x1f};
const uint8_t pse_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x09, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x2f,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,PseChannel,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x30,0x09,0x01,0x02, 0x09,0x01,0x00,0x25,0x03,'P','S','E'};
Bytes build(uint8_t opcode,std::initializer_list<Bytes> parts){Bytes p={opcode,0,0};for(const auto& part:parts)p.insert(p.end(),part.begin(),part.end());p[1]=uint8_t(p.size()>>8);p[2]=uint8_t(p.size());return p;}
Bytes hbytes(uint8_t id,const std::string& v){Bytes h={id,uint8_t((v.size()+3)>>8),uint8_t(v.size()+3)};h.insert(h.end(),v.begin(),v.end());return h;}
Bytes text16(const char* s){Bytes out={0x01,0,0};for(;*s;++s){out.push_back(0);out.push_back(uint8_t(*s));}out.push_back(0);out.push_back(0);out[2]=uint8_t(out.size());return out;}
struct Events {std::string name,data,phonebook,map;uint32_t length=0;unsigned begins=0,ends=0,pb_ends=0,map_ends=0;bool ok=false;PbapResult result;MapResult map_result;};
struct Phone {
 VoiceProfiles rfcomm;ObexPort port;uint8_t ring[4096]{};ObexConfig config;Events ev;Fixture f;
 std::map<uint16_t,uint16_t> actor_of;bth_rfcomm_t peer{};uint32_t pg=23;uint16_t rfcomm_phone=0,sdp_phone=0;
 bth_sdp_t pse{};bth_sdp_cursor_t cursor{};bth_sdp_record_t records[1]={{0x10009,{pse_record,sizeof pse_record}}};
 std::map<uint8_t,Bytes> inbox;std::set<uint8_t> closed;size_t read=0;uint8_t reply[700]{};uint8_t next_id=0x60;
 std::vector<uint16_t> phone_disconnects;unsigned sdp_requests=0,connects[4]={};uint8_t server=PseChannel;
 // map: the scripted phone holds a MAS record (channel 17) instead of the PSE.
 Phone(bool opp,bool pbap,bool connect=true,bool map=false){
  config.opp_enabled=opp;config.pbap_enabled=pbap;config.map_enabled=map;config.user=&ev;
  if(map){records[0]={0x1000a,{mas_record,sizeof mas_record}};server=MasChannel;}
  config.on_map_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->map.append(reinterpret_cast<const char*>(p),n);};
  config.on_map_end=[](const MapResult& r,void* u){auto& e=*static_cast<Events*>(u);e.map_result=r;++e.map_ends;};
  config.on_object_begin=[](const char* n,const char*,uint32_t l,void* u){auto& e=*static_cast<Events*>(u);e.name=n;e.length=l;e.data.clear();++e.begins;};
  config.on_object_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->data.append(reinterpret_cast<const char*>(p),n);};
  config.on_object_end=[](bool ok,void* u){auto& e=*static_cast<Events*>(u);e.ok=ok;++e.ends;};
  config.on_phonebook_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->phonebook.append(reinterpret_cast<const char*>(p),n);};
  config.on_phonebook_end=[](const PbapResult& r,void* u){auto& e=*static_cast<Events*>(u);e.result=r;++e.pb_ends;};
  CHECK(port.begin(config,ring,sizeof ring));CHECK(bth_sdp_init(&pse,records,1,5));
  f.host.end();f.config.voice_control=&rfcomm;f.config.voice_config=VoiceControlConfig();f.config.voice_config.obex=&port;
  CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();if(connect)f.classicConnect();
 }
 ~Phone(){f.host.end();}
 void signal(const Bytes& b){f.acl(0x25,1,b);}
 void handleSignal(const Bytes& p){
  for(size_t at=0;at+4<=p.size();){
   const uint8_t code=p[at],id=p[at+1];const size_t n=bth_read_le16(p.data()+at+2);const uint8_t* q=p.data()+at+4;
   if(code==2){
    const uint16_t psm=bth_read_le16(q),scid=bth_read_le16(q+2),phone=psm==1?0x95:0x96;CHECK(psm==1||psm==3);++connects[psm];actor_of[phone]=scid;
    if(psm==3){CHECK(bth_rfcomm_init(&peer,++pg,false,server,512,7));rfcomm_phone=phone;}else{sdp_phone=phone;cursor={};}
    signal({3,id,8,0,uint8_t(phone),uint8_t(phone>>8),uint8_t(scid),uint8_t(scid>>8),0,0,0,0});
    signal({4,next_id++,8,0,uint8_t(scid),uint8_t(scid>>8),0,0,1,2,0xa0,2});
   }else if(code==4){const uint16_t dcid=bth_read_le16(q),actor=actor_of[dcid];signal({5,id,6,0,uint8_t(actor),uint8_t(actor>>8),0,0,0,0});}
   else if(code==6){const uint16_t dcid=bth_read_le16(q),scid=bth_read_le16(q+2);phone_disconnects.push_back(dcid);signal({7,id,4,0,uint8_t(dcid),uint8_t(dcid>>8),uint8_t(scid),uint8_t(scid>>8)});}
   at+=n+4;
  }
 }
 void rfcommIn(const Bytes& b){
  size_t n=0;bth_rfcomm_event_t e{};CHECK(bth_rfcomm_receive(&peer,pg,f.now,b.data(),b.size(),reply,sizeof reply,&n,&e)>=BTH_OK);
  if(e.type==BTH_RFCOMM_DATA){inbox[e.dlci].insert(inbox[e.dlci].end(),e.payload.data,e.payload.data+e.payload.len);CHECK(bth_rfcomm_release(&peer,pg,e.dlci,1)==BTH_OK);}
  if(e.type==BTH_RFCOMM_DLC_CLOSED)closed.insert(e.dlci);
  if(n)f.acl(0x25,actor_of[rfcomm_phone],Bytes(reply,reply+n));
 }
 void events(){uint8_t kind,data[ObexPort::ChunkBytes];size_t n;while(port.pop(kind,data,sizeof data,n))ObexPort::deliver(config,kind,data,n);}
 void tick(){
  f.poll();unsigned budget=8;while(budget--&&!f.tx.empty())f.acknowledge(f.pop());
  while(read<f.output.size()){const auto p=f.output[read++];
   if(p.cid==1)handleSignal(p.bytes);
   else if(sdp_phone&&p.cid==sdp_phone){++sdp_requests;uint8_t out[672];size_t m=0;CHECK(bth_sdp_process_compact(&pse,&cursor,p.bytes.data(),p.bytes.size(),out,40,&m)==BTH_OK);f.acl(0x25,actor_of[sdp_phone],Bytes(out,out+m));}
   else if(rfcomm_phone&&p.cid==rfcomm_phone)rfcommIn(p.bytes);
  }
  if(rfcomm_phone&&peer.initialized){size_t n=0;CHECK(bth_rfcomm_next(&peer,pg,f.now,reply,sizeof reply,&n)>=BTH_OK);if(n)f.acl(0x25,actor_of[rfcomm_phone],Bytes(reply,reply+n));}
  events();
 }
 void ticks(unsigned n){for(unsigned i=0;i<n;++i)tick();}
 const bth_rfcomm_dlc_t* dlc(uint8_t d)const{for(const auto& c:peer.dlc)if(c.state&&c.dlci==d)return &c;return nullptr;}
 void send(uint8_t d,const Bytes& p){
  size_t at=0;unsigned guard=0;
  while(at<p.size()){const auto* c=dlc(d);CHECK(c&&c->mtu);const size_t k=p.size()-at<c->mtu?p.size()-at:c->mtu;size_t n=0;
   const auto st=bth_rfcomm_send(&peer,pg,d,p.data()+at,k,reply,sizeof reply,&n);if(st==BTH_MORE){tick();CHECK(++guard<500);continue;}
   CHECK(st==BTH_OK);f.acl(0x25,actor_of[rfcomm_phone],Bytes(reply,reply+n));at+=k;tick();} // Host RX window: one ACL per turn.
 }
 Bytes await(uint8_t d,unsigned limit=600){
  for(unsigned i=0;i<limit;++i){auto& b=inbox[d];if(b.size()>=3&&b.size()>=size_t(b[1]<<8|b[2])){const size_t n=size_t(b[1]<<8|b[2]);Bytes p(b.begin(),b.begin()+n);b.erase(b.begin(),b.begin()+n);return p;}tick();}
  CHECK(false);return {};
 }
};
void default_disabled_identical(){
 Fixture f;f.boot();f.classicConnect();
 CHECK(f.host.classic_.record_count_==2&&f.host.gap_.config.uuid_count==1&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x110c);
 CHECK(f.host.signaling_.service_count==3&&psm3Refused(f)&&f.host.obex_sdp_cid_==0&&f.host.obex_rfcomm_cid_==0);
 const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference({}));
 for(uint16_t uuid:{uint16_t(0x1105),uint16_t(0x112e)})CHECK(sdpExchange(f,sdp,0x81,search(uuid))==Bytes({3,0,1,0,5,0,0,0,0,0}));
 // No outgoing channel ever appears without OBEX: only the AVRCP controller.
 for(unsigned i=0;i<50;++i){f.poll(100);while(!f.tx.empty())f.acknowledge(f.pop());}f.settle();CHECK(f.host.radioStats().connected);
 for(const auto& c:f.host.signaling_.channels)CHECK(c.state==BTH_CHANNEL_CLOSED||c.psm==1||c.psm==0x17);
}
void enabled_sdp_and_eir(){
 static const uint8_t opp[]={0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x07,0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x05,
  0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x04,0x35,0x03,0x19,0x00,0x08,
  0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00,
  0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x05,0x09,0x01,0x01,0x09,0x01,0x00,0x25,0x10,
  'G','I','G','A',' ','O','b','j','e','c','t',' ','P','u','s','h',0x09,0x03,0x03,0x35,0x02,0x08,0xff};
 static const uint8_t pce[]={0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x08,0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x2e,
  0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00,
  0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x30,0x09,0x01,0x01,0x09,0x01,0x00,0x25,0x0e,'G','I','G','A',' ','P','h','o','n','e','b','o','o','k'};
 Phone p(true,true,false);auto& f=p.f;
 CHECK(p.port.recordCount()==2&&p.port.records()[0].attributes.len==sizeof opp&&!memcmp(p.port.records()[0].attributes.data,opp,sizeof opp));
 CHECK(p.port.records()[1].attributes.len==sizeof pce&&!memcmp(p.port.records()[1].attributes.data,pce,sizeof pce));
 CHECK(f.host.classic_.record_count_==4&&f.host.gap_.config.uuid_count==3&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x1105&&f.host.uuids_[2]==0x112e);
 f.classicConnect();CHECK(f.host.signaling_.service_count==4);
 const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference({p.port.records()[0],p.port.records()[1]}));
 const auto push=sdpExchange(f,sdp,0x81,search(0x1105));CHECK(push.size()==14&&push[8]==1&&push[10]==1&&push[12]==7);
 const auto pce_search=sdpExchange(f,sdp,0x81,search(0x112e));CHECK(pce_search.size()==14&&pce_search[12]==8);
 // With SPP too: one shared RFCOMM owner, eight-record ceiling not exceeded.
 VoiceProfiles rfcomm;SppPort spp;ObexPort obex;uint8_t rx[512],tx[512],ring[2048];ObexConfig c;c.opp_enabled=c.pbap_enabled=true;
 CHECK(spp.begin("S",3,rx,512,tx,512,0)&&obex.begin(c,ring,sizeof ring));
 Fixture g;g.host.end();g.config.voice_control=&rfcomm;g.config.voice_config.serial=&spp;g.config.voice_config.obex=&obex;
 CHECK(g.host.begin(g.config,g.hooks)==Status::Ok&&g.host.classic_.record_count_==5&&g.host.gap_.config.uuid_count==4&&g.host.uuids_[1]==0x1101&&g.host.uuids_[3]==0x112e);g.host.end();
 c.opp_channel=3;CHECK(obex.begin(c,ring,sizeof ring));CHECK(g.host.begin(g.config,g.hooks)==Status::InvalidArgument&&!rfcomm.enabled()); // SPP channel collision.
}
void opp_through_actor(){
 Phone p(true,false);auto& f=p.f;
 const uint16_t local=f.channel(3,0x90);p.read=0;f.output.clear();CHECK(p.rfcomm.cid()==local);
 p.rfcomm_phone=0x90;p.actor_of[0x90]=local;CHECK(bth_rfcomm_init(&p.peer,p.pg,true,0,512,7));
 size_t n=0;CHECK(bth_rfcomm_start(&p.peer,p.pg,f.now,p.reply,sizeof p.reply,&n)==BTH_OK);f.acl(0x25,local,Bytes(p.reply,p.reply+n));p.ticks(5);
 CHECK(bth_rfcomm_open(&p.peer,p.pg,4,true,f.now,p.reply,sizeof p.reply,&n)==BTH_OK);f.acl(0x25,local,Bytes(p.reply,p.reply+n));
 for(unsigned i=0;i<100&&!bth_rfcomm_ready(&p.peer,p.pg,8);++i)p.tick();CHECK(bth_rfcomm_ready(&p.peer,p.pg,8)&&p.port.stats().opp_connected);
 p.send(8,build(0x80,{{0x10,0,0x04,0x00}}));CHECK(p.await(8)==Bytes({0xa0,0,7,0x10,0,0x04,0x00}));
 std::string object;for(unsigned i=0;i<2800;++i)object+=char('a'+i%26);
 p.send(8,build(0x02,{text16("notes.txt"),{0xc3,0,0,0x0a,0xf0},hbytes(0x48,object.substr(0,900))}));CHECK(p.await(8)==Bytes({0x90,0,3}));
 p.send(8,build(0x02,{hbytes(0x48,object.substr(900,1000))}));CHECK(p.await(8)==Bytes({0x90,0,3}));
 p.send(8,build(0x82,{hbytes(0x49,object.substr(1900))}));CHECK(p.await(8)==Bytes({0xa0,0,3}));p.ticks(5);
 CHECK(p.ev.begins==1&&p.ev.name=="notes.txt"&&p.ev.length==2800&&p.ev.data==object&&p.ev.ends==1&&p.ev.ok);
 // Streaming pacing: the larger of the SPP/OBEX intervals reaches the owner.
 f.host.classic_.playing_=true;p.tick();CHECK(p.rfcomm.bulk_interval_==4);f.host.classic_.playing_=false;p.tick();CHECK(p.rfcomm.bulk_interval_==0);
 // ACL loss mid-object fails it; nothing remains admitted.
 p.send(8,build(0x02,{text16("lost.bin"),hbytes(0x48,"x")}));p.await(8);
 f.event(5,{0,0x25,0,0x13});f.settle();p.events();CHECK(p.ev.ends==2&&!p.ev.ok&&!p.port.stats().opp_connected&&p.rfcomm.cid()==0);
}
void pbap_through_actor(){
 Phone p(false,true);auto& f=p.f;const uint8_t d=PseChannel*2; // The actor initiates the mux.
 CHECK(p.port.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",100,0)==Status::Ok);
 Bytes connect=p.await(d,1500);
 CHECK(p.connects[1]==1&&p.sdp_requests>1&&p.connects[3]==1&&std::count(p.phone_disconnects.begin(),p.phone_disconnects.end(),0x95)==1);
 CHECK(f.host.obex_sdp_cid_==0&&f.host.obex_rfcomm_cid_==p.rfcomm.cid()&&p.rfcomm.mux().initiator&&connect.size()==26&&connect[0]==0x80);
 p.send(d,build(0xa0,{{0x10,0,0x04,0x00},{0xcb,0,0,0,5}}));
 Bytes get=p.await(d);CHECK(get[0]==0x83&&get[3]==0xcb&&get[7]==5);
 std::string book;for(int i=0;i<25;++i)book+="BEGIN:VCARD\r\nVERSION:2.1\r\nFN:Contact "+std::to_string(i)+"\r\nEND:VCARD\r\n";
 p.send(d,build(0x90,{hbytes(0x48,book.substr(0,1000))}));CHECK(p.await(d)==Bytes({0x83,0,3}));
 p.send(d,build(0xa0,{hbytes(0x49,book.substr(1000))}));Bytes disc=p.await(d);CHECK(disc[0]==0x81);
 p.send(d,build(0xa0,{}));p.ticks(40);
 CHECK(p.ev.pb_ends==1&&p.ev.result.ok&&p.ev.phonebook==book&&p.closed.count(d));
 // The initiated mux retires once idle: the actor disconnects its PSM 3 channel.
 CHECK(std::count(p.phone_disconnects.begin(),p.phone_disconnects.end(),0x96)==1&&p.rfcomm.cid()==0&&f.host.obex_rfcomm_cid_==0);
 CHECK(p.port.stats().pbap_state==PbapState::Idle&&p.port.stats().pse_channel==PseChannel);
 // Second pull: cached PSE, no second SDP channel; link encryption loss fails it.
 const unsigned before=p.connects[1];p.inbox.clear();p.closed.clear();
 CHECK(p.port.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",100,0)==Status::Ok);
 p.await(d,1500);CHECK(p.connects[1]==before&&p.connects[3]==2);
 f.event(8,{0,0x25,0,0});f.settle();p.events();CHECK(p.ev.pb_ends==2&&!p.ev.result.ok&&p.port.stats().pbap_state==PbapState::Idle);
}
void pbap_without_acl(){
 Phone p(false,true,false);CHECK(p.port.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",1,0)==Status::Ok);
 p.ticks(3);CHECK(p.ev.pb_ends==1&&!p.ev.result.ok&&p.ev.result.response==0&&p.port.stats().pbap_state==PbapState::Idle);
 for(const auto& c:p.f.host.signaling_.channels)CHECK(c.state==BTH_CHANNEL_CLOSED);
}
void map_sdp_eir_identity(){
 // MAP adds no SDP record or EIR UUID: with OPP+PBAP the actor's records,
 // UUIDs, PSM services and SDP answers are byte-identical with MAP on or off.
 std::vector<Bytes> answers;
 for(bool map:{false,true}){
  VoiceProfiles rfcomm;ObexPort obex;uint8_t ring[2048];ObexConfig c;c.opp_enabled=c.pbap_enabled=true;c.map_enabled=map;CHECK(obex.begin(c,ring,sizeof ring)&&obex.mapEnabled()==map);
  Fixture f;f.host.end();f.config.voice_control=&rfcomm;f.config.voice_config=VoiceControlConfig();f.config.voice_config.obex=&obex;
  CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();
  CHECK(f.host.classic_.record_count_==4&&f.host.gap_.config.uuid_count==3&&f.host.uuids_[1]==0x1105&&f.host.uuids_[2]==0x112e);
  f.classicConnect();CHECK(f.host.signaling_.service_count==4);const uint16_t sdp=f.channel(1,0x81);
  const Bytes all=sdpExchange(f,sdp,0x81,everything);CHECK(all==reference({obex.records()[0],obex.records()[1]}));
  for(uint16_t uuid:{uint16_t(0x1132),uint16_t(0x1133),uint16_t(0x1134)})CHECK(sdpExchange(f,sdp,0x81,search(uuid))==Bytes({3,0,1,0,5,0,0,0,0,0}));
  answers.push_back(all);f.host.end();
 }
 CHECK(answers.size()==2&&answers[0]==answers[1]);
 // MAP alone: the RFCOMM owner exists for the outgoing DLC, but no record/UUID.
 Phone m(false,false,false,true);auto& f=m.f;
 CHECK(m.port.recordCount()==0&&f.host.classic_.record_count_==2&&f.host.gap_.config.uuid_count==1&&f.host.uuids_[0]==0x110b);
 f.classicConnect();const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference({}));
 for(uint16_t uuid:{uint16_t(0x1132),uint16_t(0x1133),uint16_t(0x1134),uint16_t(0x112e)})CHECK(sdpExchange(f,sdp,0x81,search(uuid))==Bytes({3,0,1,0,5,0,0,0,0,0}));
 // Idle MAP opens no outgoing channel.
 for(unsigned i=0;i<50;++i){f.poll(100);while(!f.tx.empty())f.acknowledge(f.pop());}f.settle();
 CHECK(f.host.obex_sdp_cid_==0&&f.host.obex_rfcomm_cid_==0);for(const auto& c:f.host.signaling_.channels)CHECK(c.state==BTH_CHANNEL_CLOSED||c.psm==1||c.psm==0x17);
}
void map_through_actor(){
 Phone p(false,false,true,true);auto& f=p.f;const uint8_t d=MasChannel*2; // The actor initiates the mux.
 CHECK(p.port.request(MapOperation::GetMessageListing,"telecom/msg/inbox",50,0,32)==Status::Ok);
 Bytes connect=p.await(d,1500);
 CHECK(p.connects[1]==1&&p.sdp_requests>1&&p.connects[3]==1&&std::count(p.phone_disconnects.begin(),p.phone_disconnects.end(),0x95)==1);
 CHECK(f.host.obex_sdp_cid_==0&&f.host.obex_rfcomm_cid_==p.rfcomm.cid()&&p.rfcomm.mux().initiator);
 // CONNECT: MAS Target and (MSE publishes features) MapSupportedFeatures.
 CHECK(connect.size()==35&&connect[0]==0x80&&connect[7]==0x46&&connect[10]==0xbb&&connect[11]==0x58&&connect[26]==0x4c&&connect[29]==0x29&&connect[34]==4);
 const auto st=p.port.stats();CHECK(st.mas_channel==MasChannel&&st.mas_version==0x0104&&st.mas_message_types==0x0e&&st.mas_features==0x1f&&st.map_state==MapState::Connecting);
 p.send(d,build(0xa0,{{0x10,0,0x04,0x00},{0xcb,0,0,0,6}}));
 for(const char* folder:{"telecom","msg","inbox"}){Bytes sp=p.await(d);CHECK(sp[0]==0x85&&sp[3]==0x02&&sp[5]==0xcb&&sp[9]==6);
  const Bytes name=text16(folder);CHECK(Bytes(sp.begin()+10,sp.end())==name);p.send(d,build(0xa0,{}));}
 Bytes get=p.await(d);CHECK(get[0]==0x83&&get[3]==0xcb&&get[7]==6);
 const Bytes params={0x4c,0,14,0x01,2,0,50,0x02,2,0,0,0x13,1,32};CHECK(std::search(get.begin(),get.end(),params.begin(),params.end())!=get.end());
 std::string xml="<MAP-msg-listing version=\"1.0\">";for(int i=0;i<20;++i)xml+="<msg handle=\"0400000000000"+std::to_string(100+i)+"\" subject=\"Hello "+std::to_string(i)+"\" type=\"SMS_GSM\" read=\"no\"/>";xml+="</MAP-msg-listing>";
 const Bytes app={0x4c,0,10,0x12,2,0,20,0x0d,1,0};
 Bytes first=build(0x90,{app,hbytes(0x48,xml.substr(0,900))});p.send(d,first);CHECK(p.await(d)==Bytes({0x83,0,3}));
 p.send(d,build(0xa0,{hbytes(0x49,xml.substr(900))}));Bytes disc=p.await(d);CHECK(disc[0]==0x81&&disc[3]==0xcb);
 p.send(d,build(0xa0,{}));p.ticks(40);
 CHECK(p.ev.map_ends==1&&p.ev.map_result.ok&&p.ev.map_result.listing_size==20&&p.ev.map_result.new_message==0&&p.ev.map==xml&&p.closed.count(d)&&p.ev.pb_ends==0);
 // The initiated mux retires once idle: the actor disconnects its PSM 3 channel.
 CHECK(std::count(p.phone_disconnects.begin(),p.phone_disconnects.end(),0x96)==1&&p.rfcomm.cid()==0&&f.host.obex_rfcomm_cid_==0);
 CHECK(p.port.stats().map_state==MapState::Idle&&p.port.stats().map_completed==1&&p.port.stats().mas_channel==MasChannel);
 // Second operation: cached MAS, no second SDP channel; ACL loss fails it.
 const unsigned before=p.connects[1];p.inbox.clear();p.closed.clear();
 CHECK(p.port.request(MapOperation::GetMessage,"0400000000000100",0,0,0)==Status::Ok);
 p.await(d,1500);CHECK(p.connects[1]==before&&p.connects[3]==2);p.send(d,build(0xa0,{{0x10,0,0x04,0x00},{0xcb,0,0,0,7}}));
 get=p.await(d);CHECK(get[0]==0x83&&get[3]==0xcb&&get[7]==7);
 p.send(d,build(0x90,{hbytes(0x48,"BEGIN:BMSG\r\n")}));CHECK(p.await(d)==Bytes({0x83,0,3}));
 f.event(5,{0,0x25,0,0x13});f.settle();p.events();
 CHECK(p.ev.map_ends==2&&!p.ev.map_result.ok&&p.ev.map_result.response==0&&p.ev.map_result.operation==MapOperation::GetMessage&&p.ev.map==xml+"BEGIN:BMSG\r\n");
 CHECK(p.port.stats().map_state==MapState::Idle&&p.port.stats().map_failed==1&&p.port.stats().mas_channel==0&&p.rfcomm.cid()==0); // A new ACL owner looks the MAS up again.
}
void map_without_acl(){
 Phone p(false,false,false,true);CHECK(p.port.request(MapOperation::GetFolderListing,"telecom/msg",10,0,0)==Status::Ok);
 p.ticks(3);CHECK(p.ev.map_ends==1&&!p.ev.map_result.ok&&p.ev.map_result.response==0&&p.port.stats().map_state==MapState::Idle);
 for(const auto& c:p.f.host.signaling_.channels)CHECK(c.state==BTH_CHANNEL_CLOSED);
}
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);default_disabled_identical();enabled_sdp_and_eir();opp_through_actor();pbap_through_actor();pbap_without_acl();
 const unsigned before=checks;map_sdp_eir_identity();map_through_actor();map_without_acl();
 printf("MAP_MCE_ACTOR_PASS checks=%u (MAP on/off SDP+EIR identity; actor-opened SDP/RFCOMM to a scripted MSE; ACL loss)\n",checks-before);
 printf("OBEX_ACTOR_SDP_EIR_OPP_PBAP_PASS checks=%u (synthetic HCI; scripted phone; no radio)\n",checks);}
