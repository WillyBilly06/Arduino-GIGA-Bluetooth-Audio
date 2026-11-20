// Real OwnedHost actor + SPP: SDP/EIR/L2CAP admission, security gating,
// actor data path and lifecycle. Synthetic HCI fixture; no radio or phone.
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#include "../src/owned/OwnedHost.cpp"
#define main original_host_fixture_main
#include "test_owned_host.cpp"
#undef main
static Bytes sdpExchange(Fixture& f,uint16_t cid,uint16_t remote,const Bytes& request){
 f.output.clear();f.acl(0x25,cid,request);f.settle();for(const auto& p:f.output)if(p.cid==remote)return p.bytes;CHECK(false);return {};
}
// ServiceSearchAttribute(public browse root, all attributes): the complete
// advertised database in one response.
static const Bytes everything={6,0,9,0,15,0x35,3,0x19,0x10,0x02,0xff,0xff,0x35,5,0x0a,0,0,0xff,0xff,0};
static const Bytes serialSearch={2,0,1,0,8,0x35,3,0x19,0x11,0x01,0,4,0};
static Bytes reference(const bth_sdp_record_t* extra){
 size_t count=0;const auto* speaker=bth_sdp_speaker_records(&count);CHECK(count==3);
 bth_sdp_record_t records[3]={speaker[0],speaker[2]};size_t n=2;if(extra)records[n++]=*extra; // Fixture: no volume callback.
 bth_sdp_t server{};CHECK(bth_sdp_init(&server,records,n,77));bth_sdp_cursor_t cursor{};uint8_t out[672];size_t written=0;
 CHECK(bth_sdp_process_compact(&server,&cursor,everything.data(),everything.size(),out,sizeof out,&written)==BTH_OK);return Bytes(out,out+written);
}
static bool psm3Refused(Fixture& f){
 f.output.clear();f.acl(0x25,1,{2,7,4,0,3,0,0x90,0});f.settle();
 for(const auto& p:f.output)if(p.cid==1)for(size_t at=0;at+4<=p.bytes.size();at+=size_t(bth_read_le16(p.bytes.data()+at+2))+4)
  if(p.bytes[at]==3&&p.bytes.size()>=at+12)return bth_read_le16(p.bytes.data()+at+8)!=0&&bth_read_le16(p.bytes.data()+at+8)!=1;
 return false;
}
struct SppPeer {
 VoiceProfiles rfcomm;SppPort port;uint8_t rx[2048]={},tx[2048]={};Fixture f;bth_rfcomm_t peer{};uint16_t cid=0;uint8_t reply[600]{};size_t read=0;std::string got;uint32_t peer_generation=19;
 explicit SppPeer(bool connect=true){
  CHECK(port.begin("GIGA Serial",3,rx,sizeof rx,tx,sizeof tx,4));
  f.host.end();f.config.voice_control=&rfcomm;f.config.voice_config=VoiceControlConfig();f.config.voice_config.serial=&port;
  CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();if(connect){f.classicConnect();open();}
 }
 ~SppPeer(){f.host.end();}
 void send(const uint8_t* p,size_t n){if(n)f.acl(0x25,cid,Bytes(p,p+n));}
 void tick(){
  f.poll();unsigned budget=8;while(budget--&&!f.tx.empty())f.acknowledge(f.pop());
  while(read<f.output.size()){const auto p=f.output[read++];if(p.cid!=0x90)continue;size_t n=0;bth_rfcomm_event_t event{};
   CHECK(bth_rfcomm_receive(&peer,peer_generation,f.now,p.bytes.data(),p.bytes.size(),reply,sizeof reply,&n,&event)>=BTH_OK);
   if(event.type==BTH_RFCOMM_DATA){CHECK(event.dlci==6);got.append(reinterpret_cast<const char*>(event.payload.data),event.payload.len);CHECK(bth_rfcomm_release(&peer,peer_generation,6,1)==BTH_OK);}send(reply,n);
  }
  size_t n=0;CHECK(bth_rfcomm_next(&peer,peer_generation,f.now,reply,sizeof reply,&n)>=BTH_OK);send(reply,n);
 }
 void ticks(unsigned count=30){for(unsigned i=0;i<count;++i)tick();}
 void open(){
  cid=f.channel(3,0x90);CHECK(cid&&rfcomm.cid()==cid);f.output.clear();read=0;got.clear();CHECK(bth_rfcomm_init(&peer,peer_generation,true,0,512,7));
  size_t n=0;CHECK(bth_rfcomm_start(&peer,peer_generation,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);ticks(5);
  CHECK(bth_rfcomm_open(&peer,peer_generation,3,true,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
  for(unsigned i=0;i<100&&!(port.connected()&&bth_rfcomm_ready(&peer,peer_generation,6));++i)tick();ticks(5);
  CHECK(port.connected()&&bth_rfcomm_ready(&peer,peer_generation,6));
 }
 void peerSend(const std::string& s){size_t n=0;CHECK(bth_rfcomm_send(&peer,peer_generation,6,reinterpret_cast<const uint8_t*>(s.data()),s.size(),reply,sizeof reply,&n)==BTH_OK);send(reply,n);}
 std::string drainPort(){std::string out;uint8_t b[300];for(size_t n;(n=port.read(b,sizeof b));)out.append(reinterpret_cast<const char*>(b),n);return out;}
};
static void default_disabled_identical(){
 Fixture f;f.boot();f.classicConnect();
 CHECK(f.host.classic_.record_count_==2&&f.host.gap_.config.uuid_count==1&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x110c);
 CHECK(f.host.signaling_.service_count==3&&psm3Refused(f));
 const uint16_t sdp=f.channel(1,0x81);const auto all=sdpExchange(f,sdp,0x81,everything);CHECK(all==reference(nullptr));
 const auto serial=sdpExchange(f,sdp,0x81,serialSearch);CHECK(serial==Bytes({3,0,1,0,5,0,0,0,0,0}));
}
static void enabled_sdp_and_eir(){
 SppPeer p(false);auto& f=p.f;
 CHECK(f.host.classic_.record_count_==3&&f.host.gap_.config.uuid_count==2&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x1101);
 f.classicConnect();CHECK(f.host.signaling_.service_count==4);
 const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference(p.port.record()));
 const auto serial=sdpExchange(f,sdp,0x81,serialSearch);CHECK(serial.size()==14&&serial[8]==1&&serial[9]==0&&serial[10]==1&&serial[11]==0&&serial[12]==6);
}
static void install_capacity(){
 // Begin rejects an SPP channel that collides with an enabled voice listener
 // and leaves the owner reusable.
 VoiceProfiles rfcomm;SppPort port;uint8_t rx[512],tx[512];CHECK(port.begin("S",1,rx,512,tx,512,0));
 Fixture f;f.host.end();f.config.voice_control=&rfcomm;f.config.voice_config.hands_free=true;f.config.voice_config.serial=&port;
 CHECK(f.host.begin(f.config,f.hooks)==Status::InvalidArgument&&!rfcomm.enabled());
 f.config.voice_config.hands_free_channel=4;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok&&rfcomm.enabled()&&f.host.classic_.record_count_==3);f.host.end();CHECK(!rfcomm.enabled());
 // Sink + target + controller + HF + HS + SPP + OPP + PBAP PCE is the full eight-record table.
 ClassicProfiles c;bth_control_tx_t q{};bth_control_tx_init(&q);RadioCallbacks cb;cb.configure=[](const CodecConfig&,void*){return Status::Ok;};cb.volume=[](uint8_t,void*){};
 CHECK(c.begin(codecBit(Codec::Sbc),cb,q)==Status::Ok&&c.record_count_==3);VoiceSdp voice;VoiceControlConfig vc;vc.hands_free=vc.headset=true;CHECK(voice.begin(vc,true));
 CHECK(c.installVoiceRecords(port.record(),1)==Status::Ok&&c.installVoiceRecords(voice.records(),voice.count())==Status::Ok&&c.record_count_==6);
 ObexPort obex;uint8_t ring[2048];ObexConfig oc;oc.opp_enabled=oc.pbap_enabled=true;CHECK(obex.begin(oc,ring,sizeof ring));
 CHECK(c.installVoiceRecords(obex.records(),obex.recordCount())==Status::Ok&&c.record_count_==8);
 CHECK(c.installVoiceRecords(port.record(),1)==Status::InvalidArgument);
}
static void actor_data_path(){
 SppPeer p;CHECK(p.port.stats().connections==1&&p.port.stats().frame_bytes==127&&p.port.stats().credit_flow);
 p.peerSend("hello\r\n");p.ticks(5);CHECK(p.drainPort()=="hello\r\n");
 std::string text;for(unsigned i=0;i<40;++i)text+="line "+std::to_string(i)+" of the SPP echo stream\r\n";
 size_t accepted=0;CHECK(p.port.write(reinterpret_cast<const uint8_t*>(text.data()),text.size(),accepted)==Status::Ok&&accepted==text.size());
 p.ticks(80);CHECK(p.got==text&&p.port.txPending()==0&&!p.rfcomm.closeRequested());
 // Echo from the peer side through the actor in both directions.
 std::string echoed;for(unsigned i=0;i<20;++i){const std::string chunk="ping"+std::to_string(i);p.peerSend(chunk);p.ticks(3);const auto in=p.drainPort();echoed+=in;
  CHECK(p.port.write(reinterpret_cast<const uint8_t*>(in.data()),in.size(),accepted)==Status::Ok&&accepted==in.size());}
 p.ticks(20);CHECK(p.got==text+echoed&&echoed.find("ping19")!=std::string::npos);
}
static void security_and_lifecycle(){
 SppPeer p;const auto first=p.port.stats().generation;size_t accepted=0;
 // ACL loss: actor resets the RFCOMM owner and the port refuses new data.
 p.f.event(5,{0,0x25,0,0x13});p.f.settle();CHECK(!p.port.connected()&&p.rfcomm.cid()==0);
 CHECK(p.port.write(reinterpret_cast<const uint8_t*>("x"),1,accepted)==Status::InvalidArgument);
 p.f.classicConnect();p.peer_generation=21;p.open();CHECK(p.port.stats().generation!=first&&p.port.stats().connections==2);
 p.peerSend("again");p.ticks(5);CHECK(p.drainPort()=="again");
 // Encryption off: the Classic owner closes the peer; nothing remains admitted.
 p.f.event(8,{0,0x25,0,0});p.f.settle();CHECK(!p.port.connected()&&p.rfcomm.closeRequested());
 CHECK(p.port.write(reinterpret_cast<const uint8_t*>("x"),1,accepted)==Status::InvalidArgument);
}
static void streaming_pacing_wiring(){
 SppPeer p;p.tick();CHECK(p.rfcomm.bulk_interval_==0);
 p.f.host.classic_.playing_=true;p.tick();CHECK(p.rfcomm.bulk_interval_==4);
 p.f.host.classic_.playing_=false;p.tick();CHECK(p.rfcomm.bulk_interval_==0);
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);default_disabled_identical();enabled_sdp_and_eir();install_capacity();actor_data_path();security_and_lifecycle();streaming_pacing_wiring();
 printf("SPP_ACTOR_SDP_EIR_SECURITY_DATA_PASS checks=%u (synthetic HCI; no radio)\n",checks);}
