#include "../src/owned/VoiceAudioPipeline.h"
#include "../src/detail/Memory.h"
#include "voice_vectors/msbc_reference.h"
#include <cstdint>
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
namespace {
unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Sink {uint8_t bytes[1600]{};size_t frames=0,limit=120;unsigned calls=0;bool correct=true;};
VoicePcmRead sinkWrite(void* p,const VoicePcmRequest& r,const uint8_t* data,size_t n){auto& s=*static_cast<Sink*>(p);++s.calls;s.correct=s.correct&&r.firstFrame==s.frames;size_t take=n<s.limit?n:s.limit;if(take*2>sizeof(s.bytes)-s.frames*2)return {0,VoiceIoResult::Invalid};std::memcpy(s.bytes+s.frames*2,data,take*2);s.frames+=take;return {take,take?VoiceIoResult::Ready:VoiceIoResult::WouldBlock};}
struct Mic {unsigned calls=0;};
size_t micAvailable(void*,const VoicePcmRequest&){return 30;}
VoicePcmRead micRead(void* p,const VoicePcmRequest&,uint8_t* data,size_t n){auto& m=*static_cast<Mic*>(p);++m.calls;for(size_t i=0;i<n*2;++i)data[i]=0x55;return {n,VoiceIoResult::Ready};}
}
extern "C" unsigned voicePipelineTest(){
 checks=0;VoiceAudioPipeline pipeline;Sink sink;int16_t rx[120],tx[120];uint8_t packet[64],h2[60],out[60];
 VoicePipelineConfig c;c.format={VoiceCodec::Cvsd,8000,30,1};c.max_rx_bytes=64;c.sink={&sink,sinkWrite};VoicePipelineBuffers b;b.rx_pcm=rx;b.rx_frames=120;b.packet=packet;b.packet_bytes=64;
 CHECK(pipeline.begin(7,1,c,b)==Status::Ok);size_t n=0;CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Ok&&n==60);for(auto value:out)CHECK(value==0);CHECK(pipeline.inputCounters().noMicSilenceFrames==30);
 Mic mic;CHECK(pipeline.setInput(7,2,MicrophoneMode::Provider,{&mic,micAvailable,micRead})==Status::Ok);CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Ok&&mic.calls==1);for(auto value:out)CHECK(value==0x55);
 uint8_t downlink[60];for(unsigned i=0;i<60;++i)downlink[i]=uint8_t(i);CHECK(pipeline.receive(7,0,downlink,sizeof downlink)==Status::Ok);sink.limit=5;CHECK(pipeline.poll(7)==Status::Ok&&sink.frames==5);sink.limit=0;for(unsigned i=0;i<8;++i)CHECK(pipeline.poll(7)==Status::Busy);CHECK(pipeline.stats().rx_units==1&&sink.frames==5);CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Ok&&mic.calls==2);
 sink.limit=120;CHECK(pipeline.poll(7)==Status::Ok&&sink.frames==30&&sink.correct);CHECK(!std::memcmp(sink.bytes,downlink,60));CHECK(pipeline.stats().rx_units==1);
 pipeline.end();CHECK(pipeline.renderTx(7,out,sizeof out,n)==Status::Unsupported&&mic.calls==2);
 // The same real pipeline now exercises a selected mSBC codec and H2 fragments.
 sink=Sink();c.format={VoiceCodec::Msbc,16000,120,1};b.tx_pcm=tx;b.tx_frames=120;b.h2=h2;b.h2_bytes=60;setAllocationFailureCountdown(0);CHECK(pipeline.begin(8,1,c,b)==Status::OutOfMemory);setAllocationFailureCountdown(-1);CHECK(pipeline.begin(8,1,c,b)==Status::Ok);
 uint8_t encoded[60];CHECK(VoiceMsbc::packH2(0,MsbcReferenceFrames,57,encoded,sizeof encoded)==Status::Ok);CHECK(pipeline.receive(8,0,encoded,30)==Status::Ok);CHECK(pipeline.poll(8)==Status::NeedMoreData&&!sink.frames);
 CHECK(pipeline.receive(8,0,encoded+30,30)==Status::Ok);CHECK(pipeline.poll(8)==Status::Ok&&sink.frames==120&&sink.correct);CHECK(pipeline.stats().rx_units==1&&!pipeline.stats().decode_errors);
 CHECK(pipeline.renderTx(8,out,sizeof out,n)==Status::Ok&&n==60&&out[0]==1&&out[1]==0x08&&out[2]==0xad);CHECK(pipeline.inputCounters().noMicSilenceFrames==120);
 pipeline.end();CHECK(pipeline.poll(8)==Status::Unsupported);return 0;
}
extern "C" unsigned voicePipelineChecks(){return checks;}
