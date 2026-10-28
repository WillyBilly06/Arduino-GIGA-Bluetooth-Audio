// Pure CPU protocol fixture: no Bluetooth/media/device APIs or real-time sleeps.
// Expected-rate contract is PCM/codec format, not a second pacing implementation.
#include "../src/owned/OwnedVoiceAudio.h"
#include "../src/owned/VoiceAudioPipeline.h"
#include "../src/detail/Memory.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
struct WorkerMailbox {
 VoiceAudioPipeline pipeline;uint32_t requested=0,generation=0,serial=1;VoiceCodec codec=VoiceCodec::Cvsd;
 unsigned demand=0;bool ready=false,queued=false;size_t cursor=0;uint8_t unit[60]{},reference[60]{},packet[64]{},h2[60]{};int16_t rx[120]{},tx[120]{};
 void step(){
  if(requested&&!ready){VoicePipelineConfig c;c.format={codec,codec==VoiceCodec::Msbc?16000u:8000u,uint16_t(codec==VoiceCodec::Msbc?120:30),1};c.sink={nullptr,[](void*,const VoicePcmRequest&,const uint8_t*,size_t n){return VoicePcmRead{n,VoiceIoResult::Ready};}};VoicePipelineBuffers b;b.rx_pcm=rx;b.rx_frames=120;b.tx_pcm=tx;b.tx_frames=120;b.packet=packet;b.packet_bytes=64;b.h2=h2;b.h2_bytes=60;assert(pipeline.begin(requested,1,c,b)==Status::Ok);generation=requested;ready=true;}
  if(ready){for(unsigned n=0;n<3;++n){const auto status=pipeline.poll(generation);assert(status==Status::Ok||status==Status::NeedMoreData||status==Status::Busy);if(status!=Status::Ok)break;}}
  if(ready&&demand&&!queued){size_t n=0;assert(pipeline.renderTx(generation,unit,sizeof unit,n)==Status::Ok&&n==60);std::memcpy(reference,unit,60);queued=true;cursor=0;--demand;++serial;}
 }
};
static bth_h4_frame_t frame(const uint8_t* p,size_t n){bth_h4_frame_t v{};assert(bth_h4_decode(p,n,&v)==BTH_OK);return v;}
struct Session {
 OwnedVoiceAudio audio;WorkerMailbox worker;uint8_t arena[256]{};uint32_t now=100;bool ncp;VoiceCodec codec;unsigned tx_packets=0,tx_bytes=0,ncp_events=0;
 Session(uint8_t maximum,bool completions,VoiceCodec selected):ncp(completions),codec(selected){
  VoiceAudioHostHooks h;h.user=&worker;
  h.prepare=[](uint32_t g,VoiceCodec c,void* p){auto& w=*static_cast<WorkerMailbox*>(p);w.requested=g;w.codec=c;return w.ready&&w.generation==g?Status::Ok:Status::Busy;};
  h.ended=[](uint32_t g,void* p){auto& w=*static_cast<WorkerMailbox*>(p);if(w.generation==g){w.pipeline.end();w.ready=w.queued=false;}};
  h.request_tx=[](uint32_t g,unsigned count,void* p){auto& w=*static_cast<WorkerMailbox*>(p);assert(g==w.generation);w.demand+=count;};
  h.copy_tx=[](uint32_t g,uint8_t* out,size_t capacity,size_t& n,uint32_t& ticket,void* p){auto& w=*static_cast<WorkerMailbox*>(p);if(g!=w.generation||!w.queued||capacity<60-w.cursor)return false;n=60-w.cursor;std::memcpy(out,w.unit+w.cursor,n);ticket=w.serial;return true;};
  h.consume_tx=[](uint32_t g,uint32_t ticket,size_t n,void* p){auto& w=*static_cast<WorkerMailbox*>(p);assert(g==w.generation&&ticket==w.serial&&w.queued&&n<=60-w.cursor);w.cursor+=n;if(w.cursor==60)w.queued=false;return true;};
  h.receive=[](uint32_t g,uint8_t status,const uint8_t* bytes,size_t n,void* p){auto& w=*static_cast<WorkerMailbox*>(p);const auto result=w.pipeline.receive(g,status,bytes,n);assert(result==Status::Ok||result==Status::OutputFull);return result==Status::Ok;};
  VoiceAudioHostConfig config;config.allow_rx_pacing=true;assert(audio.begin(config,h,arena,sizeof arena)==Status::Ok);
  const uint8_t features[]={0xbf,0xfe,0xcf,0xfe,0xdb,0xff,0x7b,0x87};audio.controllerInfo(15,features,maximum,2);audio.startSetup();
  for(unsigned tries=0;tries<8&&!audio.setupDone();++tries){bth_sco_setup_command_t command{};assert(audio.setupNext(now,command)==BTH_OK);assert(audio.setupQueued(command.ticket,now)==BTH_OK);uint8_t wire[80]={4,14,0,1,uint8_t(command.opcode),uint8_t(command.opcode>>8)},parameters[65]{};size_t n=1;
   if(command.opcode==0x1002)n=65;else if(command.opcode==0xfc1d){parameters[1]=1;n=6;}else if(command.opcode==0x0c2e){parameters[1]=ncp?1:0;n=2;}else assert(false);
   wire[2]=uint8_t(n+3);std::memcpy(wire+6,parameters,n);assert(audio.setupEvent(frame(wire,n+6),++now)==BTH_OK);
  }
  assert(audio.ready());const uint8_t peer[]={1,2,3,4,5,6};assert(audio.attach(7,0x25,peer)==Status::Ok);
  const uint8_t request[]={4,4,10,1,2,3,4,5,6,0,0,0,2};assert(audio.event(frame(request,sizeof request),true,true,codec,++now,0)==BTH_OK);
  audio.poll(true,++now);worker.step();audio.poll(true,++now);bth_sco_command_t accept{};assert(audio.nextCommand(accept)==BTH_OK&&accept.opcode==0x0429);assert(bth_read_le16(accept.parameters+16)==(codec==VoiceCodec::Cvsd?0x0060:0x0043));assert(audio.commandQueued(accept.ticket,now)==BTH_OK);
  const uint8_t status[]={4,15,4,0,1,0x29,4};assert(audio.event(frame(status,sizeof status),true,true,codec,++now,7)==BTH_OK);
  uint8_t complete[]={4,0x2c,17,0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2};complete[19]=codec==VoiceCodec::Cvsd?2:3;assert(audio.event(frame(complete,sizeof complete),true,true,codec,++now,0)==BTH_OK);assert(audio.active());worker.step();assert(worker.queued&&audio.state().interval_slots==6&&audio.state().air_tx_bytes==30&&audio.state().config.controller_tx_flow==ncp);
 }
 ~Session(){audio.end();worker.pipeline.end();}
 unsigned drain(){unsigned packets=0;for(unsigned bounded=0;bounded<4;++bounded){worker.step();uint8_t bytes[80];size_t n=0;VoiceAudioSubmission receipt{};const auto result=audio.prepareSubmission(bytes,sizeof bytes,n,receipt);if(result==BTH_MORE||result==BTH_IGNORED)break;assert(result==BTH_OK&&bytes[0]==3&&bytes[3]>0&&n==size_t(bytes[3])+4);assert(bth_read_le16(bytes+1)==0x33);assert(audio.commitSubmission(receipt)==BTH_OK);tx_bytes+=bytes[3];++tx_packets;++packets;}return packets;}
 void window(unsigned index,uint32_t period_us){
  now=1000+uint32_t((uint64_t(index)*period_us)/1000);worker.step();uint8_t payload[60]{};
  if(codec==VoiceCodec::Msbc){std::memcpy(payload,worker.reference,60);static constexpr uint8_t h2sequence[]={0x08,0x38,0xc8,0xf8};payload[1]=h2sequence[index&3];}
  bth_sco_view_t received{0x33,0,{payload,60}};assert(audio.receive(received,true)==BTH_OK);audio.poll(true,now);worker.step();const unsigned sent=drain();
  if(ncp){uint8_t completed[]={4,0x13,5,1,0x33,0,uint8_t(sent),0};assert(audio.completed(frame(completed,sizeof completed))==BTH_OK);++ncp_events;}
  else assert(!audio.state().tx_available&&!audio.state().tx_outstanding);
 }
};
int main(int argc,char** argv){
 const char* selector=argc>1?argv[1]:"cvsd_fragmented";const bool ncp=std::strcmp(selector,"ncp_fragmented")==0;const bool msbc=std::strcmp(selector,"msbc_fragmented")==0;const bool unfragmented=std::strcmp(selector,"cvsd_unfragmented")==0;
 assert(ncp||msbc||unfragmented||std::strcmp(selector,"cvsd_fragmented")==0);const uint8_t maximum=unfragmented?64:30;const uint32_t period_us=msbc?7500:3750,host_bytes_per_second=msbc?8000:16000;constexpr unsigned windows=64;
 {
  Session s(maximum,ncp,msbc?VoiceCodec::Msbc:VoiceCodec::Cvsd);for(unsigned i=0;i<windows;++i)s.window(i,period_us);const uint64_t duration=uint64_t(windows)*period_us;const unsigned expected=unsigned(duration*host_bytes_per_second/1000000);
  std::printf("VOICE_RX_PACING case=%s codec=%s hci_tx_max=%u rx_payload=60 air_tx=30 interval_slots=6 windows=%u duration_us=%llu expected_host_bytes_per_second=%u actual_payload_bytes=%u expected_payload_bytes=%u actual_hci_packets=%u ncp_events=%u no_mic_pcm_frames_rendered=%u\n",selector,msbc?"MSBC":"CVSD",maximum,windows,(unsigned long long)duration,host_bytes_per_second,s.tx_bytes,expected,s.tx_packets,s.ncp_events,s.worker.pipeline.inputCounters().noMicSilenceFrames);std::fflush(stdout);
  assert(s.tx_bytes==expected&&"fragmented RX pacing must preserve the nominal host byte rate without NCP grants");
 }
 assert(memoryStats().current_blocks==0&&memoryStats().current_bytes==0);return 0;
}
