#pragma once
#include "OwnedVoiceAudio.h"
#include "VoiceAudioPipeline.h"
#include "VoiceInputRegistry.h"
#include <atomic>
namespace giga_bredr { namespace detail {
struct VoiceWorkerOutput {
 Status (*prepare)(owned::VoiceCodec,void*)=nullptr;
 void (*restore)(void*)=nullptr;void (*wake)(void*)=nullptr;
 owned::VoicePcmSink sink;void* user=nullptr;
};
struct VoiceWorkerSnapshot {uint32_t generation=0;owned::VoicePcmFormat format{owned::VoiceCodec::Cvsd,8000,30,1};VoicePipelineStats pipeline;owned::VoiceInputCounters input;Status status=Status::Busy;};
// One radio actor and one existing audio worker. Shared operations try a bounded
// mailbox lock once. Codec, allocation, I2S and microphone callbacks occur only
// in step()/end() after the worker released that mailbox. No extra thread.
class VoiceAudioWorker {
public:
 explicit VoiceAudioWorker(uint64_t lifetime):inputs_(lifetime){}
 ~VoiceAudioWorker(){end();}
 Status begin(const VoiceWorkerOutput&);void end();void step();
 VoiceAudioHostHooks hooks();
 // A pending request alone leaves media running; the flag is set only after
 // resources are ready for takeover or while an output remains voice-owned.
 bool preempting()const{return preempting_.load(std::memory_order_acquire);}
 uint8_t speakerGain()const{return uint8_t(gains_.load(std::memory_order_acquire)>>8);}
 Status setInput(owned::VoicePcmProvider provider,VoiceInputToken& token){return enabled_.load()?inputs_.set(provider,token):Status::Unsupported;}
 Status detachInput(VoiceInputToken token){return enabled_.load()?inputs_.detach(token):Status::Unsupported;}
 Status snapshot(VoiceWorkerSnapshot&);
private:
 struct Slot {uint8_t bytes[64]{};uint8_t length=0,status=0;};
 struct Mail {uint32_t wanted=0,ended=0,prepared=0,serial=0;owned::VoiceCodec codec=owned::VoiceCodec::Cvsd;Status result=Status::Busy;unsigned demand=0;Slot rx[4];uint8_t rx_read=0,rx_count=0;uint8_t tx[60]{};uint8_t tx_size=0,tx_cursor=0;VoiceWorkerSnapshot snapshot;};
 class Lease {public:explicit Lease(VoiceAudioWorker& w):w_(w),held_(!w.flag_.test_and_set(std::memory_order_acquire)){}~Lease(){if(held_)w_.flag_.clear(std::memory_order_release);}explicit operator bool()const{return held_;}private:VoiceAudioWorker& w_;bool held_;};
 static Status prepare(uint32_t,owned::VoiceCodec,void*);static void ended(uint32_t,void*);static void demand(uint32_t,unsigned,void*);
 static bool copyTx(uint32_t,uint8_t*,size_t,size_t&,uint32_t&,void*);static bool consumeTx(uint32_t,uint32_t,size_t,void*);static bool receive(uint32_t,uint8_t,const uint8_t*,size_t,void*);
 static Status health(uint32_t,void*);
 static void gains(uint8_t,uint8_t,void*);
 void stop(bool restore);Status start(uint32_t,owned::VoiceCodec);Status takeOutput(uint32_t,owned::VoiceCodec);void publish(Status);
 VoiceInputRegistry inputs_;VoiceWorkerOutput output_{};VoiceAudioPipeline pipeline_;Mail mail_{};
 std::atomic_flag flag_=ATOMIC_FLAG_INIT;std::atomic<bool> enabled_{false},preempting_{false};
 std::atomic<uint32_t> cancelled_{0},requested_generation_{0};std::atomic<unsigned> pending_demand_{0};
 std::atomic<uint16_t> gains_{0x0f0f};
 void* arena_=nullptr;uint32_t generation_=0,input_epoch_=0,pending_failure_generation_=0;Status pending_failure_status_=Status::Ok;bool output_taken_=false,output_ready_=false;
 uint8_t pending_tx_[60]{};size_t pending_tx_bytes_=0;Status worker_status_=Status::Ok;
};
}}
