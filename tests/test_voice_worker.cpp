#include <atomic>
#define private public
#include "../src/owned/VoiceAudioWorker.h"
#undef private
#include "../src/detail/Memory.h"
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
namespace {
unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
struct Output {unsigned starts=0,ends=0,wakes=0,frames=0;bool busy=false;};
struct Mic {unsigned calls=0;bool retired=false,block_publication=true;VoiceAudioWorker* worker=nullptr;};
}
extern "C" unsigned voiceWorkerTest(){
 checks=0;Output output;VoiceWorkerOutput io;io.user=&output;
 io.prepare=[](VoiceCodec,void* p){++static_cast<Output*>(p)->starts;return Status::Ok;};
 io.restore=[](void* p){++static_cast<Output*>(p)->ends;};
 io.wake=[](void* p){++static_cast<Output*>(p)->wakes;};
 io.sink={&output,[](void* p,const VoicePcmRequest&,const uint8_t*,size_t n){auto& o=*static_cast<Output*>(p);if(o.busy)return VoicePcmRead{0,VoiceIoResult::WouldBlock};o.frames+=unsigned(n);return VoicePcmRead{n,VoiceIoResult::Ready};}};
 VoiceAudioWorker worker(93);CHECK(worker.begin(io)==Status::Ok);auto h=worker.hooks();const size_t before=memoryStats().current_blocks;
 CHECK(h.prepare(1,VoiceCodec::Cvsd,h.user)==Status::Busy&&output.starts==0&&memoryStats().current_blocks==before);CHECK(!worker.preempting());worker.step();CHECK(worker.preempting()&&output.starts==1&&h.prepare(1,VoiceCodec::Cvsd,h.user)==Status::Ok);
 CHECK(memoryStats().current_blocks==before+1);h.request_tx(1,1,h.user);worker.step();uint8_t bytes[60];size_t n=0;uint32_t ticket=0;CHECK(h.copy_tx(1,bytes,sizeof bytes,n,ticket,h.user)&&n==60);for(auto b:bytes)CHECK(b==0);CHECK(h.consume_tx(1,ticket,n,h.user));
 Mic mic;mic.worker=&worker;VoiceInputToken token;VoicePcmProvider provider;provider.context=&mic;provider.available=[](void*,const VoicePcmRequest& r){return size_t(r.format.framesPerUnit);};provider.read=[](void* p,const VoicePcmRequest&,uint8_t* b,size_t frames){auto& m=*static_cast<Mic*>(p);if(m.retired)return VoicePcmRead{0,VoiceIoResult::Invalid};++m.calls;std::memset(b,0x55,frames*2);if(m.block_publication){m.block_publication=false;m.worker->flag_.test_and_set(std::memory_order_acquire);}return VoicePcmRead{frames,VoiceIoResult::Ready};};
 CHECK(worker.setInput(provider,token)==Status::Ok);h.request_tx(1,1,h.user);worker.step();CHECK(mic.calls==1&&worker.pending_tx_bytes_==60);worker.flag_.clear(std::memory_order_release);worker.step();CHECK(h.copy_tx(1,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==0x55&&mic.calls==1);CHECK(h.consume_tx(1,ticket,n,h.user));CHECK(worker.detachInput(token)==Status::Ok);mic.retired=true;
 h.request_tx(1,1,h.user);worker.step();CHECK(h.copy_tx(1,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==0&&mic.calls==1);CHECK(h.consume_tx(1,ticket,n,h.user));
 output.busy=true;CHECK(h.receive(1,0,bytes,sizeof bytes,h.user));worker.step();CHECK(!output.frames);output.busy=false;worker.step();CHECK(output.frames==30);
 h.ended(1,h.user);worker.step();CHECK(output.ends==1&&!worker.preempting()&&memoryStats().current_blocks==before);
 CHECK(h.prepare(2,VoiceCodec::Msbc,h.user)==Status::Busy);worker.step();CHECK(h.prepare(2,VoiceCodec::Msbc,h.user)==Status::Ok&&memoryStats().current_blocks==before+2);h.request_tx(2,1,h.user);worker.step();CHECK(h.copy_tx(2,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==1&&bytes[2]==0xad&&mic.calls==1);h.ended(2,h.user);worker.step();CHECK(memoryStats().current_blocks==before);worker.end();return 0;
}
extern "C" unsigned voiceWorkerChecks(){return checks;}
