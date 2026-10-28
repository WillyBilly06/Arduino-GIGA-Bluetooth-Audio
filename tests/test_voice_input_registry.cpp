#include "../src/owned/VoiceInputRegistry.h"
#include "../src/owned/VoiceAudioPipeline.h"
#include <cstdint>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
namespace {
unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Context {VoiceInputRegistry* registry;VoiceInputToken token{};unsigned calls=0;Status nested=Status::Ok;bool retired=false;};
size_t available(void*,const VoicePcmRequest&){return 30;}
VoicePcmRead read(void* p,const VoicePcmRequest&,uint8_t* data,size_t n){auto& c=*static_cast<Context*>(p);++c.calls;if(c.retired)return {0,VoiceIoResult::Invalid};c.nested=c.registry->detach(c.token);for(size_t i=0;i<n*2;++i)data[i]=0x55;return {n,VoiceIoResult::Ready};}
VoicePcmRead sink(void*,const VoicePcmRequest&,const uint8_t*,size_t n){return {n,VoiceIoResult::Ready};}
}
extern "C" unsigned voiceInputRegistryTest(){
 checks=0;VoiceInputRegistry registry(42);Context context{&registry};VoiceInputToken token{};CHECK(registry.set({&context,available,read},token)==Status::Ok);context.token=token;
 VoiceAudioPipeline pipeline;int16_t rx[32];uint8_t packet[64],out[60];VoicePipelineConfig config;config.sink={nullptr,sink};VoicePipelineBuffers buffers;buffers.rx_pcm=rx;buffers.rx_frames=32;buffers.packet=packet;buffers.packet_bytes=64;
 {
  VoiceInputLease lease;CHECK(registry.tryAcquire(lease));config.input_mode=lease.snapshot().mode;config.input=lease.snapshot().provider;CHECK(pipeline.begin(7,lease.snapshot().epoch,config,buffers)==Status::Ok);
  size_t n=0;CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Ok&&context.calls==1&&context.nested==Status::Busy);VoiceInputToken unchanged{99,99};CHECK(registry.set({&context,available,read},unchanged)==Status::Busy&&unchanged.lifetime==99&&unchanged.epoch==99);
 }
 CHECK(registry.detach(token)==Status::Ok);context.retired=true;CHECK(registry.detach(token)==Status::Ok);
 {
  VoiceInputLease lease;CHECK(registry.tryAcquire(lease));CHECK(lease.snapshot().mode==MicrophoneMode::None&&lease.snapshot().epoch>token.epoch);CHECK(pipeline.setInput(7,lease.snapshot().epoch,lease.snapshot().mode,lease.snapshot().provider)==Status::Ok);size_t n=0;CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Ok&&context.calls==1);for(auto b:out)CHECK(b==0);
 }
 Context replacement{&registry};VoiceInputToken next{};CHECK(registry.set({&replacement,available,read},next)==Status::Ok&&next.epoch>token.epoch);replacement.token=next;CHECK(registry.detach(token)==Status::InvalidArgument);CHECK(registry.detach({41,next.epoch})==Status::InvalidArgument);CHECK(registry.detach(next)==Status::Ok);pipeline.end();return 0;
}
extern "C" unsigned voiceInputRegistryChecks(){return checks;}
