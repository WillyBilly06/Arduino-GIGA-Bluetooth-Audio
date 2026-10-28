#include "../src/owned/OwnedVoiceAudio.h"
#include "../src/owned/VoiceAudioPipeline.h"
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
namespace {
unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Worker {
 uint32_t requested=0,generation=0,serial=1;VoiceCodec codec=VoiceCodec::Cvsd;unsigned prepare_calls=0,ended=0,demand=0,received=0;bool ready=false,queued=false,consume_busy=false;size_t cursor=0;
 uint8_t unit[60]{},last[64]{},packet[64]{},h2[60]{};int16_t rx[120]{},tx[120]{};VoiceAudioPipeline pipeline;
 void service(){
  if(requested&&!ready){VoicePipelineConfig c;c.format={codec,codec==VoiceCodec::Msbc?16000u:8000u,uint16_t(codec==VoiceCodec::Msbc?120:30),1};c.sink={nullptr,[](void*,const VoicePcmRequest&,const uint8_t*,size_t n){return VoicePcmRead{n,VoiceIoResult::Ready};}};VoicePipelineBuffers b;b.rx_pcm=rx;b.rx_frames=120;b.tx_pcm=tx;b.tx_frames=120;b.packet=packet;b.packet_bytes=64;b.h2=h2;b.h2_bytes=60;if(pipeline.begin(requested,1,c,b)==Status::Ok){generation=requested;ready=true;}}
  if(ready&&demand&&!queued){size_t n=0;if(pipeline.renderTx(generation,unit,sizeof unit,n)==Status::Ok){queued=true;cursor=0;--demand;++serial;}}
 }
};
Status prepare(uint32_t g,VoiceCodec c,void* p){auto& w=*static_cast<Worker*>(p);++w.prepare_calls;w.requested=g;w.codec=c;return w.ready&&w.generation==g?Status::Ok:Status::Busy;}
void ended(uint32_t g,void* p){auto& w=*static_cast<Worker*>(p);if(w.generation==g){w.pipeline.end();w.ready=w.queued=false;}++w.ended;}
void requestTx(uint32_t g,unsigned n,void* p){auto& w=*static_cast<Worker*>(p);if(w.generation==g)w.demand+=n;}
bool copyTx(uint32_t g,uint8_t* out,size_t cap,size_t& n,uint32_t& ticket,void* p){auto& w=*static_cast<Worker*>(p);if(!w.queued||w.generation!=g||cap<60-w.cursor)return false;n=60-w.cursor;std::memcpy(out,w.unit+w.cursor,n);ticket=w.serial;return true;}
bool consumeTx(uint32_t g,uint32_t ticket,size_t n,void* p){auto& w=*static_cast<Worker*>(p);if(w.consume_busy){w.consume_busy=false;return false;}if(w.generation!=g||!w.queued||ticket!=w.serial||n>60-w.cursor)return false;w.cursor+=n;if(w.cursor==60)w.queued=false;return true;}
bool receive(uint32_t g,uint8_t status,const uint8_t* data,size_t n,void* p){auto& w=*static_cast<Worker*>(p);if(w.generation!=g||!w.ready||n>sizeof w.last)return false;if(w.pipeline.receive(g,status,data,n)!=Status::Ok)return false;std::memcpy(w.last,data,n);++w.received;return true;}
bth_h4_frame_t event(const uint8_t* p,size_t n){bth_h4_frame_t v{};bth_h4_decode(p,n,&v);return v;}
}
extern "C" unsigned ownedVoiceAudioTest(){
 checks=0;OwnedVoiceAudio audio;Worker w;uint8_t storage[256]{};VoiceAudioHostConfig config;VoiceAudioHostHooks hooks;hooks.prepare=prepare;hooks.ended=ended;hooks.request_tx=requestTx;hooks.copy_tx=copyTx;hooks.consume_tx=consumeTx;hooks.receive=receive;hooks.user=&w;CHECK(audio.begin(config,hooks,storage,sizeof storage)==Status::Ok);
 const uint8_t features[]={0xbf,0xfe,0xcf,0xfe,0xdb,0xff,0x7b,0x87};audio.controllerInfo(15,features,64,2);audio.startSetup();uint32_t now=100;
 for(unsigned tries=0;tries<12&&!audio.setupDone();++tries){bth_sco_setup_command_t c{};CHECK(audio.setupNext(now,c)==BTH_OK);CHECK(audio.setupQueued(c.ticket,now)==BTH_OK);uint8_t wire[80]={4,14,0,1,uint8_t(c.opcode),uint8_t(c.opcode>>8)},data[65]{};size_t n=1;
  if(c.opcode==0x1002){data[11]=0x10;n=65;}else if(c.opcode==0xfc1d){data[1]=1;n=6;}else if(c.opcode==0x0c2e){data[1]=1;n=2;}
  wire[2]=uint8_t(n+3);std::memcpy(wire+6,data,n);CHECK(audio.setupEvent(event(wire,n+6),++now)==BTH_OK);
 }
 CHECK(audio.ready());const uint8_t peer[]={1,2,3,4,5,6};CHECK(audio.attach(7,0x25,peer)==Status::Ok);
 const uint8_t request[]={4,4,10,1,2,3,4,5,6,0,0,0,2};CHECK(audio.event(event(request,sizeof request),true,true,VoiceCodec::Cvsd,++now,0)==BTH_OK);
 audio.poll(true,++now);CHECK(w.requested&&!w.ready);w.service();CHECK(w.ready);audio.poll(true,++now);bth_sco_command_t command{};CHECK(audio.nextCommand(command)==BTH_OK&&command.opcode==0x0429);CHECK(audio.commandQueued(command.ticket,now)==BTH_OK);
 const uint8_t status[]={4,15,4,0,1,0x29,4};CHECK(audio.event(event(status,sizeof status),true,true,VoiceCodec::Cvsd,++now,7)==BTH_OK);
 const uint8_t complete[]={4,0x2c,17,0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2};CHECK(audio.event(event(complete,sizeof complete),true,true,VoiceCodec::Cvsd,++now,0)==BTH_OK);w.service();CHECK(w.queued&&w.pipeline.inputCounters().noMicSilenceFrames==30);
 uint8_t out[80];size_t n=0;VoiceAudioSubmission submission{};CHECK(audio.prepareSubmission(out,sizeof out,n,submission)==BTH_OK&&out[0]==3&&n==64);w.consume_busy=true;CHECK(audio.commitSubmission(submission)==BTH_OK&&w.queued);for(unsigned i=4;i<64;++i)CHECK(out[i]==0);CHECK(audio.prepareSubmission(out,sizeof out,n,submission)==BTH_MORE);audio.poll(true,++now);CHECK(!w.queued);
 const uint8_t duplicate[]={4,0x13,9,2,0x33,0,0,0,0x33,0,0,0};CHECK(audio.validateCompleted(event(duplicate,sizeof duplicate))==BTH_ERR_STATE&&audio.state().tx_outstanding==1);
 const uint8_t completed[]={4,0x13,5,1,0x33,0,1,0};CHECK(audio.completed(event(completed,sizeof completed))==BTH_OK&&w.demand==1);
 uint8_t payload[]={1,2,3,4};bth_sco_view_t data{0x33,0,{payload,sizeof payload}};CHECK(audio.receive(data,true)==BTH_OK);audio.poll(true,++now);CHECK(w.received==1&&!std::memcmp(w.last,payload,sizeof payload));CHECK(w.pipeline.poll(w.generation)==Status::Ok);
 audio.poll(true,10000);uint8_t odd=1;bth_sco_view_t invalid{0x33,0,{&odd,1}};CHECK(audio.receive(invalid,true)==BTH_ERR_LENGTH);audio.poll(true,10001);CHECK(audio.state().state==BTH_SCO_DISCONNECTING);
 const uint8_t close[]={4,5,4,0,0x33,0,0x13};CHECK(audio.event(event(close,sizeof close),true,true,VoiceCodec::Cvsd,10002,0)==BTH_OK&&!w.ready&&w.ended==1);audio.detach();return 0;
}
extern "C" unsigned ownedVoiceAudioChecks(){return checks;}
