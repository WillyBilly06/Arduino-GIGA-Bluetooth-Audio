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
#include "hsp_hf_peer.inc"
struct HsPeer {
 VoiceProfiles voice;Fixture f;bth_rfcomm_t peer{};uint16_t cid=0;uint8_t reply[160]{};size_t read=0;std::string at;
 HsPeer(){f.host.end();f.config.voice_control=&voice;f.config.voice_config.headset=true;f.config.voice_config.hands_free=true;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();open();}
 ~HsPeer(){f.host.end();}
 void send(const uint8_t* p,size_t n){if(n)f.acl(0x25,cid,Bytes(p,p+n));}
 void tick(){
  f.poll();unsigned budget=8;while(budget--&&!f.tx.empty())f.acknowledge(f.pop());
  while(read<f.output.size()){const auto p=f.output[read++];if(p.cid!=0x90)continue;size_t n=0;bth_rfcomm_event_t event{};
   CHECK(bth_rfcomm_receive(&peer,19,f.now,p.bytes.data(),p.bytes.size(),reply,sizeof reply,&n,&event)>=BTH_OK);
   if(event.type==BTH_RFCOMM_DATA){CHECK(event.dlci==4);at.append(reinterpret_cast<const char*>(event.payload.data),event.payload.len);CHECK(bth_rfcomm_release(&peer,19,event.dlci,1)==BTH_OK);}send(reply,n);
  }
  size_t n=0;CHECK(bth_rfcomm_next(&peer,19,f.now,reply,sizeof reply,&n)>=BTH_OK);send(reply,n);
 }
 void ticks(unsigned count=30){for(unsigned i=0;i<count;++i)tick();}
 void open(){cid=f.channel(3,0x90);CHECK(cid&&voice.cid()==cid);f.output.clear();read=0;at.clear();CHECK(bth_rfcomm_init(&peer,19,true,0,127,2));
  size_t n=0;CHECK(bth_rfcomm_start(&peer,19,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);ticks(5);
  CHECK(bth_rfcomm_open(&peer,19,2,true,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
  for(unsigned i=0;i<200&&!voice.hspStatus().connected;++i)tick();ticks();CHECK(voice.hspStatus().connected&&voice.hspStatus().generation&&!voice.hfpStatus().generation&&at.empty());
 }
 void ok(){const char bytes[]="\r\nOK\r\n";size_t n=0;CHECK(bth_rfcomm_send(&peer,19,4,reinterpret_cast<const uint8_t*>(bytes),sizeof bytes-1,reply,sizeof reply,&n)==BTH_OK);send(reply,n);ticks();}
};
static void admission_and_wire(){
 HsPeer p;const auto g=p.voice.hspStatus().generation;
 CHECK(p.f.host.headsetCommand(static_cast<HspCommand>(255),g)==Status::InvalidArgument);
 CHECK(p.f.host.headsetCommand(HspCommand::Button,0)==Status::InvalidArgument);
 CHECK(p.f.host.headsetCommand(HspCommand::Button,g+1)==Status::InvalidArgument);
 CHECK(p.f.host.voiceCommand(HfpCommand::Answer,g,nullptr)==Status::InvalidArgument);
 p.f.host.deferred_count_=1;CHECK(p.f.host.headsetCommand(HspCommand::Button,g)==Status::Busy);p.f.host.deferred_count_=0;
 p.f.host.voice_retiring_=true;CHECK(p.f.host.headsetCommand(HspCommand::Button,g)==Status::Busy);p.f.host.voice_retiring_=false;
 p.ticks();CHECK(p.at.empty());CHECK(p.f.host.headsetCommand(HspCommand::Button,g)==Status::Ok);
 CHECK(p.f.host.headsetCommand(HspCommand::Button,g)==Status::Busy);p.ticks(100);CHECK(p.at=="AT+CKPD=200\r");
 CHECK(p.f.host.headsetCommand(HspCommand::Button,g)==Status::Busy);CHECK(p.voice.hspStatus().completedCommands==0);
 p.ok();CHECK(p.voice.hspStatus().completedCommands==1&&p.voice.hspStatus().lastCommandOk);
 CHECK(p.voice.hfpStatus().completedCommands==0&&!p.voice.hfpStatus().generation);p.ticks(100);CHECK(p.at=="AT+CKPD=200\r");
}
static void hf_is_not_hs(){
 HfPeer p(false);CHECK(p.voice.hfpStatus().slcEstablished&&!p.voice.hspStatus().connected);
 const auto before=p.at;CHECK(p.f.host.headsetCommand(HspCommand::Button,p.voice.hfpStatus().generation)==Status::InvalidArgument);
 for(unsigned i=0;i<30;++i)p.tick();CHECK(p.at==before&&p.at.find("CKPD")==std::string::npos);
}
static void unsent_teardown_and_reconnect(){
 HsPeer p;const auto old=p.voice.hspStatus().generation;
 CHECK(p.f.host.headsetCommand(HspCommand::Button,old)==Status::Ok);
 p.f.event(5,{0,0x25,0,0x13});p.f.settle();CHECK(!p.voice.hspStatus().generation&&p.at.empty());
 CHECK(p.f.host.headsetCommand(HspCommand::Button,old)==Status::InvalidArgument);
 p.f.classicConnect();p.open();const auto current=p.voice.hspStatus().generation;CHECK(current!=old);
 CHECK(p.f.host.headsetCommand(HspCommand::Button,old)==Status::InvalidArgument);
 CHECK(p.f.host.headsetCommand(HspCommand::Button,current)==Status::Ok);p.ticks();CHECK(p.at=="AT+CKPD=200\r");p.ok();
 // Loss of link security cannot admit a fresh HS intent.
 p.f.event(8,{0,0x25,0,0});p.f.settle();CHECK(p.f.host.headsetCommand(HspCommand::Button,current)==Status::InvalidArgument);
}
static void copied_button_retirement_fence(){
 HsPeer p;auto& f=p.f;const auto old=p.voice.hspStatus().generation;
 f.ready=false;CHECK(f.host.headsetCommand(HspCommand::Button,old)==Status::Ok);
 for(unsigned i=0;i<5;++i)p.voice.poll(true,++f.now);
 bool queued=false;CHECK(bth_control_tx_channel_pending(&f.host.control_,0,f.host.peers_[0].owner,0x90,&queued)==BTH_OK&&queued);
 CHECK(f.host.disconnectChannel(p.cid));f.host.synchronizeChannels();CHECK(!p.voice.cid());
 const auto* channel=bth_l2cap_sig_find(&f.host.signaling_,p.cid);CHECK(channel&&channel->state==BTH_CHANNEL_DISCONNECTING);const auto id=channel->disconnect_id;
 f.acl(0x25,1,{7,id,4,0,0x90,0,uint8_t(p.cid),uint8_t(p.cid>>8),2,32,4,0,3,0,0x91,0});f.poll();
 channel=bth_l2cap_sig_find(&f.host.signaling_,p.cid);CHECK(f.host.voice_retiring_&&channel&&channel->state==BTH_CHANNEL_DISCONNECTING);
 CHECK(f.host.headsetCommand(HspCommand::Button,old)==Status::Busy);
 f.ready=true;f.settle();CHECK(!f.host.voice_retiring_&&!f.host.radioStats().transport_errors);
 CHECK(f.host.headsetCommand(HspCommand::Button,old)==Status::InvalidArgument);
}
int main(){admission_and_wire();hf_is_not_hs();unsent_teardown_and_reconnect();copied_button_retirement_fence();
 std::printf("HSP_ACTOR_PASS checks=%u real_owned_host_RFCOMM=1 synthetic_controller=1 no_device_io=1\n",checks);return 0;}
