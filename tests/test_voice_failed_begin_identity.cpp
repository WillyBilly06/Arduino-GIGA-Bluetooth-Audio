// Uses the existing deterministic fake frontend boundary; never a phone call.
#define main original_voice_frontend_fixture_main
#include "test_voice_frontend.cpp"
#undef main
namespace {
size_t available(void*,const VoicePcmRequest& q){return q.format.framesPerUnit;}
VoicePcmRead read(void*,const VoicePcmRequest&,uint8_t* data,size_t n){std::memset(data,0,n*2);return {n,VoiceIoResult::Ready};}
ReceiverConfig config(){ReceiverConfig c;c.maximum_packet_bytes=128;c.maximum_access_unit_bytes=128;c.queue_packets=2;c.startup_packets=1;c.voice.hands_free=true;return c;}
}
int main(){
 VoicePcmProvider provider{nullptr,available,read};VoiceInputToken first{},last{};auto cfg=config();GigaBredr a,b,c,d;
 if(a.begin(cfg)!=Status::Ok||a.setVoiceInput(provider,first)!=Status::Ok)return 2;a.end();
 detail::setAllocationFailureCountdown(0);Status firstFail=b.begin(cfg);detail::setAllocationFailureCountdown(-1);if(firstFail!=Status::OutOfMemory)return 3;
 detail::setAllocationFailureCountdown(1);Status secondFail=c.begin(cfg);detail::setAllocationFailureCountdown(-1);if(secondFail!=Status::OutOfMemory)return 4;
 if(d.begin(cfg)!=Status::Ok||d.setVoiceInput(provider,last)!=Status::Ok)return 5;
 if(first.lifetime>UINT64_MAX-3||last.lifetime!=first.lifetime+3||d.detachVoiceInput(first)!=Status::InvalidArgument||d.detachVoiceInput(last)!=Status::Ok)return 6;
 d.end();if(detail::memoryStats().current_blocks!=0)return 7;
 std::printf("VOICE_FAILED_BEGIN_IDENTITY_PASS old=%llu new=%llu failures_burned=2\n",(unsigned long long)first.lifetime,(unsigned long long)last.lifetime);return 0;
}
