// OBEX Object Push server, PBAP client and MAP client over the real shared
// RFCOMM owner (VoiceProfiles), the real RFCOMM kernel on both ends, the real
// Classic control FIFO and the real SDP server answering the PCE/MCE lookup.
// The peer is a scripted OPP client / PBAP PSE / MAP MSE (exact bytes).
// Synthetic only: no controller, radio or phone.
#include "../src/owned/VoiceProfiles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
using namespace giga_bredr;using namespace giga_bredr::detail;
namespace {
unsigned checks;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"OBEX PROFILES CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Bytes=std::vector<uint8_t>;
constexpr uint8_t PseChannel=19,OppChannel=4,MasChannel=17;
const uint8_t PbapTarget[16]={0x79,0x61,0x35,0xf0,0xf0,0xc5,0x11,0xd8,0x09,0x66,0x08,0x00,0x20,0x0c,0x9a,0x66};
const uint8_t MasTarget[16]={0xbb,0x58,0x2b,0x40,0x42,0x0c,0x11,0xdb,0xb0,0xde,0x08,0x00,0x20,0x0c,0x9a,0x66};
std::string pattern(size_t n,unsigned seed){std::string s(n,'\0');for(size_t i=0;i<n;++i)s[i]=char((i*131u+seed*7u+(i>>8))&0xff);return s;}
Bytes text16(const char* s){Bytes out;for(;*s;++s){out.push_back(0);out.push_back(uint8_t(*s));}out.push_back(0);out.push_back(0);return out;}
struct Packet {uint8_t code=0;Bytes bytes;bth_span_t headers{};};
Bytes build(uint8_t opcode,std::initializer_list<Bytes> parts){Bytes p={opcode,0,0};for(const auto& part:parts)p.insert(p.end(),part.begin(),part.end());p[1]=uint8_t(p.size()>>8);p[2]=uint8_t(p.size());return p;}
Bytes hbytes(uint8_t id,const Bytes& v){Bytes h={id,uint8_t((v.size()+3)>>8),uint8_t(v.size()+3)};h.insert(h.end(),v.begin(),v.end());return h;}
Bytes hbytes(uint8_t id,const std::string& v){return hbytes(id,Bytes(v.begin(),v.end()));}
Bytes hu32(uint8_t id,uint32_t v){return {id,uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8),uint8_t(v)};}
const uint8_t pse_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x09, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x2f,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,PseChannel,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02, 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x30,0x09,0x01,0x02,
 0x09,0x01,0x00,0x25,0x03,'P','S','E'};
struct Events {
 std::string name,type,data,phonebook,map;uint32_t length=0;unsigned begins=0,ends=0,pb_ends=0,map_ends=0;bool ok=false;PbapResult result;MapResult map_result;
};
struct Harness {
 VoiceProfiles voice;ObexPort obex;std::vector<uint8_t> ring;bth_control_tx_t queue{};bth_hci_flow_t flow{};bth_rfcomm_t peer{};
 ObexConfig config;Events ev;uint32_t now=100;uint8_t wire[1100]{},reply[600]{};
 std::deque<Bytes> to_voice;std::map<uint8_t,Bytes> inbox;std::set<uint8_t> peer_closed,peer_opened;bool deliver=true,we_initiate=false;uint8_t server=PseChannel;
 // server: the phone's PSE/MAS RFCOMM channel (the scripted peer listens there).
 Harness(bool opp,bool pbap,size_t ring_size=4096,bool initiate=false,bool map=false,uint8_t server_channel=PseChannel):ring(ring_size),we_initiate(initiate),server(server_channel){
  bth_control_tx_init(&queue);REQUIRE(bth_control_tx_attach(&queue,0,0x25,7));
  bth_hci_flow_init(&flow);REQUIRE(bth_hci_flow_set_buffers(&flow,1021,8,251,4));REQUIRE(bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC));
  config.opp_enabled=opp;config.pbap_enabled=pbap;config.opp_channel=OppChannel;config.user=&ev;
  config.on_object_begin=[](const char* n,const char* t,uint32_t l,void* u){auto& e=*static_cast<Events*>(u);e.name=n;e.type=t;e.length=l;e.data.clear();++e.begins;};
  config.on_object_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->data.append(reinterpret_cast<const char*>(p),n);};
  config.on_object_end=[](bool ok,void* u){auto& e=*static_cast<Events*>(u);e.ok=ok;++e.ends;};
  config.on_phonebook_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->phonebook.append(reinterpret_cast<const char*>(p),n);};
  config.on_phonebook_end=[](const PbapResult& r,void* u){auto& e=*static_cast<Events*>(u);e.result=r;++e.pb_ends;};
  config.map_enabled=map;
  config.on_map_data=[](const uint8_t* p,size_t n,void* u){static_cast<Events*>(u)->map.append(reinterpret_cast<const char*>(p),n);};
  config.on_map_end=[](const MapResult& r,void* u){auto& e=*static_cast<Events*>(u);e.map_result=r;++e.map_ends;};
  REQUIRE(obex.begin(config,ring.data(),ring.size()));
  VoiceControlConfig c;c.obex=&obex;REQUIRE(voice.begin(c,queue)==Status::Ok);voice.reset(7);
  REQUIRE(voice.channelOpened(7,0x40,0x80,672,initiate));
  // The peer is the phone: initiator of a mux it opened, otherwise responder.
  REQUIRE(bth_rfcomm_init(&peer,19,!initiate,server,512,7));
 }
 void complete(){const uint8_t p[]={4,0x13,5,1,0x25,0,1,0};bth_h4_frame_t f{};REQUIRE(bth_h4_decode(p,sizeof p,&f)==BTH_OK);REQUIRE(bth_hci_flow_on_event(&flow,&f)==BTH_OK);}
 const bth_rfcomm_dlc_t* peerDlc(uint8_t dlci)const{for(const auto& d:peer.dlc)if(d.state&&d.dlci==dlci)return &d;return nullptr;}
 void drain(){
  size_t n=0;const uint32_t owners[]={7,0};
  while(bth_control_tx_next(&queue,owners,&flow,wire,sizeof wire,&n)==BTH_OK){
   REQUIRE(n>=9&&wire[0]==2);if(bth_read_le16(wire+7)!=0x80){complete();continue;}
   size_t rn=0;bth_rfcomm_event_t e{};REQUIRE(bth_rfcomm_receive(&peer,19,now,wire+9,n-9,reply,sizeof reply,&rn,&e)>=BTH_OK);
   if(e.type==BTH_RFCOMM_DATA){inbox[e.dlci].insert(inbox[e.dlci].end(),e.payload.data,e.payload.data+e.payload.len);if(peerDlc(e.dlci)->credit)REQUIRE(bth_rfcomm_release(&peer,19,e.dlci,1)==BTH_OK);}
   if(e.type==BTH_RFCOMM_DLC_CLOSED)peer_closed.insert(e.dlci);
   if(e.type==BTH_RFCOMM_DLC_OPENED)peer_opened.insert(e.dlci);
   if(rn)to_voice.emplace_back(reply,reply+rn);complete();
  }
 }
 void feed(){while(!to_voice.empty()&&voice.receive(0x40,to_voice.front().data(),to_voice.front().size(),true,now))to_voice.pop_front();}
 void events(){if(!deliver)return;uint8_t kind,data[ObexPort::ChunkBytes];size_t n;while(obex.pop(kind,data,sizeof data,n))ObexPort::deliver(config,kind,data,n);}
 void tick(uint32_t step=1){
  now+=step;feed();voice.poll(true,now);drain();size_t n=0;REQUIRE(bth_rfcomm_next(&peer,19,now,reply,sizeof reply,&n)>=BTH_OK);if(n)to_voice.emplace_back(reply,reply+n);feed();events();
 }
 void run(unsigned count){for(unsigned i=0;i<count;++i)tick();}
 void startMux(){size_t n=0;REQUIRE(bth_rfcomm_start(&peer,19,now,reply,sizeof reply,&n)==BTH_OK);to_voice.emplace_back(reply,reply+n);run(5);REQUIRE(peer.mux==BTH_RFCOMM_OPEN);}
 void openOpp(){
  if(peer.mux!=BTH_RFCOMM_OPEN)startMux();size_t n=0;
  REQUIRE(bth_rfcomm_open(&peer,19,OppChannel,true,now,reply,sizeof reply,&n)==BTH_OK);to_voice.emplace_back(reply,reply+n);
  for(unsigned k=0;k<40&&!bth_rfcomm_ready(&peer,19,OppChannel*2);++k)tick();REQUIRE(bth_rfcomm_ready(&peer,19,OppChannel*2));run(3);
  REQUIRE(obex.stats().opp_connected);
 }
 // Sends one OBEX packet from the peer, fragmented to its RFCOMM N1 and credits.
 void peerSend(uint8_t dlci,const Bytes& p){
  const auto* d=peerDlc(dlci);REQUIRE(d&&d->mtu);size_t at=0;unsigned guard=0;
  while(at<p.size()){const size_t k=p.size()-at<d->mtu?p.size()-at:d->mtu;size_t n=0;const auto st=bth_rfcomm_send(&peer,19,dlci,p.data()+at,k,reply,sizeof reply,&n);
   if(st==BTH_MORE){tick();REQUIRE(++guard<400);continue;}REQUIRE(st==BTH_OK);to_voice.emplace_back(reply,reply+n);at+=k;d=peerDlc(dlci);REQUIRE(d);}
 }
 bool peerPacket(uint8_t dlci,Packet& out){
  auto& b=inbox[dlci];if(b.size()<3)return false;const size_t n=size_t(b[1])<<8|b[2];if(b.size()<n)return false;
  out.bytes.assign(b.begin(),b.begin()+n);b.erase(b.begin(),b.begin()+n);out.code=out.bytes[0];return true;
 }
 Packet await(uint8_t dlci,unsigned limit=300){Packet p;for(unsigned i=0;i<limit;++i){if(peerPacket(dlci,p))return p;tick();}REQUIRE(false);return p;}
 bool quiet(uint8_t dlci,unsigned count){Packet p;for(unsigned i=0;i<count;++i){tick();if(inbox[dlci].size())return false;}return true;}
 // PCE SDP lookup through the real SDP server holding a PSE record.
 void sdpLookup(){
  REQUIRE(obex.sdpWanted());obex.sdpConnecting();REQUIRE(!obex.sdpWanted()&&obex.stats().pbap_state==PbapState::ServiceDiscovery);obex.sdpOpened(672);
  bth_sdp_record_t records[]={{0x10009,{pse_record,sizeof pse_record}}};bth_sdp_t server{};REQUIRE(bth_sdp_init(&server,records,1,3));bth_sdp_cursor_t cursor{};
  uint8_t req[128],rsp[64];size_t n=0,m=0;unsigned rounds=0;
  while(obex.sdpRequest(req,sizeof req,n)){REQUIRE(bth_sdp_process_compact(&server,&cursor,req,n,rsp,30,&m)==BTH_OK);obex.sdpReceive(rsp,m);++rounds;REQUIRE(rounds<20);}
  REQUIRE(rounds>1&&obex.sdpFinished()&&obex.stats().pse_channel==PseChannel&&obex.stats().pse_version==0x0102);obex.sdpClosed();
  REQUIRE(obex.stats().pbap_state==PbapState::Transport);
 }
 uint8_t pbapDlci()const{return uint8_t(server*2+(we_initiate?0:1));}
 // Accepts the PCE CONNECT as a PSE and returns the GET it sends next.
 Packet acceptConnect(uint32_t id=0x2a){
  const uint8_t d=pbapDlci();Packet c=await(d);bth_obex_request_t r;bth_obex_header_t h;
  REQUIRE(bth_obex_parse_request(c.bytes.data(),c.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_CONNECT&&r.version==0x10&&r.max_packet==ObexPort::PacketBytes);
  REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TARGET,&h)==BTH_OK&&h.data.len==16&&!memcmp(h.data.data,PbapTarget,16));
  REQUIRE(obex.stats().pbap_state==PbapState::Connecting);
  peerSend(d,build(0xa0,{{0x10,0,0x04,0x00},hu32(BTH_OBEX_H_CONNECTION_ID,id),hbytes(BTH_OBEX_H_WHO,Bytes(PbapTarget,PbapTarget+16))}));
  return await(d);
 }
};
void checkConnectionId(const bth_obex_request_t& r,uint32_t id){bth_span_t rest=r.headers;bth_obex_header_t h;REQUIRE(bth_obex_header_next(&rest,&h)==BTH_OK&&h.id==BTH_OBEX_H_CONNECTION_ID&&h.value==id);}

void opp_push_streaming(){
 Harness t(true,false);t.openOpp();const uint8_t d=OppChannel*2;
 t.peerSend(d,build(0x80,{{0x10,0,0x10,0x00}}));Packet c=t.await(d);REQUIRE(c.bytes==Bytes({0xa0,0,7,0x10,0,0x04,0x00}));
 // 5000-byte object in PUT packets of at most 1024 bytes (the advertised maximum).
 const std::string object=pattern(5000,3);const char* name="photo \xc3\xa9.jpg";
 Bytes name16={0,'p',0,'h',0,'o',0,'t',0,'o',0,' ',0,0xe9,0,'.',0,'j',0,'p',0,'g',0,0};
 size_t at=0;bool first=true;unsigned packets=0;
 while(at<object.size()){
  Bytes p={0x02,0,0};
  if(first){const Bytes n=hbytes(0x01,name16);p.insert(p.end(),n.begin(),n.end());const Bytes ty=hbytes(0x42,std::string("image/jpeg",11));p.insert(p.end(),ty.begin(),ty.end());const Bytes l=hu32(0xc3,5000);p.insert(p.end(),l.begin(),l.end());first=false;}
  const size_t room=1024-p.size()-3,k=object.size()-at<room?object.size()-at:room;const bool last=at+k==object.size();
  const Bytes body=hbytes(last?0x49:0x48,object.substr(at,k));p.insert(p.end(),body.begin(),body.end());if(last)p[0]=0x82;p[1]=uint8_t(p.size()>>8);p[2]=uint8_t(p.size());
  REQUIRE(p.size()<=1024);t.peerSend(d,p);const Packet r=t.await(d);REQUIRE(r.bytes==Bytes({uint8_t(last?0xa0:0x90),0,3}));at+=k;++packets;
 }
 t.run(5);REQUIRE(packets==5&&t.ev.begins==1&&t.ev.name==name&&t.ev.type=="image/jpeg"&&t.ev.length==5000&&t.ev.data==object&&t.ev.ends==1&&t.ev.ok);
 // Second object: vCard, single final packet, no Length header.
 const std::string card="BEGIN:VCARD\r\nVERSION:2.1\r\nN:Doe;Jane\r\nFN:Jane Doe\r\nEND:VCARD\r\n";
 t.peerSend(d,build(0x82,{hbytes(0x01,text16("jane.vcf")),hbytes(0x42,std::string("text/x-vcard",13)),hbytes(0x49,card)}));REQUIRE(t.await(d).bytes==Bytes({0xa0,0,3}));
 t.run(3);REQUIRE(t.ev.begins==2&&t.ev.name=="jane.vcf"&&t.ev.type=="text/x-vcard"&&t.ev.length==ObexUnknownLength&&t.ev.data==card&&t.ev.ends==2&&t.ev.ok);
 const auto s=t.obex.stats();REQUIRE(s.objects_completed==2&&s.objects_failed==0&&s.object_bytes==5000+card.size()&&s.event_bytes_pending==0);
 t.peerSend(d,build(0x81,{}));REQUIRE(t.await(d).bytes==Bytes({0xa0,0,3}));
 size_t n=0;REQUIRE(bth_rfcomm_disconnect(&t.peer,19,d,t.now,t.reply,sizeof t.reply,&n)==BTH_OK);t.to_voice.emplace_back(t.reply,t.reply+n);t.run(5);
 REQUIRE(!t.obex.stats().opp_connected&&!t.voice.closeRequested());
}
void opp_backpressure(){
 // A 2048-byte event ring with loop() not draining: the server withholds the
 // CONTINUE (so the peer cannot send) instead of dropping or buffering more.
 Harness t(true,false,2048);t.openOpp();const uint8_t d=OppChannel*2;t.deliver=false;
 t.peerSend(d,build(0x80,{{0x10,0,0x04,0x00}}));t.await(d);
 const std::string a=pattern(990,1),b=pattern(1000,2),c=pattern(500,3);
 t.peerSend(d,build(0x02,{hbytes(0x01,text16("big.bin")),hbytes(0x48,a)}));REQUIRE(t.await(d).bytes==Bytes({0x90,0,3}));
 t.peerSend(d,build(0x02,{hbytes(0x48,b)}));REQUIRE(t.quiet(d,200));REQUIRE(t.obex.stats().event_bytes_pending>1000);
 t.deliver=true;REQUIRE(t.await(d).bytes==Bytes({0x90,0,3}));
 t.peerSend(d,build(0x82,{hbytes(0x49,c)}));REQUIRE(t.await(d).bytes==Bytes({0xa0,0,3}));t.run(3);
 REQUIRE(t.ev.ends==1&&t.ev.ok&&t.ev.data==a+b+c&&t.ev.name=="big.bin"&&t.ev.length==ObexUnknownLength);
}
void opp_errors(){
 Harness t(true,false);t.openOpp();const uint8_t d=OppChannel*2;
 // A directed CONNECT (e.g. FTP Target) is another service.
 t.peerSend(d,build(0x80,{{0x10,0,0x04,0x00},hbytes(0x46,Bytes(16,0xf9))}));REQUIRE(t.await(d).bytes==Bytes({0xd3,0,7,0x10,0,0x04,0x00}));
 t.peerSend(d,build(0x80,{{0x10,0,0x04,0x00}}));REQUIRE(t.await(d).bytes[0]==0xa0);
 // Delete request (final PUT without body), GET and SETPATH are refused.
 t.peerSend(d,build(0x82,{hbytes(0x01,text16("x"))}));REQUIRE(t.await(d).bytes==Bytes({0xc3,0,3}));
 t.peerSend(d,build(0x83,{hbytes(0x42,std::string("text/x-vcard",13))}));REQUIRE(t.await(d).bytes==Bytes({0xd1,0,3}));
 t.peerSend(d,build(0x85,{{0x02,0}}));REQUIRE(t.await(d).bytes==Bytes({0xd1,0,3}));
 REQUIRE(t.ev.begins==0&&t.obex.stats().objects_rejected==1);
 // ABORT mid-object: one failed end, then a fresh object succeeds.
 t.peerSend(d,build(0x02,{hbytes(0x01,text16("a.txt")),hbytes(0x48,std::string("part"))}));REQUIRE(t.await(d).bytes[0]==0x90);
 t.peerSend(d,build(0xff,{}));REQUIRE(t.await(d).bytes==Bytes({0xa0,0,3}));t.run(2);REQUIRE(t.ev.begins==1&&t.ev.ends==1&&!t.ev.ok);
 t.peerSend(d,build(0x82,{hbytes(0x01,text16("b.txt")),hbytes(0x49,std::string("whole"))}));REQUIRE(t.await(d).bytes[0]==0xa0);t.run(2);REQUIRE(t.ev.ends==2&&t.ev.ok&&t.ev.data=="whole");
 // Correctly framed but malformed headers: Bad Request; the open object fails.
 t.peerSend(d,build(0x02,{hbytes(0x01,text16("c.txt")),hbytes(0x48,std::string("1"))}));t.await(d);
 t.peerSend(d,build(0x02,{{0x48,0,9,1}}));REQUIRE(t.await(d).bytes==Bytes({0xc0,0,3}));t.run(2);REQUIRE(t.ev.ends==3&&!t.ev.ok&&t.obex.stats().protocol_errors==1);
 // Declared length above the negotiated maximum: the DLC is closed.
 t.peerSend(d,build(0x02,{hbytes(0x01,text16("d.txt")),hbytes(0x48,std::string("2"))}));t.await(d);
 Bytes huge={0x02,0x07,0xd0,0x48,0x07,0xcd};huge.resize(127,0x55);t.peerSend(d,huge);t.run(20);
 REQUIRE(t.peer_closed.count(d)&&!t.obex.stats().opp_connected&&t.obex.stats().protocol_errors==2&&t.ev.ends==4&&!t.ev.ok&&!t.voice.closeRequested());
 // Reconnect, then the peer drops the DLC mid-object.
 t.peer_closed.clear();t.openOpp();t.peerSend(d,build(0x02,{hbytes(0x01,text16("e.txt")),hbytes(0x48,std::string("3"))}));t.await(d);
 size_t n=0;REQUIRE(bth_rfcomm_disconnect(&t.peer,19,d,t.now,t.reply,sizeof t.reply,&n)==BTH_OK);t.to_voice.emplace_back(t.reply,t.reply+n);t.run(5);
 REQUIRE(t.ev.begins==5&&t.ev.ends==5&&!t.ev.ok&&t.obex.stats().objects_failed==4);
}
void pbap_pull(bool initiate){
 Harness t(false,true,4096,initiate);if(!initiate)t.startMux();const uint8_t d=t.pbapDlci();
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",500,0)==Status::Ok);
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",500,0)==Status::Busy);
 t.obex.service(t.now);t.sdpLookup();
 Packet g=t.acceptConnect();bth_obex_request_t r;bth_obex_header_t h;
 REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET&&r.final);checkConnectionId(r,0x2a);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&Bytes(h.data.data,h.data.data+h.data.len)==text16("telecom/pb.vcf"));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TYPE,&h)==BTH_OK&&Bytes(h.data.data,h.data.data+h.data.len)==Bytes({'x','-','b','t','/','p','h','o','n','e','b','o','o','k',0}));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&Bytes(h.data.data,h.data.data+h.data.len)==Bytes({0x04,2,0x01,0xf4,0x05,2,0,0,0x07,1,0}));
 REQUIRE(t.obex.stats().pbap_state==PbapState::Transferring);
 std::string book;for(int i=0;i<30;++i)book+="BEGIN:VCARD\r\nVERSION:2.1\r\nN:Person"+std::to_string(i)+";Test\r\nTEL;CELL:+1555000"+std::to_string(100+i)+"\r\nEND:VCARD\r\n";
 // PSE responses up to the PCE maximum (1024), split by RFCOMM N1.
 size_t at=0;unsigned rounds=0;
 while(true){
  const size_t room=1024-3-8-3,k=book.size()-at<room?book.size()-at:room;const bool last=at+k==book.size();
  const Bytes app={0x08,2,0x00,30};
  t.peerSend(d,build(last?0xa0:0x90,{hbytes(0x4c,app),hbytes(last?0x49:0x48,book.substr(at,k))}));at+=k;++rounds;
  if(last)break;REQUIRE(t.await(d).bytes==Bytes({0x83,0,3})); // Continuation GET.
 }
 Packet disc=t.await(d);REQUIRE(bth_obex_parse_request(disc.bytes.data(),disc.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_DISCONNECT);checkConnectionId(r,0x2a);
 t.run(3);REQUIRE(t.ev.pb_ends==1&&t.ev.result.ok&&t.ev.result.response==0xa0&&t.ev.result.phonebook_size==30&&t.ev.result.bytes==book.size()&&t.ev.phonebook==book&&rounds>1);
 REQUIRE(t.obex.stats().pbap_state==PbapState::Closing);
 t.peerSend(d,build(0xa0,{}));t.run(10);REQUIRE(t.peer_closed.count(d)&&t.obex.stats().pbap_state==PbapState::Idle&&t.obex.stats().pbap_completed==1);
 if(initiate){REQUIRE(t.voice.closeRequested());return;} // The idle initiated mux retires its L2CAP channel.
 REQUIRE(!t.voice.closeRequested());
 // Cached PSE channel: the next pull skips SDP and reuses the phone's mux.
 t.peer_closed.clear();REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",0,0)==Status::Ok);t.obex.service(t.now);REQUIRE(!t.obex.sdpWanted());
 g=t.acceptConnect(0x31);REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET);checkConnectionId(r,0x31);
 t.peerSend(d,build(0xa0,{hbytes(0x4c,Bytes({0x08,2,0x01,0x02}))}));t.await(d);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.pb_ends==2&&t.ev.result.ok&&t.ev.result.phonebook_size==0x102&&t.ev.result.bytes==0&&t.peer_closed.count(d));
}
void pbap_failures(){
 Harness t(false,true);t.startMux();const uint8_t d=t.pbapDlci();
 // CONNECT refused: failure with the PSE code; no OBEX DISCONNECT, DLC closed.
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",10,0)==Status::Ok);t.obex.service(t.now);t.sdpLookup();
 t.await(d);t.peerSend(d,build(0xc3,{}));t.run(15);
 REQUIRE(t.ev.pb_ends==1&&!t.ev.result.ok&&t.ev.result.response==0xc3&&t.peer_closed.count(d)&&t.inbox[d].empty()&&t.obex.stats().pbap_state==PbapState::Idle);
 // Abort after the first body packet: ABORT, then DISCONNECT.
 t.peer_closed.clear();REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",10,0)==Status::Ok);t.obex.service(t.now);
 t.acceptConnect(7);REQUIRE(t.obex.abort()==Status::Ok);t.peerSend(d,build(0x90,{hbytes(0x48,std::string("BEGIN:VCARD\r\n"))}));
 Packet a=t.await(d);bth_obex_request_t r;REQUIRE(bth_obex_parse_request(a.bytes.data(),a.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_ABORT);checkConnectionId(r,7);
 t.peerSend(d,build(0xa0,{}));a=t.await(d);REQUIRE(a.bytes[0]==0x81);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.pb_ends==2&&!t.ev.result.ok&&t.ev.result.aborted&&t.ev.phonebook=="BEGIN:VCARD\r\n"&&t.peer_closed.count(d));
 REQUIRE(t.obex.abort()==Status::InvalidArgument);
 // Not Found from GET, then DISCONNECT.
 t.peer_closed.clear();t.ev.phonebook.clear();REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/missing.vcf",10,0)==Status::Ok);t.obex.service(t.now);
 t.acceptConnect();t.peerSend(d,build(0xc4,{}));REQUIRE(t.await(d).bytes[0]==0x81);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.pb_ends==3&&!t.ev.result.ok&&t.ev.result.response==0xc4&&t.peer_closed.count(d));
 // Silent PSE: the response deadline fails the pull and closes the DLC.
 t.peer_closed.clear();REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",10,0)==Status::Ok);t.obex.service(t.now);
 t.acceptConnect();t.tick(ObexPort::ResponseTimeoutMs+1);t.run(10);
 REQUIRE(t.ev.pb_ends==4&&!t.ev.result.ok&&t.ev.result.response==0&&t.peer_closed.count(d)&&t.obex.stats().pbap_state==PbapState::Idle);
 // Malformed response (header overrun): protocol error, DLC closed.
 t.peer_closed.clear();REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",10,0)==Status::Ok);t.obex.service(t.now);
 t.acceptConnect();t.peerSend(d,Bytes({0x90,0,6,0x48,0,9}));t.run(15);
 REQUIRE(t.ev.pb_ends==5&&!t.ev.result.ok&&t.peer_closed.count(d)&&t.obex.stats().protocol_errors==1&&!t.voice.closeRequested());
 // Abort before OBEX (SDP stage) completes at once.
 Harness u(false,true);REQUIRE(u.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",1,0)==Status::Ok);REQUIRE(u.obex.abort()==Status::Ok);u.events();
 REQUIRE(u.ev.pb_ends==1&&u.ev.result.aborted&&u.obex.stats().pbap_state==PbapState::Idle);
 REQUIRE(u.obex.request(PbapOperation::None,"x",1,0)==Status::InvalidArgument&&u.obex.request(PbapOperation::PullPhonebook,"",1,0)==Status::InvalidArgument);
 // A PSE that is not in the phone's SDP database fails the lookup.
 REQUIRE(u.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",1,0)==Status::Ok);u.obex.service(u.now);u.obex.sdpConnecting();u.obex.sdpOpened(672);
 uint8_t req[64];size_t n=0;REQUIRE(u.obex.sdpRequest(req,sizeof req,n));const uint8_t empty[]={0x07,req[1],req[2],0x00,0x05,0x00,0x02,0x35,0x00,0x00};u.obex.sdpReceive(empty,sizeof empty);
 REQUIRE(u.obex.sdpFinished());u.obex.sdpClosed();u.events();REQUIRE(u.ev.pb_ends==2&&!u.ev.result.ok&&u.obex.stats().pbap_state==PbapState::Idle);
}
void pbap_listing(){
 Harness t(false,true);t.startMux();const uint8_t d=t.pbapDlci();
 REQUIRE(t.obex.request(PbapOperation::PullVcardListing,"/telecom/pb",20,5)==Status::Ok);t.obex.service(t.now);t.sdpLookup();
 Packet p=t.acceptConnect(9);bth_obex_request_t r;bth_obex_header_t h;
 for(const char* folder:{"telecom","pb"}){
  REQUIRE(bth_obex_parse_request(p.bytes.data(),p.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_SETPATH&&r.flags==0x02);checkConnectionId(r,9);
  REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&Bytes(h.data.data,h.data.data+h.data.len)==text16(folder));
  t.peerSend(d,build(0xa0,{}));p=t.await(d);
 }
 REQUIRE(bth_obex_parse_request(p.bytes.data(),p.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&h.data.len==0);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TYPE,&h)==BTH_OK&&!memcmp(h.data.data,"x-bt/vcard-listing",19));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&Bytes(h.data.data,h.data.data+h.data.len)==Bytes({0x04,2,0,20,0x05,2,0,5}));
 const std::string xml="<?xml version=\"1.0\"?><vCard-listing version=\"1.0\"><card handle=\"1.vcf\" name=\"Doe;Jane\"/></vCard-listing>";
 t.peerSend(d,build(0xa0,{hbytes(0x49,xml)}));REQUIRE(t.await(d).bytes[0]==0x81);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.pb_ends==1&&t.ev.result.ok&&t.ev.result.operation==PbapOperation::PullVcardListing&&t.ev.phonebook==xml);
 // A refused SETPATH fails the listing (then OBEX DISCONNECT).
 t.peer_closed.clear();REQUIRE(t.obex.request(PbapOperation::PullVcardListing,"telecom/nope",20,0)==Status::Ok);t.obex.service(t.now);
 p=t.acceptConnect();t.peerSend(d,build(0xa0,{}));p=t.await(d);t.peerSend(d,build(0xc4,{}));REQUIRE(t.await(d).bytes[0]==0x81);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.pb_ends==2&&!t.ev.result.ok&&t.ev.result.response==0xc4&&t.peer_closed.count(d));
}
void opp_and_pbap_share_mux(){
 // Both endpoints on one phone-opened mux: an object push while a pull runs.
 Harness t(true,true);t.openOpp();const uint8_t o=OppChannel*2,d=t.pbapDlci();
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",5,0)==Status::Ok);t.obex.service(t.now);t.sdpLookup();
 t.acceptConnect();t.peerSend(o,build(0x80,{{0x10,0,0x04,0x00}}));REQUIRE(t.await(o).bytes[0]==0xa0);
 t.peerSend(o,build(0x82,{hbytes(0x01,text16("n.txt")),hbytes(0x49,std::string("note"))}));REQUIRE(t.await(o).bytes[0]==0xa0);
 t.peerSend(d,build(0xa0,{hbytes(0x49,std::string("BEGIN:VCARD\r\nEND:VCARD\r\n"))}));REQUIRE(t.await(d).bytes[0]==0x81);t.peerSend(d,build(0xa0,{}));t.run(10);
 REQUIRE(t.ev.ends==1&&t.ev.ok&&t.ev.data=="note"&&t.ev.pb_ends==1&&t.ev.result.ok&&t.peer_closed.count(d)&&!t.peer_closed.count(o));
 // Channel conflicts are rejected at begin.
 VoiceProfiles v;ObexPort x;uint8_t ring[2048];ObexConfig c;c.opp_enabled=true;c.opp_channel=1;REQUIRE(x.begin(c,ring,sizeof ring));
 bth_control_tx_t q{};bth_control_tx_init(&q);VoiceControlConfig vc;vc.hands_free=true;vc.obex=&x;REQUIRE(v.begin(vc,q)==Status::InvalidArgument);
 vc.hands_free=false;REQUIRE(v.begin(vc,q)==Status::Ok);v.end();
 ObexConfig none;REQUIRE(!x.begin(none,ring,sizeof ring)&&!x.enabled());c.opp_channel=31;REQUIRE(!x.begin(c,ring,sizeof ring));c.opp_channel=4;REQUIRE(!x.begin(c,ring,1024));
 c.opp_service_name="123456789012345678901234567890123";REQUIRE(!x.begin(c,ring,sizeof ring));
}
// ---- MAP client (MCE) against a scripted MSE ----
// MAS record shaped like a MAP 1.4 phone stack (sorted attribute IDs).
const uint8_t mas_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x0a, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x32,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,MasChannel,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02, 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x34,0x09,0x01,0x04,
 0x09,0x01,0x00,0x25,0x07,'S','M','S','/','M','M','S', 0x09,0x02,0x00,0x09,0x10,0x29,
 0x09,0x03,0x15,0x08,0x01, 0x09,0x03,0x16,0x08,0x0e, 0x09,0x03,0x17,0x0a,0x00,0x07,0x7f,0xff};
// MAP 1.0 MSE: no GOEP PSM and no MapSupportedFeatures.
const uint8_t mas10_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x0a, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x32,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,MasChannel,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x34,0x09,0x01,0x00,
 0x09,0x03,0x15,0x08,0x00, 0x09,0x03,0x16,0x08,0x02};
Bytes span(const bth_obex_header_t& h){return Bytes(h.data.data,h.data.data+h.data.len);}
Bytes zstr(const char* s){return Bytes(s,s+strlen(s)+1);}
// MCE SDP lookup of the MAS through the real SDP server; exact first request.
void masLookup(Harness& t,const uint8_t* record,size_t size){
 REQUIRE(t.obex.sdpWanted());t.obex.sdpConnecting();
 REQUIRE(!t.obex.sdpWanted()&&t.obex.stats().map_state==MapState::ServiceDiscovery&&t.obex.stats().pbap_state==PbapState::Idle);t.obex.sdpOpened(672);
 bth_sdp_record_t records[]={{0x1000a,{record,size}}};bth_sdp_t server{};REQUIRE(bth_sdp_init(&server,records,1,3));bth_sdp_cursor_t cursor{};
 uint8_t req[128],rsp[64];size_t n=0,m=0;unsigned rounds=0;
 while(t.obex.sdpRequest(req,sizeof req,n)){
  if(!rounds)REQUIRE(n==33&&req[0]==0x06&&Bytes(req+3,req+n)==Bytes({0x00,0x1c,0x35,0x03,0x19,0x11,0x32,0x02,0x40,0x35,18,0x09,0x00,0x04,0x09,0x00,0x09,
   0x09,0x02,0x00,0x09,0x03,0x15,0x09,0x03,0x16,0x09,0x03,0x17,0x00}));
  REQUIRE(bth_sdp_process_compact(&server,&cursor,req,n,rsp,30,&m)==BTH_OK);t.obex.sdpReceive(rsp,m);++rounds;REQUIRE(rounds<20);
 }
 REQUIRE(rounds>1&&t.obex.sdpFinished());t.obex.sdpClosed();REQUIRE(t.obex.stats().map_state==MapState::Transport);
}
// Accepts the MCE CONNECT (exact bytes) as an MSE; returns the next request.
Packet acceptMap(Harness& t,uint32_t id,bool features=true){
 const uint8_t d=t.pbapDlci();Packet c=t.await(d);
 Bytes expect={0x80,0,0,0x10,0,0x04,0x00,0x46,0,19};expect.insert(expect.end(),MasTarget,MasTarget+16);
 if(features)expect.insert(expect.end(),{0x4c,0,9,0x29,4,0,0,0,4});expect[2]=uint8_t(expect.size());
 REQUIRE(c.bytes==expect&&t.obex.stats().map_state==MapState::Connecting);
 t.peerSend(d,build(0xa0,{{0x10,0,0x04,0x00},hu32(BTH_OBEX_H_CONNECTION_ID,id),hbytes(BTH_OBEX_H_WHO,Bytes(MasTarget,MasTarget+16))}));
 return t.await(d);
}
// Answers one SETPATH (flags 0x02, Connection ID, Name) per folder segment.
Packet setPaths(Harness& t,Packet p,std::initializer_list<const char*> folders,uint32_t id){
 for(const char* folder:folders){
  bth_obex_request_t r;bth_obex_header_t h;
  REQUIRE(bth_obex_parse_request(p.bytes.data(),p.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_SETPATH&&r.flags==0x02&&r.constants==0);checkConnectionId(r,id);
  REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&span(h)==text16(folder));
  t.peerSend(t.pbapDlci(),build(0xa0,{}));p=t.await(t.pbapDlci());
 }
 return p;
}
// Answers the final DISCONNECT and waits for the DLC to close.
void closeMap(Harness& t,uint32_t id){
 const uint8_t d=t.pbapDlci();Packet disc=t.await(d);bth_obex_request_t r;
 REQUIRE(bth_obex_parse_request(disc.bytes.data(),disc.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_DISCONNECT);checkConnectionId(r,id);
 REQUIRE(t.obex.stats().map_state==MapState::Closing);
 t.peerSend(d,build(0xa0,{}));t.run(10);REQUIRE(t.peer_closed.count(d)&&t.obex.stats().map_state==MapState::Idle);
}
void map_off_identity(){
 // MAP adds no SDP record, EIR UUID or listener: records/UUIDs equal MAP off.
 uint8_t ring_a[2048],ring_b[2048];ObexConfig off,on;off.opp_enabled=off.pbap_enabled=true;on=off;on.map_enabled=true;
 ObexPort a,b;REQUIRE(a.begin(off,ring_a,sizeof ring_a)&&b.begin(on,ring_b,sizeof ring_b));
 REQUIRE(a.recordCount()==2&&b.recordCount()==2);
 for(size_t i=0;i<2;++i)REQUIRE(a.records()[i].handle==b.records()[i].handle&&a.records()[i].attributes.len==b.records()[i].attributes.len&&
  !memcmp(a.records()[i].attributes.data,b.records()[i].attributes.data,a.records()[i].attributes.len));
 uint16_t ua[4],ub[4];REQUIRE(a.uuids(ua,4)==2&&b.uuids(ub,4)==2&&ua[0]==ub[0]&&ua[1]==ub[1]&&a.oppChannel()==b.oppChannel());
 REQUIRE(!a.mapEnabled()&&b.mapEnabled()&&!a.stats().map_enabled&&b.stats().map_enabled);
 REQUIRE(a.request(MapOperation::GetMessageListing,"telecom/msg/inbox",1,0,0)==Status::Unsupported&&a.mapAbort()==Status::Unsupported);
 // MAP alone: enabled, but no record, no UUID, no OPP channel.
 ObexConfig only;only.map_enabled=true;ObexPort c;REQUIRE(c.begin(only,ring_a,sizeof ring_a)&&c.enabled()&&c.mapEnabled()&&!c.pbapEnabled()&&!c.oppEnabled());
 REQUIRE(c.recordCount()==0&&c.records()==nullptr&&c.uuids(ua,4)==0&&c.oppChannel()==0);
 REQUIRE(c.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",1,0)==Status::Unsupported&&c.abort()==Status::Unsupported);
 const ObexConfig defaults;REQUIRE(!defaults.map_enabled&&!defaults.on_map_data&&!defaults.on_map_end);
 const MapResult none;REQUIRE(none.operation==MapOperation::None&&none.listing_size==0xffff&&none.new_message==0xff&&!none.mse_time[0]&&!none.bytes);
 // Malformed end records are never delivered.
 Events e;ObexConfig cb;cb.user=&e;cb.on_map_end=[](const MapResult& r,void* u){auto& v=*static_cast<Events*>(u);v.map_result=r;++v.map_ends;};
 const uint8_t good[15]={2,1,0,0xa0,0,3,1,0,0,0,9,3,'a','b','c'};
 ObexPort::deliver(cb,ObexPort::MapEnd,good,14);ObexPort::deliver(cb,ObexPort::MapEnd,good,11);REQUIRE(e.map_ends==0);
 ObexPort::deliver(cb,ObexPort::MapEnd,good,15);REQUIRE(e.map_ends==1&&e.map_result.ok&&e.map_result.listing_size==3&&e.map_result.new_message==1&&e.map_result.bytes==9&&!strcmp(e.map_result.mse_time,"abc"));
}
void map_operations(bool initiate){
 Harness t(false,false,4096,initiate,true,MasChannel);if(!initiate)t.startMux();const uint8_t d=t.pbapDlci();
 REQUIRE(t.obex.request(MapOperation::GetMessageListing,"telecom/msg/inbox",10,0,40)==Status::Ok);
 REQUIRE(t.obex.request(MapOperation::GetFolderListing,"telecom/msg",10,0,0)==Status::Busy);
 t.obex.service(t.now);masLookup(t,mas_record,sizeof mas_record);
 auto s=t.obex.stats();
 REQUIRE(s.mas_channel==MasChannel&&s.mas_version==0x0104&&s.mas_instance_id==1&&s.mas_message_types==0x0e&&s.mas_features==0x00077fff&&s.mas_l2cap_psm==0x1029&&s.pse_channel==0);
 Packet g=setPaths(t,acceptMap(t,0x2b),{"telecom","msg","inbox"},0x2b);bth_obex_request_t r;bth_obex_header_t h;
 REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET&&r.final);checkConnectionId(r,0x2b);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&h.data.len==0);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TYPE,&h)==BTH_OK&&span(h)==zstr("x-bt/MAP-msg-listing"));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&span(h)==Bytes({0x01,2,0,10,0x02,2,0,0,0x13,1,40}));
 REQUIRE(t.obex.stats().map_state==MapState::Transferring);
 // Listing in several responses up to the MCE maximum (1024); the first
 // carries MessagesListingSize, NewMessage and MSETime.
 std::string xml="<MAP-msg-listing version=\"1.0\">";
 for(int i=0;i<12;++i)xml+="<msg handle=\"2000010000"+std::to_string(i)+"\" subject=\"Message number "+std::to_string(i)+" with a long subject\" datetime=\"20261120T0930"+std::to_string(10+i)+"\" sender_name=\"Sender "+std::to_string(i)+"\" type=\"SMS_GSM\" size=\"120\" reception_status=\"complete\" attachment_size=\"0\" priority=\"no\" read=\"no\" sent=\"no\" protected=\"no\"/>";
 xml+="</MAP-msg-listing>";
 const std::string time="20261120T093000+0100";
 size_t at=0;unsigned rounds=0;
 while(true){
  Bytes app;if(!rounds){app={0x12,2,0,12,0x0d,1,1,0x19,uint8_t(time.size())};app.insert(app.end(),time.begin(),time.end());}
  const size_t room=1024-3-(app.empty()?0:app.size()+3)-3,k=xml.size()-at<room?xml.size()-at:room;const bool last=at+k==xml.size();
  if(app.empty())t.peerSend(d,build(last?0xa0:0x90,{hbytes(last?0x49:0x48,xml.substr(at,k))}));
  else t.peerSend(d,build(last?0xa0:0x90,{hbytes(0x4c,app),hbytes(last?0x49:0x48,xml.substr(at,k))}));
  at+=k;++rounds;if(last)break;REQUIRE(t.await(d).bytes==Bytes({0x83,0,3})); // Continuation GET.
 }
 REQUIRE(rounds>2);closeMap(t,0x2b);
 REQUIRE(t.ev.map_ends==1&&t.ev.map_result.ok&&t.ev.map_result.operation==MapOperation::GetMessageListing&&t.ev.map_result.response==0xa0&&!t.ev.map_result.aborted);
 REQUIRE(t.ev.map_result.listing_size==12&&t.ev.map_result.new_message==1&&time==t.ev.map_result.mse_time&&t.ev.map_result.bytes==xml.size()&&t.ev.map==xml);
 s=t.obex.stats();REQUIRE(s.map_completed==1&&s.map_failed==0&&s.map_bytes==xml.size()&&s.pbap_completed==0&&s.pbap_bytes==0&&s.last_response==0xa0&&t.ev.pb_ends==0);
 if(initiate){REQUIRE(t.voice.closeRequested());return;} // The idle initiated mux retires its L2CAP channel.
 REQUIRE(!t.voice.closeRequested());
 // Cached MAS: folder listing (no Name header, no SubjectLength) skips SDP.
 t.peer_closed.clear();t.ev.map.clear();REQUIRE(t.obex.request(MapOperation::GetFolderListing,"telecom/msg",1024,0,99)==Status::Ok);t.obex.service(t.now);REQUIRE(!t.obex.sdpWanted());
 g=setPaths(t,acceptMap(t,0x31),{"telecom","msg"},0x31);
 REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET);checkConnectionId(r,0x31);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)!=BTH_OK);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TYPE,&h)==BTH_OK&&span(h)==zstr("x-obex/folder-listing"));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&span(h)==Bytes({0x01,2,0x04,0x00,0x02,2,0,0}));
 const std::string folders="<?xml version=\"1.0\"?><folder-listing version=\"1.0\"><folder name=\"inbox\"/><folder name=\"outbox\"/><folder name=\"sent\"/><folder name=\"deleted\"/><folder name=\"draft\"/></folder-listing>";
 t.peerSend(d,build(0xa0,{hbytes(0x4c,Bytes({0x11,2,0,5})),hbytes(0x49,folders)}));closeMap(t,0x31);
 REQUIRE(t.ev.map_ends==2&&t.ev.map_result.ok&&t.ev.map_result.operation==MapOperation::GetFolderListing&&t.ev.map_result.listing_size==5&&t.ev.map_result.new_message==0xff&&!t.ev.map_result.mse_time[0]&&t.ev.map==folders);
 // Message by handle: no SETPATH; Name=handle, Attachment off, Charset UTF-8.
 t.peer_closed.clear();t.ev.map.clear();REQUIRE(t.obex.request(MapOperation::GetMessage,"20000100001",0,0,0)==Status::Ok);t.obex.service(t.now);
 g=acceptMap(t,0x32);REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET);checkConnectionId(r,0x32);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_OK&&span(h)==text16("20000100001"));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_TYPE,&h)==BTH_OK&&span(h)==zstr("x-bt/message"));
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&span(h)==Bytes({0x0a,1,0,0x14,1,1}));
 const std::string message="BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:UNREAD\r\nTYPE:SMS_GSM\r\nFOLDER:telecom/msg/inbox\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nN:Doe;Jane\r\nTEL:+15550100\r\nEND:VCARD\r\n"
  "BEGIN:BENV\r\nBEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:46\r\nBEGIN:MSG\r\nSee you at 10 \xe2\x98\x95\r\nEND:MSG\r\nEND:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n";
 t.peerSend(d,build(0x90,{hbytes(0x48,message.substr(0,100))}));REQUIRE(t.await(d).bytes==Bytes({0x83,0,3}));
 t.peerSend(d,build(0xa0,{hbytes(0x49,message.substr(100))}));closeMap(t,0x32);
 REQUIRE(t.ev.map_ends==3&&t.ev.map_result.ok&&t.ev.map_result.operation==MapOperation::GetMessage&&t.ev.map_result.bytes==message.size()&&t.ev.map==message&&t.ev.map_result.listing_size==0xffff);
 REQUIRE(t.obex.stats().map_completed==3&&t.obex.stats().mas_channel==MasChannel&&!t.voice.closeRequested());
}
void map_failures(){
 Harness t(false,false,4096,false,true,MasChannel);t.startMux();const uint8_t d=t.pbapDlci();
 // CONNECT refused: failure with the MSE code; no OBEX DISCONNECT, DLC closed.
 REQUIRE(t.obex.request(MapOperation::GetMessageListing,"telecom/msg/inbox",10,0,0)==Status::Ok);t.obex.service(t.now);masLookup(t,mas_record,sizeof mas_record);
 t.await(d);t.peerSend(d,build(0xc3,{}));t.run(15);
 REQUIRE(t.ev.map_ends==1&&!t.ev.map_result.ok&&t.ev.map_result.response==0xc3&&t.peer_closed.count(d)&&t.inbox[d].empty()&&t.obex.stats().map_state==MapState::Idle);
 // A refused SETPATH segment: DISCONNECT, failure with that code.
 t.peer_closed.clear();REQUIRE(t.obex.request(MapOperation::GetMessageListing,"telecom/msg/nope",10,0,0)==Status::Ok);t.obex.service(t.now);
 Packet p=setPaths(t,acceptMap(t,3),{"telecom","msg"},3);t.peerSend(d,build(0xc4,{}));closeMap(t,3);
 REQUIRE(t.ev.map_ends==2&&!t.ev.map_result.ok&&t.ev.map_result.response==0xc4);
 // GET error (unknown handle): DISCONNECT, failure with the code.
 t.peer_closed.clear();REQUIRE(t.obex.request(MapOperation::GetMessage,"FFFF",0,0,0)==Status::Ok);t.obex.service(t.now);
 acceptMap(t,4);t.peerSend(d,build(0xc6,{}));closeMap(t,4);
 REQUIRE(t.ev.map_ends==3&&!t.ev.map_result.ok&&t.ev.map_result.response==0xc6&&t.obex.stats().last_response==0xc6);
 // Abort after the first body packet: ABORT (Connection ID), then DISCONNECT.
 t.peer_closed.clear();t.ev.map.clear();REQUIRE(t.obex.request(MapOperation::GetMessage,"0123456789abcdef",0,0,0)==Status::Ok);t.obex.service(t.now);
 acceptMap(t,7);REQUIRE(t.obex.abort()==Status::Unsupported&&t.obex.mapAbort()==Status::Ok);t.peerSend(d,build(0x90,{hbytes(0x48,std::string("BEGIN:BMSG\r\n"))}));
 Packet a=t.await(d);bth_obex_request_t r;REQUIRE(bth_obex_parse_request(a.bytes.data(),a.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_ABORT);checkConnectionId(r,7);
 t.peerSend(d,build(0xa0,{}));closeMap(t,7);
 REQUIRE(t.ev.map_ends==4&&!t.ev.map_result.ok&&t.ev.map_result.aborted&&t.ev.map=="BEGIN:BMSG\r\n"&&t.obex.mapAbort()==Status::InvalidArgument);
 // Silent MSE: the response deadline fails the operation and closes the DLC.
 t.peer_closed.clear();REQUIRE(t.obex.request(MapOperation::GetFolderListing,"",10,0,0)==Status::Ok);t.obex.service(t.now);
 p=acceptMap(t,8);REQUIRE(p.bytes[0]==0x83); // Root folder: no SETPATH.
 t.tick(ObexPort::ResponseTimeoutMs+1);t.run(10);
 REQUIRE(t.ev.map_ends==5&&!t.ev.map_result.ok&&t.ev.map_result.response==0&&t.peer_closed.count(d)&&t.obex.stats().map_state==MapState::Idle);
 // Malformed response (header overrun): protocol error, DLC closed.
 t.peer_closed.clear();REQUIRE(t.obex.request(MapOperation::GetMessage,"1",0,0,0)==Status::Ok);t.obex.service(t.now);
 acceptMap(t,9);t.peerSend(d,Bytes({0x90,0,6,0x48,0,9}));t.run(15);
 REQUIRE(t.ev.map_ends==6&&!t.ev.map_result.ok&&t.peer_closed.count(d)&&t.obex.stats().protocol_errors==1&&!t.voice.closeRequested());
 // Link loss mid-transfer: the MSE drops the DLC; the partial body was delivered.
 t.peer_closed.clear();t.ev.map.clear();REQUIRE(t.obex.request(MapOperation::GetMessage,"2",0,0,0)==Status::Ok);t.obex.service(t.now);
 acceptMap(t,10);t.peerSend(d,build(0x90,{hbytes(0x48,std::string("BEGIN:BMSG"))}));REQUIRE(t.await(d).bytes==Bytes({0x83,0,3}));
 size_t n=0;REQUIRE(bth_rfcomm_disconnect(&t.peer,19,d,t.now,t.reply,sizeof t.reply,&n)==BTH_OK);t.to_voice.emplace_back(t.reply,t.reply+n);t.run(10);
 REQUIRE(t.ev.map_ends==7&&!t.ev.map_result.ok&&!t.ev.map_result.aborted&&t.ev.map_result.response==0&&t.ev.map=="BEGIN:BMSG"&&t.obex.stats().map_state==MapState::Idle);
 REQUIRE(t.obex.stats().map_failed==7&&t.obex.stats().map_completed==0);
 // Abort before OBEX (SDP stage) completes at once; input validation.
 Harness u(false,false,4096,false,true,MasChannel);REQUIRE(u.obex.request(MapOperation::GetMessageListing,"telecom/msg/inbox",1,0,0)==Status::Ok);REQUIRE(u.obex.mapAbort()==Status::Ok);u.events();
 REQUIRE(u.ev.map_ends==1&&u.ev.map_result.aborted&&u.ev.map_result.operation==MapOperation::GetMessageListing&&u.obex.stats().map_state==MapState::Idle);
 REQUIRE(u.obex.request(MapOperation::None,"x",1,0,0)==Status::InvalidArgument&&u.obex.request(MapOperation::GetFolderListing,nullptr,1,0,0)==Status::InvalidArgument);
 for(const char* bad:{"","12345678901234567","12g4","20 1","0x12"})REQUIRE(u.obex.request(MapOperation::GetMessage,bad,0,0,0)==Status::InvalidArgument);
 std::string longest(ObexPort::PathBytes-1,'a'),too_long(ObexPort::PathBytes,'a');
 REQUIRE(u.obex.request(MapOperation::GetMessageListing,too_long.c_str(),1,0,0)==Status::InvalidArgument);
 REQUIRE(u.obex.request(MapOperation::GetMessageListing,longest.c_str(),1,0,0)==Status::Ok&&u.obex.mapAbort()==Status::Ok);u.events();REQUIRE(u.ev.map_ends==2);
 // A MAS that is not in the phone's SDP database fails the lookup.
 REQUIRE(u.obex.request(MapOperation::GetMessage,"ABCDEF0123456789",0,0,0)==Status::Ok);u.obex.service(u.now);u.obex.sdpConnecting();u.obex.sdpOpened(672);
 uint8_t req[64];REQUIRE(u.obex.sdpRequest(req,sizeof req,n)&&n==33&&req[9]==0x32);const uint8_t empty[]={0x07,req[1],req[2],0x00,0x05,0x00,0x02,0x35,0x00,0x00};u.obex.sdpReceive(empty,sizeof empty);
 REQUIRE(u.obex.sdpFinished());u.obex.sdpClosed();u.events();REQUIRE(u.ev.map_ends==3&&!u.ev.map_result.ok&&u.obex.stats().map_state==MapState::Idle&&u.obex.stats().mas_channel==0);
}
void map_version_10(){
 // A MAP 1.0 MSE (no MapSupportedFeatures): CONNECT carries no app parameters;
 // a listing response without size/time parameters reports them absent.
 Harness t(false,false,4096,false,true,MasChannel);t.startMux();const uint8_t d=t.pbapDlci();
 REQUIRE(t.obex.request(MapOperation::GetMessageListing,"/telecom/msg/inbox/",0,0,0)==Status::Ok);t.obex.service(t.now);masLookup(t,mas10_record,sizeof mas10_record);
 const auto s=t.obex.stats();REQUIRE(s.mas_version==0x0100&&s.mas_instance_id==0&&s.mas_message_types==0x02&&s.mas_features==0&&s.mas_l2cap_psm==0);
 Packet g=setPaths(t,acceptMap(t,0x40,false),{"telecom","msg","inbox"},0x40);bth_obex_request_t r;bth_obex_header_t h;
 REQUIRE(bth_obex_parse_request(g.bytes.data(),g.bytes.size(),&r)==BTH_OK&&r.opcode==BTH_OBEX_GET&&bth_obex_header_find(r.headers,BTH_OBEX_H_APP_PARAMS,&h)==BTH_OK&&span(h)==Bytes({0x01,2,0,0,0x02,2,0,0}));
 // MaxListCount 0: size only, no body. Oversized MSETime is truncated to 23.
 const std::string time(30,'7');Bytes app={0x12,2,0x01,0x2c,0x19,uint8_t(time.size())};app.insert(app.end(),time.begin(),time.end());
 t.peerSend(d,build(0xa0,{hbytes(0x4c,app)}));closeMap(t,0x40);
 REQUIRE(t.ev.map_ends==1&&t.ev.map_result.ok&&t.ev.map_result.listing_size==300&&t.ev.map_result.bytes==0&&strlen(t.ev.map_result.mse_time)==23&&t.ev.map.empty());
}
void map_and_pbap_share_client(){
 // One OBEX client link: a MAP operation makes PBAP Busy and vice versa; each
 // abort only touches its own profile. Caches are per profile.
 Harness t(false,true,4096,false,true,MasChannel);t.startMux();
 REQUIRE(t.obex.request(MapOperation::GetMessageListing,"telecom/msg/inbox",5,0,0)==Status::Ok);
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",5,0)==Status::Busy&&t.obex.abort()==Status::InvalidArgument);
 t.obex.service(t.now);masLookup(t,mas_record,sizeof mas_record);
 const uint8_t d=t.pbapDlci();setPaths(t,acceptMap(t,0x50),{"telecom","msg","inbox"},0x50);
 REQUIRE(t.obex.stats().pbap_state==PbapState::Idle&&t.obex.stats().map_state==MapState::Transferring);
 t.peerSend(d,build(0xa0,{hbytes(0x49,std::string("<MAP-msg-listing version=\"1.0\"/>"))}));closeMap(t,0x50);REQUIRE(t.ev.map_ends==1&&t.ev.map_result.ok&&t.ev.pb_ends==0);
 // PBAP next: the PSE is not cached by the MAS lookup.
 REQUIRE(t.obex.request(PbapOperation::PullPhonebook,"telecom/pb.vcf",5,0)==Status::Ok);t.obex.service(t.now);REQUIRE(t.obex.sdpWanted());
 REQUIRE(t.obex.request(MapOperation::GetMessage,"1",0,0,0)==Status::Busy&&t.obex.mapAbort()==Status::InvalidArgument);
 REQUIRE(t.obex.stats().pbap_state==PbapState::ServiceDiscovery&&t.obex.stats().map_state==MapState::Idle);
 REQUIRE(t.obex.abort()==Status::Ok);t.events();REQUIRE(t.ev.pb_ends==1&&t.ev.result.aborted&&t.ev.map_ends==1);
 // The cached MAS still skips SDP afterwards.
 REQUIRE(t.obex.request(MapOperation::GetMessage,"1",0,0,0)==Status::Ok);t.obex.service(t.now);REQUIRE(!t.obex.sdpWanted()&&t.obex.stats().map_state==MapState::Transport);
 REQUIRE(t.obex.mapAbort()==Status::Ok);t.events();REQUIRE(t.ev.map_ends==2&&t.ev.map_result.aborted);
}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 opp_push_streaming();opp_backpressure();opp_errors();pbap_pull(false);pbap_pull(true);pbap_failures();pbap_listing();opp_and_pbap_share_mux();
 const unsigned before=checks;
 map_off_identity();map_operations(false);map_operations(true);map_failures();map_version_10();map_and_pbap_share_client();
 std::printf("MAP_MCE_PROFILES_PASS checks=%u (scripted MSE: SDP, CONNECT target, SETPATH, GET listing/message, continuation, errors, abort, timeout, link loss)\n",checks-before);
 std::printf("OBEX_OPP_PBAP_PROFILES_PASS checks=%u (real RFCOMM both ends, scripted OPP client/PSE/MSE; no radio)\n",checks);return 0;
}
