#include <future>
#include <thread>
#include <new>
#define main original_voice_frontend_main
#include "test_voice_frontend.cpp"
#undef main
int main(){
 GigaBredr r;HspCommandToken empty{};
 CHECK(r.headsetCommand(HspCommand::Button,empty)==Status::Unsupported);
 ReceiverConfig cfg;cfg.voice.headset=true;cfg.voice.hands_free=true;
 CHECK(r.begin(cfg)==Status::Ok);VoiceStatus status;
 CHECK(r.voiceStatus(status)==Status::Ok);auto token=status.headset_command_token;
 CHECK(token.lifetime&&token.generation==7&&hs_calls==0);
 CHECK(r.headsetCommand(static_cast<HspCommand>(255),token)==Status::InvalidArgument&&hs_calls==0);
 CHECK(r.headsetCommand(HspCommand::Button,{0,7})==Status::InvalidArgument&&hs_calls==0);
 CHECK(r.headsetCommand(HspCommand::Button,{token.lifetime,0})==Status::InvalidArgument&&hs_calls==0);
 CHECK(r.headsetCommand(HspCommand::Button,{token.lifetime+1,7})==Status::InvalidArgument&&hs_calls==0);
 std::promise<Status> result;auto future=result.get_future();r.impl_->radio_mutex.lock();
 std::thread caller([&]{result.set_value(r.headsetCommand(HspCommand::Button,token));});
 const bool bounded=future.wait_for(std::chrono::seconds(1))==std::future_status::ready;
 r.impl_->radio_mutex.unlock();const auto busy=future.get();caller.join();
 CHECK(bounded&&busy==Status::Busy&&hs_calls==0);
 CHECK(r.headsetCommand(HspCommand::Button,token)==Status::Ok&&hs_calls==1);
 hs_result=Status::Busy;CHECK(r.headsetCommand(HspCommand::Button,token)==Status::Busy&&hs_calls==2);hs_result=Status::Ok;
 hs_generation=8;CHECK(r.headsetCommand(HspCommand::Button,token)==Status::InvalidArgument&&hs_calls==3);
 CHECK(r.voiceStatus(status)==Status::Ok&&status.headset_command_token.generation==8);
 hs_connected=false;CHECK(r.voiceStatus(status)==Status::Ok&&!status.headset_command_token.lifetime&&!status.headset_command_token.generation);hs_connected=true;hs_generation=7;
 r.end();const auto before=hs_calls;CHECK(r.headsetCommand(HspCommand::Button,token)==Status::Unsupported&&hs_calls==before);
 CHECK(r.begin(cfg)==Status::Ok);CHECK(r.voiceStatus(status)==Status::Ok&&status.headset_command_token.generation==token.generation&&status.headset_command_token.lifetime!=token.lifetime);
 CHECK(r.headsetCommand(HspCommand::Button,token)==Status::InvalidArgument&&hs_calls==before);
 CHECK(r.headsetCommand(HspCommand::Button,status.headset_command_token)==Status::Ok&&hs_calls==before+1);r.end();
 cfg.voice.headset=false;CHECK(r.begin(cfg)==Status::Ok);CHECK(r.voiceStatus(status)==Status::Ok&&!status.headset_command_token.lifetime);
 CHECK(r.headsetCommand(HspCommand::Button,token)==Status::Unsupported);r.end();
 // Reconstruct at exactly the same address; scalar DLC generations may repeat.
 alignas(GigaBredr) unsigned char storage[sizeof(GigaBredr)];cfg.voice.headset=true;
 auto* a=new(storage)GigaBredr;CHECK(a->begin(cfg)==Status::Ok);CHECK(a->voiceStatus(status)==Status::Ok);auto old=status.headset_command_token;a->~GigaBredr();
 auto* b=new(storage)GigaBredr;CHECK(b->begin(cfg)==Status::Ok);CHECK(b->voiceStatus(status)==Status::Ok);
 const auto calls=hs_calls;CHECK(status.headset_command_token.generation==old.generation&&status.headset_command_token.lifetime!=old.lifetime);
 CHECK(b->headsetCommand(HspCommand::Button,old)==Status::InvalidArgument&&hs_calls==calls);b->~GigaBredr();
 CHECK(detail::memoryStats().current_blocks==0);
 std::printf("HSP_FACADE_PASS checks=%u synthetic_radio_I2S=1 same_address_lifetime_fenced=1 no_device_io=1\n",checks);return 0;
}
