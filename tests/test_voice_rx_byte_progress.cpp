#include <cstring>
#define private public
#include "../src/owned/OwnedVoiceAudio.h"
#undef private
// Reuse the actual pipeline/mailbox boundary from the existing adapter test.
#include "test_owned_voice_audio.cpp"
namespace {
struct ByteSession {
 OwnedVoiceAudio audio;Worker w;uint8_t arena[256]{};uint32_t now=100;unsigned bytes=0,packets=0;
 ByteSession(uint8_t maximum,VoiceCodec codec=VoiceCodec::Cvsd){
  VoiceAudioHostHooks h;h.prepare=prepare;h.ended=ended;h.request_tx=requestTx;h.copy_tx=copyTx;h.consume_tx=consumeTx;h.receive=receive;h.user=&w;VoiceAudioHostConfig c;c.allow_rx_pacing=true;
  if(audio.begin(c,h,arena,sizeof arena)!=Status::Ok)return;const uint8_t features[]={0xbf,0xfe,0xcf,0xfe,0xdb,0xff,0x7b,0x87};audio.controllerInfo(15,features,maximum,2);audio.startSetup();
  for(unsigned i=0;i<8&&!audio.setupDone();++i){bth_sco_setup_command_t command{};if(audio.setupNext(now,command)!=BTH_OK)return;if(audio.setupQueued(command.ticket,now)!=BTH_OK)return;uint8_t wire[80]={4,14,0,1,uint8_t(command.opcode),uint8_t(command.opcode>>8)},data[65]{};size_t n=1;
   if(command.opcode==0x1002)n=65;else if(command.opcode==0xfc1d){data[1]=1;n=6;}else if(command.opcode==0x0c2e)n=2;else return;wire[2]=uint8_t(n+3);std::memcpy(wire+6,data,n);audio.setupEvent(event(wire,n+6),++now);
  }
  const uint8_t peer[]={1,2,3,4,5,6};audio.attach(7,0x25,peer);const uint8_t request[]={4,4,10,1,2,3,4,5,6,0,0,0,2};audio.event(event(request,sizeof request),true,true,codec,++now,0);tick();audio.poll(true,++now);bth_sco_command_t command{};if(audio.nextCommand(command)!=BTH_OK)return;audio.commandQueued(command.ticket,now);const uint8_t status[]={4,15,4,0,1,0x29,4};audio.event(event(status,sizeof status),true,true,codec,++now,7);uint8_t complete[]={4,0x2c,17,0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2};complete[19]=codec==VoiceCodec::Msbc?3:2;audio.event(event(complete,sizeof complete),true,true,codec,++now,0);w.service();
 }
 ~ByteSession(){audio.end();w.pipeline.end();}
 void tick(){audio.poll(true,++now);for(unsigned i=0;i<3;++i){const auto result=w.pipeline.poll(w.generation);if(result!=Status::Ok)break;}w.service();}
 bth_status_t feed(size_t n,uint8_t status=0){uint8_t payload[64]{};bth_sco_view_t p{0x33,status,{payload,n}};const auto result=audio.receive(p,true);tick();return result;}
 unsigned drain(){const unsigned before=bytes;for(unsigned i=0;i<16;++i){w.service();uint8_t out[80];size_t n=0;VoiceAudioSubmission receipt{};const auto result=audio.prepareSubmission(out,sizeof out,n,receipt);if(result!=BTH_OK)break;if(audio.commitSubmission(receipt)!=BTH_OK)break;bytes+=receipt.bytes;++packets;}return bytes-before;}
};
}
extern "C" unsigned voiceRxByteProgressTest(){
 checks=0;
 const uint8_t maxima[]={20,30,40};for(uint8_t maximum:maxima){ByteSession s(maximum);CHECK(s.audio.active());for(unsigned i=0;i<32;++i){CHECK(s.feed(60)==BTH_OK);CHECK(s.drain()==60);}CHECK(s.bytes==1920&&!s.audio.state().tx_available&&!s.audio.state().tx_outstanding);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==33*30);}
 {ByteSession s(22,VoiceCodec::Msbc);CHECK(s.audio.active());CHECK(s.feed(60)==BTH_OK);CHECK(s.drain()==60&&s.packets==3);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==240);}
 {ByteSession s(64);CHECK(s.feed(30)==BTH_OK);CHECK(s.drain()==0);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==30);CHECK(s.feed(30)==BTH_OK);CHECK(s.drain()==60);}
 const size_t incoming_sizes[]={48,64};for(size_t incoming:incoming_sizes){ByteSession s(64);CHECK(s.audio.active());for(unsigned i=0;i<60;++i){CHECK(s.feed(incoming)==BTH_OK);s.drain();}CHECK(s.bytes==60*incoming);CHECK(s.audio.state().rx_pacing_bytes==0);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==(1+s.bytes/60)*30);}
 {ByteSession s(64);CHECK(s.feed(64)==BTH_OK);CHECK(s.feed(64)==BTH_OK);CHECK(s.drain()==120);CHECK(s.audio.state().rx_pacing_bytes==8);CHECK(s.feed(52)==BTH_OK);CHECK(s.drain()==60);CHECK(!s.audio.state().rx_pacing_bytes);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==120);}
 {ByteSession s(64);CHECK(s.feed(0,2)==BTH_OK);CHECK(!s.drain());CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==30);CHECK(s.feed(60,2)==BTH_OK);CHECK(s.drain()==60);}
 {ByteSession s(64);for(unsigned i=0;i<4;++i)CHECK(s.feed(64)==BTH_OK);CHECK(s.feed(64)==BTH_ERR_CAPACITY);CHECK(s.audio.state().rx_pacing_overflows==1&&s.audio.state().rx_pacing_rejected_bytes==64&&s.audio.state().state==BTH_SCO_DISCONNECTING);}
 {ByteSession s(30);CHECK(s.feed(60)==BTH_OK);uint8_t out[80];size_t n=0;VoiceAudioSubmission receipt{};CHECK(s.audio.prepareSubmission(out,sizeof out,n,receipt)==BTH_OK);auto forged=receipt;forged.bytes=28;CHECK(s.audio.commitSubmission(forged)==BTH_ERR_PARAM&&s.w.cursor==0);CHECK(s.audio.commitSubmission(receipt)==BTH_OK&&s.w.cursor==30);CHECK(s.audio.commitSubmission(receipt)==BTH_IGNORED&&s.w.cursor==30);CHECK(s.drain()==30);CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==60);}
 {ByteSession s(64);CHECK(s.feed(60)==BTH_OK);uint8_t out[80];size_t n=0;VoiceAudioSubmission receipt{};CHECK(s.audio.prepareSubmission(out,sizeof out,n,receipt)==BTH_OK);s.w.consume_busy=true;CHECK(s.audio.commitSubmission(receipt)==BTH_OK&&s.w.queued);CHECK(s.audio.prepareSubmission(out,sizeof out,n,receipt)==BTH_MORE);s.tick();s.w.service();CHECK(s.w.pipeline.inputCounters().noMicSilenceFrames==60);CHECK(!s.drain());}
 return 0;
}
extern "C" unsigned voiceRxByteProgressChecks(){return checks;}
