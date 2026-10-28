// Independent token-owner RED/GREEN fixture. All radio/I2S behavior is the
// actual existing frontend test's deterministic fake boundary, never hardware.
#define main original_voice_frontend_fixture_main
#include "test_voice_frontend.cpp"
#undef main
#include <new>

namespace token_owner {
struct Mic {unsigned calls=0;bool retired=false;};
size_t available(void*,const VoicePcmRequest& r){return r.format.framesPerUnit;}
VoicePcmRead read(void* p,const VoicePcmRequest&,uint8_t* bytes,size_t n){
 auto& mic=*static_cast<Mic*>(p);++mic.calls;
 if(mic.retired)return {0,VoiceIoResult::Invalid};
 std::memset(bytes,0x66,n*2);return {n,VoiceIoResult::Ready};
}
unsigned assertions=0,failures=0;
void expect(bool value,const char* name,unsigned which){
 ++assertions;if(!value){++failures;std::printf("TOKEN_OWNER_RED case=%u check=%s\n",which,name);}
}
ReceiverConfig config(){ReceiverConfig c;c.maximum_packet_bytes=128;c.maximum_access_unit_bytes=128;c.queue_packets=2;c.startup_packets=1;c.voice.hands_free=true;return c;}
void run(unsigned which,bool same_address){
 alignas(GigaBredr) unsigned char first_storage[sizeof(GigaBredr)];
 alignas(GigaBredr) unsigned char other_storage[sizeof(GigaBredr)];
 auto* first=new(first_storage)GigaBredr;
 Mic old_mic,new_mic;VoiceInputToken old_token{},new_token{};
 const auto cfg=config();
 const auto first_begin=first->begin(cfg);expect(first_begin==Status::Ok,"first_begin",which);
 if(first_begin!=Status::Ok){first->~GigaBredr();return;}
 expect(first->setVoiceInput({&old_mic,available,read},old_token)==Status::Ok,"old_provider_registered",which);
 const auto old_address=reinterpret_cast<uintptr_t>(first);
 first->end();first->~GigaBredr();old_mic.retired=true;

 auto* current=new(same_address?first_storage:other_storage)GigaBredr;
 const auto second_begin=current->begin(cfg);expect(second_begin==Status::Ok,"second_begin",which);
 if(second_begin!=Status::Ok){current->~GigaBredr();return;}
 expect(current->setVoiceInput({&new_mic,available,read},new_token)==Status::Ok,"new_provider_registered",which);
 const auto stale=current->detachVoiceInput(old_token);
 expect(stale==Status::InvalidArgument,"old_receiver_token_rejected",which);

 // Exercise the actually registered provider through the real frontend/worker
 // path rather than inspecting registry fields or only comparing token bytes.
 auto hooks=current->impl_->voice->worker.hooks();const uint32_t generation=100+which;
 expect(hooks.prepare(generation,VoiceCodec::Cvsd,hooks.user)==Status::Busy,"voice_prepare_pending",which);
 current->poll();expect(hooks.prepare(generation,VoiceCodec::Cvsd,hooks.user)==Status::Ok,"voice_prepared",which);
 hooks.request_tx(generation,1,hooks.user);current->poll();
 uint8_t bytes[60]{};size_t count=0;uint32_t ticket=0;
 const bool copied=hooks.copy_tx(generation,bytes,sizeof bytes,count,ticket,hooks.user);
 expect(copied&&count==60,"new_provider_TX_present",which);
 expect(new_mic.calls==1&&old_mic.calls==0,"only_new_provider_called",which);
 bool pattern=true;for(uint8_t value:bytes)pattern=pattern&&value==0x66;
 expect(pattern,"old_token_did_not_replace_provider_with_silence",which);
 if(copied)expect(hooks.consume_tx(generation,ticket,count,hooks.user),"consume_first_TX",which);
 const auto current_detach=current->detachVoiceInput(new_token);
 expect(current_detach==Status::Ok,"current_token_still_valid",which);new_mic.retired=true;
 const unsigned calls_before=new_mic.calls;
 hooks.request_tx(generation,1,hooks.user);current->poll();
 count=0;ticket=0;const bool silence=hooks.copy_tx(generation,bytes,sizeof bytes,count,ticket,hooks.user);
 expect(silence&&count==60,"silence_after_real_detach",which);
 bool all_zero=true;for(uint8_t value:bytes)all_zero=all_zero&&value==0;
 expect(all_zero&&new_mic.calls==calls_before,"successful_detach_fences_provider",which);
 if(silence)expect(hooks.consume_tx(generation,ticket,count,hooks.user),"consume_silence_TX",which);
 std::printf("TOKEN_OWNER_CASE index=%u same_address=%u old_lifetime=%llu old_epoch=%u new_lifetime=%llu new_epoch=%u stale_status=%u new_calls=%u old_calls=%u current_detach=%u\n",which,
   unsigned(old_address==reinterpret_cast<uintptr_t>(current)),
   static_cast<unsigned long long>(old_token.lifetime),old_token.epoch,
   static_cast<unsigned long long>(new_token.lifetime),new_token.epoch,
   unsigned(stale),new_mic.calls,old_mic.calls,unsigned(current_detach));
 expect((old_address==reinterpret_cast<uintptr_t>(current))==same_address,"address_case_setup",which);
 hooks.ended(generation,hooks.user);current->poll();current->end();current->~GigaBredr();
 expect(!output_active&&detail::memoryStats().current_blocks==0,"teardown_no_live_allocations",which);
}
}
int main(){
 token_owner::run(1,false);token_owner::run(2,true);
 std::printf("VOICE_TOKEN_OWNER checks=%u failures=%u fake_radio_I2S=1 live_allocations=%llu\n",token_owner::assertions,token_owner::failures,
   static_cast<unsigned long long>(detail::memoryStats().current_blocks));
 return token_owner::failures?42:0;
}
