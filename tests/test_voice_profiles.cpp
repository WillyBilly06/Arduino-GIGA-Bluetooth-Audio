#include "../src/owned/VoiceProfiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace giga_bredr;using namespace giga_bredr::detail;
namespace {
unsigned checks;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"VOICE CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Harness {
 VoiceProfiles voice;bth_control_tx_t queue{};bth_hci_flow_t flow{};bth_rfcomm_t peer{};
 uint32_t now=100;uint8_t wire[1100]{},reply[160]{};std::string at,pending;size_t pending_cursor=0;unsigned brsf=0;bool auto_answer=true;
 Harness(){
  bth_control_tx_init(&queue);REQUIRE(bth_control_tx_attach(&queue,0,0x25,7));
  bth_hci_flow_init(&flow);REQUIRE(bth_hci_flow_set_buffers(&flow,1021,8,251,4));REQUIRE(bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC));
  VoiceControlConfig config;config.hands_free=true;config.headset=true;
  REQUIRE(voice.begin(config,queue)==Status::Ok);voice.reset(7);
  REQUIRE(voice.channelOpened(7,0x40,0x80,672));
  REQUIRE(bth_rfcomm_init(&peer,19,true,0,127,2));
 }
 void send(const uint8_t* p,size_t n){REQUIRE(voice.receive(0x40,p,n,true,now));}
 void complete(){const uint8_t p[]={4,0x13,5,1,0x25,0,1,0};bth_h4_frame_t f{};REQUIRE(bth_h4_decode(p,sizeof p,&f)==BTH_OK);REQUIRE(bth_hci_flow_on_event(&flow,&f)==BTH_OK);}
 void answer(){
  if(!auto_answer)return;
  for(;;){size_t cr=at.find('\r');if(cr==std::string::npos)break;std::string row=at.substr(0,cr+1);at.erase(0,cr+1);
   if(row=="AT+BRSF=16\r"){++brsf;pending+="\r\n+BRSF: 0\r\nOK\r\n";}
   else if(row=="AT+CIND=?\r")pending+="\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n";
   else if(row=="AT+CIND?\r")pending+="\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n";
   else if(row=="AT+CMER=3,0,0,1\r"||row=="AT+VGS=15\r"||row=="AT+VGM=15\r")pending+="\r\nOK\r\n";
   else REQUIRE(false);
  }
 }
 void drain(){
  size_t n=0;const uint32_t owners[]={7,0};
  while(bth_control_tx_next(&queue,owners,&flow,wire,sizeof wire,&n)==BTH_OK){
   REQUIRE(n>=9&&wire[0]==2&&bth_read_le16(wire+7)==0x80);size_t rn=0;bth_rfcomm_event_t e{};
   REQUIRE(bth_rfcomm_receive(&peer,19,now,wire+9,n-9,reply,sizeof reply,&rn,&e)>=BTH_OK);
   if(e.type==BTH_RFCOMM_DATA){at.append(reinterpret_cast<const char*>(e.payload.data),e.payload.len);REQUIRE(bth_rfcomm_release(&peer,19,e.dlci,1)==BTH_OK);answer();}
   if(rn)send(reply,rn);complete();
  }
 }
 void tick(){
  ++now;voice.poll(true,now);drain();
  size_t n=0;REQUIRE(bth_rfcomm_next(&peer,19,now,reply,sizeof reply,&n)>=BTH_OK);if(n)send(reply,n);
  if(pending_cursor<pending.size()){
   size_t take=std::min(size_t(23),pending.size()-pending_cursor);
   auto st=bth_rfcomm_send(&peer,19,2,reinterpret_cast<const uint8_t*>(pending.data()+pending_cursor),take,reply,sizeof reply,&n);
   REQUIRE(st==BTH_OK||st==BTH_MORE);if(st==BTH_OK){send(reply,n);pending_cursor+=take;}
  }
 }
 void open(uint8_t scn){
  size_t n=0;if(peer.mux==BTH_RFCOMM_CLOSED){REQUIRE(bth_rfcomm_start(&peer,19,now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);for(unsigned k=0;k<5;++k)tick();}
  REQUIRE(bth_rfcomm_open(&peer,19,scn,true,now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
  for(unsigned k=0;k<30&&!bth_rfcomm_ready(&peer,19,uint8_t(scn*2));++k)tick();
  REQUIRE(bth_rfcomm_ready(&peer,19,uint8_t(scn*2)));
 }
};
void dual_listener_and_slc(){Harness h;h.open(1);for(unsigned k=0;k<300&&!h.voice.hfpStatus().slcEstablished;++k)h.tick();for(unsigned k=0;k<30;++k)h.tick();
 REQUIRE(h.voice.hfpStatus().slcEstablished&&h.voice.hfpStatus().state==owned::HfpState::Ready);REQUIRE(h.brsf==1);
 h.open(2);for(unsigned k=0;k<10;++k)h.tick();REQUIRE(h.voice.hspStatus().connected);REQUIRE(!h.voice.closeRequested());
 REQUIRE(h.voice.stats().received_frames==h.voice.stats().consumed_frames);
}
void copied_queue_pressure(){Harness h;for(unsigned k=0;k<3;++k){bth_control_reservation_t s{};REQUIRE(bth_control_tx_reserve(&h.queue,0,&s)==BTH_OK);s.data[0]=uint8_t(k);REQUIRE(bth_control_tx_commit(&h.queue,s.token,0x90,1,672)==BTH_OK);}
 size_t n=0;REQUIRE(bth_rfcomm_start(&h.peer,19,h.now,h.reply,sizeof h.reply,&n)==BTH_OK);h.send(h.reply,n);
 REQUIRE(h.voice.pendingOutput());const auto old=h.voice.stats();for(unsigned k=0;k<20;++k)h.voice.poll(true,++h.now);REQUIRE(h.voice.pendingOutput());REQUIRE(h.voice.stats().transmitted_frames==old.transmitted_frames);
 // Drain the unrelated FIFO prefix using the real transport and real NCP accounting.
 const uint32_t owners[]={7,0};while(bth_control_tx_next(&h.queue,owners,&h.flow,h.wire,sizeof h.wire,&n)==BTH_OK)h.complete();
 for(unsigned k=0;k<5;++k)h.tick();REQUIRE(h.peer.mux==BTH_RFCOMM_OPEN);REQUIRE(!h.voice.closeRequested());
}
void stale_and_expiry(){Harness h;h.auto_answer=false;h.open(1);for(unsigned k=0;k<10;++k)h.tick();REQUIRE(h.at.find("AT+BRSF=16\r")!=std::string::npos);const uint32_t token=h.voice.hfpStatus().generation;REQUIRE(token);
 REQUIRE(h.voice.commandHfp(token+1,owned::HfpCommand::Answer)==BTH_IGNORED);
 h.voice.poll(true,h.now+5001);REQUIRE(h.voice.closeRequested());
 h.voice.channelClosed(0x40);REQUIRE(!h.voice.hfpStatus().generation);REQUIRE(h.voice.commandHfp(token,owned::HfpCommand::Answer)==BTH_IGNORED);
 REQUIRE(!h.voice.channelOpened(8,0x41,0x81,672));
}
}
int main(int argc,char** argv){std::string which=argc>1?argv[1]:"all";if(which=="all"||which=="slc")dual_listener_and_slc();if(which=="all"||which=="pressure")copied_queue_pressure();if(which=="all"||which=="expiry")stale_and_expiry();std::printf("VOICE_OWNER_PROTOCOL checks=%u\n",checks);return 0;}
