#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define private public
#include "owned/VoiceAudioWorker.h"
#undef private
#include "detail/Memory.h"
using namespace giga_bredr; using namespace giga_bredr::detail; using namespace giga_bredr::owned;
struct State { VoiceAudioWorker* worker=nullptr; bool inject=true; unsigned prepares=0, restores=0; };
static void* alloc(size_t n,void* user){auto& s=*static_cast<State*>(user);if(s.inject){s.inject=false;s.worker->flag_.test_and_set(std::memory_order_acquire);}return std::malloc(n);}
static void freeBlock(void* p,void*){std::free(p);}
int main(){
 State s;VoiceAudioWorker worker(901);s.worker=&worker;
 if(!setMemoryProvider(MemoryKind::Fast,alloc,freeBlock,&s))return 2;
 VoiceWorkerOutput out;out.user=&s;
 out.prepare=[](VoiceCodec,void* p){++static_cast<State*>(p)->prepares;return Status::Ok;};
 out.restore=[](void* p){++static_cast<State*>(p)->restores;};out.wake=[](void*){};
 out.sink={&s,[](void*,const VoicePcmRequest&,const uint8_t*,size_t n){return VoicePcmRead{n,VoiceIoResult::Ready};}};
 bool ok=worker.begin(out)==Status::Ok;auto hooks=worker.hooks();
 ok=ok&&hooks.prepare(1,VoiceCodec::Cvsd,hooks.user)==Status::Busy;
 worker.step();
 bool pending=worker.generation_==1&&!worker.preempting()&&s.prepares==0&&memoryStats().current_blocks==1;
 worker.flag_.clear(std::memory_order_release);
 worker.step();
 bool resumed=worker.preempting()&&s.prepares==1&&hooks.prepare(1,VoiceCodec::Cvsd,hooks.user)==Status::Ok;
 hooks.ended(1,hooks.user);worker.step();
 bool retired=!worker.preempting()&&s.restores==1&&memoryStats().current_blocks==0;
 worker.end();ok=ok&&pending&&resumed&&retired&&memoryStats().current_blocks==0;
 std::printf("VOICE_ADMISSION_BUSY pending=%d retry=%d retired=%d\n",pending,resumed,retired);
 return ok?0:2;
}
