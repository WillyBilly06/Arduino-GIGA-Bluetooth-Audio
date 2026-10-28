#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#define private public
#include "GigaBredr.h"
#undef private
#include "GigaBredr.cpp"
using namespace giga_bredr;
namespace {detail::RadioCallbacks callbacks;unsigned checks=0,ends=0;uint32_t output_rate=0;size_t output_frames=0;bool output_active=false;int32_t last_slot=0;
struct MicContext {GigaBredr* receiver;VoiceInputToken token;unsigned reads=0;Status reentry=Status::Ok;bool retired=false;};}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"CHECK line%d %s\n",__LINE__,#x);std::abort();}}while(0)
namespace giga_bredr { namespace detail {
uint32_t compiledCodecs(){return codecBit(Codec::Sbc);}
const DecoderOps* codecOperations(Codec){static DecoderOps ops={[](void** s,const CodecConfig&){*s=allocate(64);return *s?Status::Ok:Status::OutOfMemory;},[](void*,const uint8_t*,size_t,int32_t*,size_t){return DecodeResult{Status::NeedMoreData,0,0};},[](void* s){release(s);}};return &ops;}
Status radioBegin(const char*,uint32_t,const RadioCallbacks& c,const BleConfig&){callbacks=c;return Status::Ok;}
Status radioBegin(const char* n,uint32_t m,const RadioCallbacks& c){return radioBegin(n,m,c,{});}void radioPoll(){}void radioEnd(){callbacks={};}RadioStats radioStats(){return {};}BleStats radioBleStats(){return {};}
Status radioBleNotify(const uint8_t*,size_t){return Status::Unsupported;}bool radioBleReadWrite(BleWrite&){return false;}Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}Status radioBleResumeAdvertising(){return Status::Unsupported;}
AvrcpStatus radioAvrcpStatus(){return {};}AvrcpMetadata radioAvrcpMetadata(){return {};}Status radioAvrcpCommand(AvrcpCommand,uint32_t,uint32_t&){return Status::Unsupported;}Status radioAvrcpCancel(uint32_t,uint32_t){return Status::Unsupported;}
Status radioVoiceStatus(VoiceStatus& s){s.enabled=s.controller_ready=s.backend_available=true;s.generation=2;return Status::Ok;}Status radioVoiceCommand(HfpCommand,uint32_t generation,const char*){return generation==7?Status::Ok:Status::InvalidArgument;}
Status i2sBegin(uint32_t r,uint16_t,const I2sPins&,uint16_t){CHECK(!output_active);output_rate=r;output_active=true;return Status::Ok;}Status i2sBegin(uint32_t r,uint16_t ms,const I2sPins& p){return i2sBegin(r,ms,p,0);}Status i2sBegin(uint32_t r,uint16_t ms){return i2sBegin(r,ms,I2sPins{},0);}Status i2sBegin(uint32_t r){return i2sBegin(r,20);}
size_t i2sWrite(const int32_t* p,size_t frames){CHECK(output_active);output_frames+=frames;if(frames){last_slot=p[2*frames-2];CHECK(last_slot==p[2*frames-1]);}return frames;}void i2sEnd(){output_active=false;++ends;}OutputStats i2sStats(){OutputStats s;s.initialized=s.running=output_active;s.sample_rate=output_rate;return s;}OutputTimingSnapshot i2sTiming(){return {};}PcmRecoverySnapshot i2sRecovery(){return {};}
}}
int main(){GigaBredr r;ReceiverConfig cfg;VoiceInputToken token;VoicePcmProvider provider;MicContext mic{&r,{}};provider.context=&mic;provider.available=[](void*,const VoicePcmRequest& q){return size_t(q.format.framesPerUnit);};provider.read=[](void* p,const VoicePcmRequest&,uint8_t* bytes,size_t frames){auto& m=*static_cast<MicContext*>(p);CHECK(!m.retired);++m.reads;m.reentry=m.receiver->detachVoiceInput(m.token);std::memset(bytes,0x55,frames*2);return VoicePcmRead{frames,VoiceIoResult::Ready};};
 CHECK(r.setVoiceInput(provider,token)==Status::Unsupported);cfg.maximum_packet_bytes=128;cfg.maximum_access_unit_bytes=128;cfg.queue_packets=2;cfg.startup_packets=1;cfg.voice.hands_free=true;cfg.voice.headset=true;CHECK(r.begin(cfg)==Status::Ok);CHECK(callbacks.voice_audio&&callbacks.voice_control&&callbacks.voice_sdp);
 CodecConfig music;music.codec=Codec::Sbc;music.sample_rate=48000;music.channels=2;music.bits_per_sample=16;CHECK(callbacks.configure(music,callbacks.user)==Status::Ok);callbacks.stream(true,callbacks.user);CHECK(output_rate==48000&&output_active);
 CHECK(r.setVoiceInput(provider,token)==Status::Ok);mic.token=token;auto h=r.impl_->voice->worker.hooks();detail::setAllocationFailureCountdown(0);CHECK(h.prepare(1,VoiceCodec::Cvsd,h.user)==Status::Busy);r.poll();CHECK(h.prepare(1,VoiceCodec::Cvsd,h.user)==Status::OutOfMemory&&output_rate==48000&&r.impl_->playing.load());detail::setAllocationFailureCountdown(-1);h.ended(1,h.user);r.poll();
 CHECK(h.prepare(2,VoiceCodec::Cvsd,h.user)==Status::Busy);r.poll();CHECK(h.prepare(2,VoiceCodec::Cvsd,h.user)==Status::Ok&&output_rate==16000&&output_active&&!r.impl_->playing.load());
 h.request_tx(2,1,h.user);r.poll();uint8_t bytes[60];size_t n=0;uint32_t ticket=0;CHECK(h.copy_tx(2,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==0x55&&mic.reads==1&&mic.reentry==Status::Busy);CHECK(h.consume_tx(2,ticket,n,h.user));
 h.gains(7,0,h.user);h.request_tx(2,1,h.user);r.poll();CHECK(h.copy_tx(2,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==0&&mic.reads==2);CHECK(h.consume_tx(2,ticket,n,h.user));CHECK(r.detachVoiceInput(token)==Status::Ok);mic.retired=true;
 h.request_tx(2,1,h.user);r.poll();CHECK(h.copy_tx(2,bytes,sizeof bytes,n,ticket,h.user)&&bytes[0]==0&&mic.reads==2);CHECK(h.consume_tx(2,ticket,n,h.user));CHECK(h.receive(2,0,bytes,sizeof bytes,h.user));r.poll();CHECK(output_frames==60);
 for(unsigned i=0;i<30;++i){bytes[2*i]=0x10;bytes[2*i+1]=0x27;}for(unsigned i=0;i<3;++i){CHECK(h.receive(2,0,bytes,sizeof bytes,h.user));r.poll();}CHECK(last_slot==int32_t(int64_t(10000)*65536*7/15));
 music.sample_rate=32000;CHECK(callbacks.configure(music,callbacks.user)==Status::Ok&&output_rate==16000);callbacks.stream(false,callbacks.user);callbacks.stream(true,callbacks.user);CHECK(output_rate==16000);
 VoiceStatus s;CHECK(r.voiceStatus(s)==Status::Ok&&s.input.noMicSilenceFrames==30);r.impl_->radio_mutex.lock();CHECK(r.voiceCommand(HfpCommand::Answer,7)==Status::Busy);r.impl_->radio_mutex.unlock();CHECK(r.voiceCommand(HfpCommand::Answer,7)==Status::Ok);
 h.ended(2,h.user);r.poll();CHECK(output_rate==32000&&output_active&&r.impl_->playing.load()&&!r.impl_->voice->worker.preempting());r.end();CHECK(!output_active&&detail::memoryStats().current_blocks==0);CHECK(r.begin(cfg)==Status::Ok);CHECK(r.detachVoiceInput(token)==Status::InvalidArgument);r.end();CHECK(detail::memoryStats().current_blocks==0);std::printf("VOICE_FRONTEND checks=%u reads=%u ends=%u\n",checks,mic.reads,ends);return 0;}
