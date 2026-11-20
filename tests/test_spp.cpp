// Serial Port Profile owner checks: real RFCOMM kernel on both ends, the real
// VoiceProfiles mux owner and the real bounded Classic control FIFO. Synthetic
// only: no controller, radio, phone, Linux or Android peer is exercised here.
#include "../src/owned/VoiceProfiles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace giga_bredr;using namespace giga_bredr::detail;
namespace {
unsigned checks;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"SPP CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
std::string pattern(size_t n,unsigned seed){std::string s(n,'\0');for(size_t i=0;i<n;++i)s[i]=char((i*131u+seed*7u+(i>>8))&0xff);return s;}
const uint8_t* bytes(const std::string& s){return reinterpret_cast<const uint8_t*>(s.data());}
struct Harness {
 VoiceProfiles voice;SppPort port;bth_control_tx_t queue{};bth_hci_flow_t flow{};bth_rfcomm_t peer{};
 std::vector<uint8_t> rx_ring,tx_ring;uint32_t now=100;uint8_t wire[1100]{},reply[600]{};
 std::string received;std::vector<size_t> frame_sizes;std::vector<uint32_t> frame_times;
 bool peer_release=true;unsigned held=0,max_frames_per_tick=0,ticks=0;
 explicit Harness(size_t rxn=2048,size_t txn=2048,bool voice_too=false,uint8_t peer_window=7,uint16_t peer_mtu=512):rx_ring(rxn),tx_ring(txn){
  bth_control_tx_init(&queue);REQUIRE(bth_control_tx_attach(&queue,0,0x25,7));
  bth_hci_flow_init(&flow);REQUIRE(bth_hci_flow_set_buffers(&flow,1021,8,251,4));REQUIRE(bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC));
  REQUIRE(port.begin("GIGA Serial",3,rx_ring.data(),rxn,tx_ring.data(),txn,4));
  VoiceControlConfig config;config.serial=&port;config.hands_free=config.headset=voice_too;
  REQUIRE(voice.begin(config,queue)==Status::Ok);voice.reset(7);
  REQUIRE(voice.channelOpened(7,0x40,0x80,672));
  REQUIRE(bth_rfcomm_init(&peer,19,true,0,peer_mtu,peer_window));
 }
 void send(const uint8_t* p,size_t n){REQUIRE(voice.receive(0x40,p,n,true,now));}
 void complete(){const uint8_t p[]={4,0x13,5,1,0x25,0,1,0};bth_h4_frame_t f{};REQUIRE(bth_h4_decode(p,sizeof p,&f)==BTH_OK);REQUIRE(bth_hci_flow_on_event(&flow,&f)==BTH_OK);}
 const bth_rfcomm_dlc_t* peerDlc(uint8_t dlci=6)const{for(const auto& d:peer.dlc)if(d.state&&d.dlci==dlci)return &d;return nullptr;}
 const bth_rfcomm_dlc_t* ownDlc(uint8_t dlci=6)const{for(const auto& d:voice.mux().dlc)if(d.state&&d.dlci==dlci)return &d;return nullptr;}
 void drain(){
  size_t n=0;const uint32_t owners[]={7,0};unsigned frames=0;
  while(bth_control_tx_next(&queue,owners,&flow,wire,sizeof wire,&n)==BTH_OK){
   REQUIRE(n>=9&&wire[0]==2);
   if(bth_read_le16(wire+7)!=0x80){complete();continue;} // Unrelated fixture slots.
   size_t rn=0;bth_rfcomm_event_t e{};
   REQUIRE(bth_rfcomm_receive(&peer,19,now,wire+9,n-9,reply,sizeof reply,&rn,&e)>=BTH_OK);
   if(e.type==BTH_RFCOMM_DATA&&e.dlci==6){
    received.append(reinterpret_cast<const char*>(e.payload.data),e.payload.len);frame_sizes.push_back(e.payload.len);frame_times.push_back(now);++frames;
    if(!peerDlc()->credit){}else if(peer_release)REQUIRE(bth_rfcomm_release(&peer,19,e.dlci,1)==BTH_OK);else ++held;
   }
   if(rn)send(reply,rn);complete();
  }
  if(frames>max_frames_per_tick)max_frames_per_tick=frames;
 }
 void releaseHeld(){while(held){REQUIRE(bth_rfcomm_release(&peer,19,6,1)==BTH_OK);--held;}}
 void tick(){++now;++ticks;voice.poll(true,now);drain();size_t n=0;REQUIRE(bth_rfcomm_next(&peer,19,now,reply,sizeof reply,&n)>=BTH_OK);if(n)send(reply,n);}
 void run(unsigned count){for(unsigned i=0;i<count;++i)tick();}
 void open(uint8_t scn=3,bool credit=true){
  size_t n=0;if(peer.mux==BTH_RFCOMM_CLOSED){REQUIRE(bth_rfcomm_start(&peer,19,now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);run(5);}
  REQUIRE(bth_rfcomm_open(&peer,19,scn,credit,now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
  for(unsigned k=0;k<30&&!bth_rfcomm_ready(&peer,19,uint8_t(scn*2));++k)tick();
  REQUIRE(bth_rfcomm_ready(&peer,19,uint8_t(scn*2)));run(3);
 }
 bth_status_t peerSend(const std::string& s,size_t at,size_t len){size_t w=0;const auto st=bth_rfcomm_send(&peer,19,6,bytes(s)+at,len,reply,sizeof reply,&w);if(st==BTH_OK)send(reply,w);return st;}
 std::string readAll(size_t chunk=4096){std::string out;std::vector<uint8_t> b(chunk);for(;;){const size_t n=port.read(b.data(),b.size());if(!n)break;out.append(reinterpret_cast<const char*>(b.data()),n);}return out;}
};
void sdp_record_bytes(){
 uint8_t out[SppPort::RecordBytes];const size_t n=SppPort::buildRecord(out,sizeof out,3,"GIGA Serial");
 const uint8_t golden[]={
  0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x06,
  0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x01,
  0x09,0x00,0x04,0x35,0x0c,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x03,
  0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,
  0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00,
  0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x01,0x09,0x01,0x02,
  0x09,0x01,0x00,0x25,0x0b,'G','I','G','A',' ','S','e','r','i','a','l'};
 REQUIRE(n==sizeof golden&&!std::memcmp(out,golden,n));
 REQUIRE(!SppPort::buildRecord(out,sizeof out,0,"x")&&!SppPort::buildRecord(out,sizeof out,31,"x")&&!SppPort::buildRecord(out,sizeof out,3,""));
 REQUIRE(SppPort::buildRecord(out,sizeof out,30,"12345678901234567890123456789012")==73+32);
 REQUIRE(!SppPort::buildRecord(out,sizeof out,30,"123456789012345678901234567890123")&&!SppPort::buildRecord(out,80,3,"GIGA Serial "));
 uint8_t rx[512],tx[512];SppPort port;REQUIRE(!port.begin("GIGA Serial",3,rx,511,tx,512,0)&&!port.begin(nullptr,3,rx,512,tx,512,0)&&!port.enabled());
 REQUIRE(port.begin("GIGA Serial",3,rx,512,tx,512,0)&&port.record()&&port.record()->handle==0x10006&&port.creditWindow(127)==4);
 // Real SDP server: ServiceSearch by SerialPort UUID and by the public browse
 // root both find the record; ServiceSearchAttribute returns these exact bytes.
 bth_sdp_t server{};REQUIRE(bth_sdp_init(&server,port.record(),1,19));
 const uint8_t search[]={2,0,1,0,8,0x35,3,0x19,0x11,0x01,0,4,0};uint8_t response[256];size_t written=0;
 REQUIRE(bth_sdp_process(&server,search,sizeof search,response,sizeof response,&written)==BTH_OK);
 REQUIRE(written==14&&response[0]==3&&response[6]==1&&response[8]==1&&!std::memcmp(response+9,"\x00\x01\x00\x06",4));
 const uint8_t browse[]={2,0,2,0,8,0x35,3,0x19,0x10,0x02,0,4,0};
 REQUIRE(bth_sdp_process(&server,browse,sizeof browse,response,sizeof response,&written)==BTH_OK&&written==14&&response[8]==1&&response[12]==6);
 const uint8_t attributes[]={6,0,3,0,15,0x35,3,0x19,0x11,0x01,0x01,0x00,0x35,5,0x0a,0,0,0xff,0xff,0};
 REQUIRE(bth_sdp_process(&server,attributes,sizeof attributes,response,sizeof response,&written)==BTH_OK&&response[0]==7);
 bool found=false;for(size_t i=0;i+sizeof golden<=written;++i)if(!std::memcmp(response+i,golden,sizeof golden))found=true;REQUIRE(found);
 const uint8_t rfcomm_only[]={2,0,4,0,8,0x35,3,0x19,0x00,0x03,0,4,0};
 REQUIRE(bth_sdp_process(&server,rfcomm_only,sizeof rfcomm_only,response,sizeof response,&written)==BTH_OK&&response[8]==1);
 port.end();REQUIRE(!port.enabled()&&!port.record());
}
void connect_credit_mtu(){
 Harness h;REQUIRE(!h.port.connected());
 h.open();REQUIRE(h.port.connected());const auto s=h.port.stats();
 REQUIRE(s.connected&&s.credit_flow&&s.frame_bytes==127&&s.generation&&s.connections==1);
 const auto* mine=h.ownDlc();const auto* theirs=h.peerDlc();REQUIRE(mine&&theirs);
 // Peer proposed 512 and its 7 credits; this owner answered N1=127 and the
 // SPP listener window (7 frames of the 2 KiB ring), not the voice window2.
 REQUIRE(mine->mtu==127&&theirs->mtu==127&&mine->window==7&&theirs->tx_credits==7&&mine->tx_credits==7);
 // Fill without reading: credits stop the peer before the ring overflows.
 const auto data=pattern(4000,1);size_t sent=0;
 for(unsigned k=0;k<400;++k){if(sent<data.size()){const size_t take=std::min<size_t>(127,data.size()-sent);if(h.peerSend(data,sent,take)==BTH_OK)sent+=take;}h.tick();}
 REQUIRE(sent==16*127&&h.port.available()==sent&&h.port.stats().rx_dropped_bytes==0&&h.peerDlc()->tx_credits==0);
 REQUIRE(h.peerSend(data,sent,1)==BTH_MORE);
 // Oversized frame from a peer is a protocol error, never a partial store.
 {Harness big;big.open();std::string large(128,'x');uint8_t frame[200];size_t w=0;
  REQUIRE(bth_rfcomm_encode(6,true,BTH_RFCOMM_UIH,false,false,0,bytes(large),large.size(),frame,sizeof frame,&w)==BTH_OK);
  big.send(frame,w);REQUIRE(big.voice.closeRequested()&&big.port.available()==0&&!big.port.connected());}
 // Reading frees space; credits return in batches and the stream resumes intact.
 std::string got=h.readAll(1000);REQUIRE(got.size()==sent);
 for(unsigned k=0;k<400;++k){if(sent<data.size()){const size_t take=std::min<size_t>(127,data.size()-sent);if(h.peerSend(data,sent,take)==BTH_OK)sent+=take;}h.tick();got+=h.readAll(333);}
 got+=h.readAll();REQUIRE(sent==data.size()&&got==data&&h.port.stats().rx_bytes==data.size()&&h.port.stats().rx_dropped_bytes==0);
 REQUIRE(!h.voice.closeRequested()&&h.voice.stats().received_frames==0);
}
void write_api(){
 Harness h;size_t accepted=7;const auto data=pattern(3000,2);
 REQUIRE(h.port.write(bytes(data),10,accepted)==Status::InvalidArgument&&accepted==0); // Not connected.
 h.open();REQUIRE(h.port.write(nullptr,1,accepted)==Status::InvalidArgument&&h.port.write(nullptr,0,accepted)==Status::Ok&&accepted==0);
 REQUIRE(h.port.write(bytes(data),data.size(),accepted)==Status::Ok&&accepted==2048);
 REQUIRE(h.port.write(bytes(data)+accepted,100,accepted)==Status::OutputFull&&accepted==0);
 REQUIRE(h.port.stats().tx_rejected_bytes==952+100&&h.port.stats().tx_pending==2048);
 h.run(80);REQUIRE(h.received==data.substr(0,2048)&&h.port.txPending()==0);
 REQUIRE(h.frame_sizes.size()==17);for(auto n:h.frame_sizes)REQUIRE(n>=1&&n<=127);
 REQUIRE(h.max_frames_per_tick<=1&&h.port.stats().tx_bytes==2048&&h.port.stats().tx_frames==17);
 // Ring wrap with partial writes interleaved with drained turns.
 const auto more=pattern(9000,3);size_t at=0;std::string expect=h.received;
 for(unsigned k=0;k<2000&&at<more.size();++k){const size_t chunk=std::min<size_t>(more.size()-at,700+k%300);
  if(h.port.write(bytes(more)+at,chunk,accepted)==Status::Ok)at+=accepted;h.tick();}
 h.run(200);expect+=more;REQUIRE(at==more.size()&&h.received==expect&&h.max_frames_per_tick<=1);
 // Peer credits gate TX: without peer releases only its outstanding credits go.
 h.peer_release=false;h.run(5);const size_t before=h.frame_sizes.size();const unsigned credits=h.ownDlc()->tx_credits;REQUIRE(credits>0&&credits<=7);
 REQUIRE(h.port.write(bytes(data),2000,accepted)==Status::Ok&&accepted==2000);h.run(60);
 REQUIRE(h.frame_sizes.size()-before==credits&&h.ownDlc()->tx_credits==0&&h.port.txPending()==2000-127*credits);
 REQUIRE(h.port.stats().peer_credits==0);
 h.peer_release=true;h.releaseHeld();h.run(80);REQUIRE(h.port.txPending()==0&&h.received.substr(h.received.size()-2000)==data.substr(0,2000));
 REQUIRE(!h.voice.closeRequested());
}
void disconnect_reconnect(){
 Harness h;h.open();const uint32_t first=h.port.stats().generation;size_t accepted=0;
 const auto data=pattern(300,4);REQUIRE(h.peerSend(data,0,10)==BTH_OK);h.tick();REQUIRE(h.port.available()==10);
 h.peer_release=false;h.run(2);REQUIRE(h.port.write(bytes(data),300,accepted)==Status::Ok);h.run(3);
 // Peer withholds credits after its initial window: some TX remains queued.
 size_t n=0;h.run(30);const size_t pending=h.port.txPending();
 REQUIRE(bth_rfcomm_disconnect(&h.peer,19,6,h.now,h.reply,sizeof h.reply,&n)==BTH_OK);h.send(h.reply,n);h.run(3);
 REQUIRE(!h.port.connected()&&!h.port.stats().connected&&h.port.stats().tx_dropped_bytes==pending&&h.port.txPending()==0);
 REQUIRE(h.port.write(bytes(data),1,accepted)==Status::InvalidArgument&&!h.voice.closeRequested());
 uint8_t b[4];REQUIRE(h.port.read(b,4)==4&&h.port.available()==6); // Received bytes stay readable.
 h.peer_release=true;h.held=0;h.open();const auto s=h.port.stats();
 REQUIRE(s.connected&&s.generation&&s.generation!=first&&s.connections==2&&s.rx_dropped_bytes==6&&h.port.available()==0);
 REQUIRE(h.port.write(bytes(data),5,accepted)==Status::Ok&&accepted==5);h.run(5);REQUIRE(h.received.substr(h.received.size()-5)==data.substr(0,5));
}
// L2CAP loss retires the mux and the DLC; a fresh L2CAP channel (local 0x41,
// new peer mux generation) reconnects the same port with a new generation.
void l2cap_reconnect(){
 Harness h;h.open();h.voice.channelClosed(0x40);REQUIRE(!h.port.connected()&&h.port.stats().connections==1);
 REQUIRE(!h.voice.channelOpened(7,0x41,0x81,672)); // Old identity must first retire.
 h.drain();REQUIRE(h.voice.authorizeRetirement(7,0x40,0x80));
 REQUIRE(h.voice.channelOpened(7,0x41,0x81,672));
 bth_rfcomm_t peer{};REQUIRE(bth_rfcomm_init(&peer,23,true,0,512,7));uint8_t reply[600];size_t n=0;
 auto deliver=[&](){size_t w=0;const uint32_t owners[]={7,0};uint8_t wire[1100];
  while(bth_control_tx_next(&h.queue,owners,&h.flow,wire,sizeof wire,&w)==BTH_OK){REQUIRE(bth_read_le16(wire+7)==0x81);size_t rn=0;bth_rfcomm_event_t e{};
   REQUIRE(bth_rfcomm_receive(&peer,23,h.now,wire+9,w-9,reply,sizeof reply,&rn,&e)>=BTH_OK);if(rn)REQUIRE(h.voice.receive(0x41,reply,rn,true,h.now));h.complete();}};
 auto step=[&](){++h.now;h.voice.poll(true,h.now);deliver();size_t w=0;REQUIRE(bth_rfcomm_next(&peer,23,h.now,reply,sizeof reply,&w)>=BTH_OK);if(w)REQUIRE(h.voice.receive(0x41,reply,w,true,h.now));};
 REQUIRE(bth_rfcomm_start(&peer,23,h.now,reply,sizeof reply,&n)==BTH_OK);REQUIRE(h.voice.receive(0x41,reply,n,true,h.now));for(unsigned k=0;k<5;++k)step();
 REQUIRE(bth_rfcomm_open(&peer,23,3,true,h.now,reply,sizeof reply,&n)==BTH_OK);REQUIRE(h.voice.receive(0x41,reply,n,true,h.now));
 for(unsigned k=0;k<30&&!bth_rfcomm_ready(&peer,23,6);++k)step();for(unsigned k=0;k<3;++k)step();
 REQUIRE(bth_rfcomm_ready(&peer,23,6)&&h.port.connected()&&h.port.stats().connections==2);
}
void legacy_flow(){
 Harness h;h.open(3,false);REQUIRE(h.port.connected()&&!h.port.stats().credit_flow&&!h.ownDlc()->credit);
 const auto data=pattern(4000,5);size_t sent=0;
 for(unsigned k=0;k<200;++k){if(sent<data.size()){const auto st=h.peerSend(data,sent,127);if(st==BTH_OK)sent+=127;else REQUIRE(st==BTH_MORE);}h.tick();}
 // MSC FC=1 stops an RFCOMM1.0 peer with ring margin; nothing is dropped.
 REQUIRE(h.ownDlc()->local_flow_off&&h.peerDlc()->peer_flow_off&&h.port.stats().rx_dropped_bytes==0&&h.port.available()==sent&&sent>=2048-2*127);
 std::string got=h.readAll();h.run(5);REQUIRE(!h.ownDlc()->local_flow_off&&!h.peerDlc()->peer_flow_off);
 for(unsigned k=0;k<200&&sent+127<=data.size();++k){if(h.peerSend(data,sent,127)==BTH_OK)sent+=127;h.tick();got+=h.readAll(200);}
 got+=h.readAll();REQUIRE(got==data.substr(0,sent)&&h.port.stats().rx_dropped_bytes==0);
 // Legacy TX: no credits, our frames still bounded by N1 and one per turn.
 size_t accepted=0;REQUIRE(h.port.write(bytes(data),1000,accepted)==Status::Ok&&accepted==1000);h.run(30);
 REQUIRE(h.received==data.substr(0,1000)&&h.max_frames_per_tick<=1);
}
void coexistence(){
 Harness h;h.open();size_t accepted=0;const auto data=pattern(1000,6);
 // One unrelated committed Classic control slot: the SPP frame waits in its
 // own owner FIFO instead of taking a shared slot ahead of control replies.
 bth_control_reservation_t slot{};REQUIRE(bth_control_tx_reserve(&h.queue,0,&slot)==BTH_OK);slot.data[0]=0xaa;REQUIRE(bth_control_tx_commit(&h.queue,slot.token,0x90,1,672)==BTH_OK);
 REQUIRE(h.port.write(bytes(data),data.size(),accepted)==Status::Ok);
 for(unsigned k=0;k<10;++k)h.voice.poll(true,++h.now);
 REQUIRE(h.voice.pendingOutput()&&h.port.txPending()==data.size()-127&&bth_control_tx_available(&h.queue,0)==2);
 bool pending=false;REQUIRE(bth_control_tx_channel_pending(&h.queue,0,7,0x80,&pending)==BTH_OK&&!pending);
 // Peer data consumes credits; the credit return is non-bulk RFCOMM output and
 // passes the waiting data frame instead of queueing behind it.
 const auto small=pattern(40,8);for(unsigned k=0;k<4;++k)REQUIRE(h.peerSend(small,k*10,10)==BTH_OK);
 const unsigned credits=h.peerDlc()->tx_credits;REQUIRE(credits==3);
 for(unsigned k=0;k<3;++k)h.voice.poll(true,++h.now);
 REQUIRE(bth_control_tx_channel_pending(&h.queue,0,7,0x80,&pending)==BTH_OK&&pending&&h.port.txPending()==data.size()-127);
 h.drain();REQUIRE(h.received.empty()&&h.peerDlc()->tx_credits==7&&bth_control_tx_available(&h.queue,0)==3&&h.readAll()==small);
 h.run(1);REQUIRE(h.received.size()==127);h.run(30);REQUIRE(h.received==data);
 // A2DP streaming pacing: actor-selected minimum spacing between data frames.
 h.voice.setBulkInterval(4);h.frame_times.clear();REQUIRE(h.port.write(bytes(data),data.size(),accepted)==Status::Ok);h.run(60);
 REQUIRE(h.frame_times.size()==8);for(size_t i=1;i<h.frame_times.size();++i)REQUIRE(h.frame_times[i]-h.frame_times[i-1]>=4);
 h.voice.setBulkInterval(0);REQUIRE(!h.voice.closeRequested());
}
void with_voice(){
 Harness h(2048,2048,true);h.open(2);REQUIRE(h.voice.hspStatus().connected&&!h.port.connected());
 h.open(3);REQUIRE(h.port.connected()&&h.voice.hspStatus().connected);
 REQUIRE(h.ownDlc(4)->window==2&&h.ownDlc(6)->window==7); // HS keeps its two-frame window.
 const auto data=pattern(500,7);size_t sent=0;for(unsigned k=0;k<40;++k){if(sent<data.size()){const size_t t=std::min<size_t>(127,data.size()-sent);if(h.peerSend(data,sent,t)==BTH_OK)sent+=t;}h.tick();}
 REQUIRE(h.readAll()==data&&h.voice.stats().received_frames==h.voice.stats().consumed_frames);
 size_t accepted=0;REQUIRE(h.port.write(bytes(data),data.size(),accepted)==Status::Ok);h.run(30);REQUIRE(h.received==data);
 REQUIRE(!h.voice.closeRequested()&&h.voice.hspStatus().connected);
 // Channel collisions and an unbegun port are rejected before any mux.
 bth_control_tx_t q{};bth_control_tx_init(&q);uint8_t rx[512],tx[512];SppPort port;VoiceProfiles voice;VoiceControlConfig c;c.serial=&port;
 REQUIRE(voice.begin(c,q)==Status::InvalidArgument);REQUIRE(port.begin("S",1,rx,512,tx,512,0));c.hands_free=true;
 REQUIRE(voice.begin(c,q)==Status::InvalidArgument);c.hands_free_channel=5;REQUIRE(voice.begin(c,q)==Status::Ok);voice.end();
 c.hands_free=false;c.headset=true;c.headset_channel=1;REQUIRE(voice.begin(c,q)==Status::InvalidArgument);
}
void security_and_unknown(){
 Harness h;h.open();REQUIRE(h.port.connected());const uint8_t junk[]={0x0f,0xef,1,0xaa};
 REQUIRE(h.voice.receive(0x40,junk,sizeof junk,false,h.now));REQUIRE(h.voice.closeRequested());
 Harness u;size_t n=0;REQUIRE(bth_rfcomm_start(&u.peer,19,u.now,u.reply,sizeof u.reply,&n)==BTH_OK);u.send(u.reply,n);u.run(5);
 REQUIRE(bth_rfcomm_open(&u.peer,19,9,true,u.now,u.reply,sizeof u.reply,&n)==BTH_OK);u.send(u.reply,n);u.run(10);
 REQUIRE(!bth_rfcomm_ready(&u.peer,19,18)&&!u.port.connected()); // Unregistered SCN refused with DM.
}
void serial_absent_is_voice(){
 // Without a port the owner is the unchanged voice-only RFCOMM owner.
 bth_control_tx_t q{};bth_control_tx_init(&q);REQUIRE(bth_control_tx_attach(&q,0,0x25,7));VoiceProfiles voice;VoiceControlConfig c;
 REQUIRE(voice.begin(c,q)==Status::InvalidArgument);c.hands_free=true;REQUIRE(voice.begin(c,q)==Status::Ok);voice.reset(7);
 REQUIRE(voice.channelOpened(7,0x40,0x80,672)&&voice.mux().listener_mask==(1u<<1)&&!voice.mux().listener_scn[0]);
}
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);sdp_record_bytes();connect_credit_mtu();write_api();disconnect_reconnect();l2cap_reconnect();legacy_flow();coexistence();with_voice();security_and_unknown();serial_absent_is_voice();
 std::printf("SPP_OWNER_PROTOCOL_PASS checks=%u (synthetic RFCOMM peer; no radio)\n",checks);return 0;}
