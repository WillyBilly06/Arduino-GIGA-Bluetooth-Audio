#include "owned/VoiceAudioWorker.h"
#include "detail/Memory.h"
#include <cstdio>
using namespace giga_bredr;using namespace giga_bredr::detail;using namespace giga_bredr::owned;
struct Output {unsigned starts=0,restores=0,wakes=0;Status result=Status::Ok;bool supersede=false,supersede_on_restore=false;VoiceAudioHostHooks hooks{};};
static VoiceWorkerOutput output(Output& o){
 VoiceWorkerOutput v;v.user=&o;
 v.prepare=[](VoiceCodec,void *p){auto& x=*static_cast<Output*>(p);++x.starts;if(x.supersede){x.supersede=false;(void)x.hooks.prepare(2,VoiceCodec::Cvsd,x.hooks.user);}return x.result;};
 v.restore=[](void *p){auto& x=*static_cast<Output*>(p);++x.restores;if(x.supersede_on_restore){x.supersede_on_restore=false;(void)x.hooks.prepare(2,VoiceCodec::Cvsd,x.hooks.user);}};
 v.wake=[](void *p){++static_cast<Output*>(p)->wakes;};
 v.sink={&o,[](void*,const VoicePcmRequest&,const uint8_t*,size_t n){return VoicePcmRead{n,VoiceIoResult::Ready};}};
 return v;
}
static bool before_takeover_oom(){
 Output o;VoiceAudioWorker w(101);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;
 const bool music_gate_remains_open_before_allocation=!w.preempting();
 setAllocationFailureCountdown(0);w.step();setAllocationFailureCountdown(-1);
 bool ok=music_gate_remains_open_before_allocation&&o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)==Status::OutOfMemory&&!w.preempting()&&o.starts==0&&o.restores==0&&memoryStats().current_blocks==before;
 w.end();return ok&&memoryStats().current_blocks==before;
}
static bool after_takeover_prepare_error(){
 Output o;o.result=Status::HardwareError;VoiceAudioWorker w(102);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;w.step();
 bool ok=o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)==Status::HardwareError&&!w.preempting()&&o.starts==1&&o.restores==1&&memoryStats().current_blocks==before;
 w.end();return ok&&memoryStats().current_blocks==before;
}
static bool failed_handover_releases_old_output(){
 Output o;VoiceAudioWorker w(103);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;w.step();
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Ok||o.starts!=1||!w.preempting())return false;
 if(o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy||!w.preempting())return false;
 setAllocationFailureCountdown(0);w.step();setAllocationFailureCountdown(-1);
 bool ok=o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::OutOfMemory&&!w.preempting()&&o.starts==1&&o.restores==1&&memoryStats().current_blocks==before;
 w.end();return ok&&memoryStats().current_blocks==before;
}
static bool superseded_failure_keeps_new_generation(){
 Output o;o.result=Status::HardwareError;o.supersede=true;VoiceAudioWorker w(104);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;w.step();
 bool ok=!w.preempting()&&o.starts==1&&o.restores==1&&o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::Busy;
 o.result=Status::Ok;w.step();ok=ok&&w.preempting()&&o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::Ok&&o.starts==2;
 o.hooks.ended(1,o.hooks.user);w.step();ok=ok&&w.preempting()&&o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::Ok;
 o.hooks.ended(2,o.hooks.user);w.step();ok=ok&&!w.preempting()&&o.restores==2;w.end();return ok&&memoryStats().current_blocks==before;
}
static bool cancel_before_start(){
 Output o;VoiceAudioWorker w(105);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;o.hooks.ended(1,o.hooks.user);w.step();
 bool ok=!w.preempting()&&o.starts==0&&o.restores==0;w.end();return ok&&memoryStats().current_blocks==before;
}
static bool cancel_restore_supersedes_generation(){
 Output o;VoiceAudioWorker w(106);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;w.step();
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Ok)return false;
 o.supersede_on_restore=true;o.hooks.ended(1,o.hooks.user);w.step();
 bool ok=!w.preempting()&&o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::Busy;
 w.step();ok=ok&&w.preempting()&&o.hooks.prepare(2,VoiceCodec::Cvsd,o.hooks.user)==Status::Ok;
 o.hooks.ended(2,o.hooks.user);w.step();ok=ok&&!w.preempting()&&o.restores==2;w.end();return ok&&memoryStats().current_blocks==before;
}
static bool pending_then_successful_takeover(){
 Output o;VoiceAudioWorker w(107);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;
 bool ok=!w.preempting()&&o.starts==0&&o.restores==0;w.step();
 ok=ok&&w.preempting()&&o.starts==1&&o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)==Status::Ok;
 o.hooks.ended(1,o.hooks.user);w.step();ok=ok&&!w.preempting()&&o.restores==1;w.end();return ok&&memoryStats().current_blocks==before;
}
static bool prepare_busy_restores_then_reopens_until_retry(){
 Output o;o.result=Status::Busy;VoiceAudioWorker w(108);if(w.begin(output(o))!=Status::Ok)return false;o.hooks=w.hooks();const size_t before=memoryStats().current_blocks;
 if(o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)!=Status::Busy)return false;
 bool ok=!w.preempting();w.step();ok=ok&&!w.preempting()&&o.starts==1&&o.restores==1;
 o.result=Status::Ok;w.step();ok=ok&&w.preempting()&&o.starts==2&&o.hooks.prepare(1,VoiceCodec::Cvsd,o.hooks.user)==Status::Ok;
 o.hooks.ended(1,o.hooks.user);w.step();ok=ok&&!w.preempting()&&o.restores==2;w.end();return ok&&memoryStats().current_blocks==before;
}
int main(){
 struct Case{const char *name;bool (*test)();};const Case cases[]={{"before_takeover_oom",before_takeover_oom},{"after_takeover_prepare_error",after_takeover_prepare_error},{"failed_handover_releases_old_output",failed_handover_releases_old_output},{"superseded_failure_keeps_new_generation",superseded_failure_keeps_new_generation},{"cancel_before_start",cancel_before_start},{"cancel_restore_supersedes_generation",cancel_restore_supersedes_generation},{"pending_then_successful_takeover",pending_then_successful_takeover},{"prepare_busy_restores_then_reopens_until_retry",prepare_busy_restores_then_reopens_until_retry}};
 unsigned failed=0;for(const auto &c:cases){bool ok=c.test();std::printf("VOICE_PREEMPT_CASE %s %s\n",c.name,ok?"PASS":"FAIL");if(!ok)failed++;}
 std::printf("VOICE_PREEMPT_SUMMARY total=8 failed=%u\n",failed);return failed?2:0;
}
