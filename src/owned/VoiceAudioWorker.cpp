#include "VoiceAudioWorker.h"
#include "../detail/Memory.h"
#include <cstring>
namespace giga_bredr { namespace detail {
Status VoiceAudioWorker::begin(const VoiceWorkerOutput& output){
 if(enabled_.load())return Status::Busy;if(!output.prepare||!output.restore||!output.wake||!output.sink.write)return Status::InvalidArgument;
 output_=output;mail_={};enabled_.store(true,std::memory_order_release);return Status::Ok;
}
void VoiceAudioWorker::end(){enabled_.store(false,std::memory_order_release);stop(true);pending_failure_generation_=0;pending_failure_status_=Status::Ok;mail_={};output_={};preempting_.store(false,std::memory_order_release);}
VoiceAudioHostHooks VoiceAudioWorker::hooks(){VoiceAudioHostHooks h;h.user=this;h.prepare=prepare;h.ended=ended;h.request_tx=demand;h.copy_tx=copyTx;h.consume_tx=consumeTx;h.receive=receive;h.health=health;h.gains=gains;return h;}
void VoiceAudioWorker::gains(uint8_t speaker,uint8_t microphone,void* p){if(speaker<=15&&microphone<=15)static_cast<VoiceAudioWorker*>(p)->gains_.store(uint16_t((speaker<<8)|microphone),std::memory_order_release);}
Status VoiceAudioWorker::prepare(uint32_t g,owned::VoiceCodec c,void* p){
 auto& w=*static_cast<VoiceAudioWorker*>(p);if(!w.enabled_.load()||!g)return Status::Unsupported;Status result=Status::Busy;
 {Lease lock(w);if(!lock)return Status::Busy;auto& m=w.mail_;if(m.ended==g)return Status::Unsupported;
  if(m.wanted!=g){m.wanted=g;m.codec=c;m.prepared=0;m.result=Status::Busy;m.demand=0;m.rx_read=m.rx_count=m.tx_size=m.tx_cursor=0;w.pending_demand_.store(0);w.requested_generation_.store(g,std::memory_order_release);}
  if(m.prepared==g)result=m.result;
 }w.output_.wake(w.output_.user);return result;
}
void VoiceAudioWorker::ended(uint32_t g,void* p){
 auto& w=*static_cast<VoiceAudioWorker*>(p);if(!w.enabled_.load())return;
 // The host retries poll every millisecond, but a physical disconnect must
 // never lose its cancellation. Atomic generation stores are a separate lane.
 // Host and worker exchange it through an atomic rather than a best-effort lock.
 w.cancelled_.store(g,std::memory_order_release);w.output_.wake(w.output_.user);
}
void VoiceAudioWorker::demand(uint32_t g,unsigned n,void* p){
 auto& w=*static_cast<VoiceAudioWorker*>(p);if(!w.enabled_.load()||w.requested_generation_.load(std::memory_order_acquire)!=g)return;w.pending_demand_.fetch_add(n,std::memory_order_release);w.output_.wake(w.output_.user);
}
bool VoiceAudioWorker::copyTx(uint32_t g,uint8_t* out,size_t cap,size_t& n,uint32_t& ticket,void* p){auto& w=*static_cast<VoiceAudioWorker*>(p);Lease lock(w);if(!lock)return false;const auto& m=w.mail_;if(g!=m.wanted||g!=m.prepared||!m.tx_size||cap<size_t(m.tx_size-m.tx_cursor))return false;n=m.tx_size-m.tx_cursor;std::memcpy(out,m.tx+m.tx_cursor,n);ticket=m.serial;return true;}
bool VoiceAudioWorker::consumeTx(uint32_t g,uint32_t ticket,size_t n,void* p){auto& w=*static_cast<VoiceAudioWorker*>(p);Lease lock(w);if(!lock)return false;auto& m=w.mail_;if(g!=m.wanted||ticket!=m.serial||!m.tx_size||n>size_t(m.tx_size-m.tx_cursor))return false;m.tx_cursor=uint8_t(m.tx_cursor+n);if(m.tx_cursor==m.tx_size)m.tx_size=m.tx_cursor=0;return true;}
bool VoiceAudioWorker::receive(uint32_t g,uint8_t status,const uint8_t* data,size_t n,void* p){auto& w=*static_cast<VoiceAudioWorker*>(p);Lease lock(w);if(!lock)return false;auto& m=w.mail_;if(g!=m.wanted||g!=m.prepared||m.result!=Status::Ok||m.rx_count==4||n>64||(n&&!data))return false;auto& slot=m.rx[(m.rx_read+m.rx_count)%4];if(n)std::memcpy(slot.bytes,data,n);slot.length=uint8_t(n);slot.status=status;++m.rx_count;w.output_.wake(w.output_.user);return true;}
Status VoiceAudioWorker::health(uint32_t g,void* p){auto& w=*static_cast<VoiceAudioWorker*>(p);Lease lock(w);if(!lock)return Status::Busy;return w.mail_.prepared==g?w.mail_.result:Status::Unsupported;}
Status VoiceAudioWorker::snapshot(VoiceWorkerSnapshot& result){Lease lock(*this);if(!lock)return Status::Busy;result=mail_.snapshot;return Status::Ok;}
void VoiceAudioWorker::stop(bool restore){pipeline_.end();release(arena_);arena_=nullptr;generation_=input_epoch_=0;output_ready_=false;pending_tx_bytes_=0;worker_status_=Status::Ok;if(restore&&output_taken_){output_.restore(output_.user);output_taken_=false;}}
Status VoiceAudioWorker::start(uint32_t g,owned::VoiceCodec codec){
 const bool wide=codec==owned::VoiceCodec::Msbc;const size_t bytes=wide?604:128;arena_=allocate(bytes,MemoryKind::Fast);if(!arena_)return Status::OutOfMemory;
 auto* memory=static_cast<uint8_t*>(arena_);VoicePipelineBuffers b;VoicePipelineConfig c;c.format={codec,wide?16000u:8000u,uint16_t(wide?120:30),1};c.sink=output_.sink;
 b.rx_pcm=reinterpret_cast<int16_t*>(memory);b.rx_frames=wide?120:32;b.packet=memory+(wide?480:64);b.packet_bytes=64;if(wide){b.tx_pcm=reinterpret_cast<int16_t*>(memory+240);b.tx_frames=120;b.h2=memory+544;b.h2_bytes=60;}
 VoiceInputLease input;if(!inputs_.tryAcquire(input)){release(arena_);arena_=nullptr;return Status::Busy;}const auto snapshot=input.snapshot();c.input_mode=snapshot.mode;c.input=snapshot.provider;
 auto result=pipeline_.begin(g,snapshot.epoch,c,b);if(result!=Status::Ok){release(arena_);arena_=nullptr;return result;}generation_=g;input_epoch_=snapshot.epoch;
 return Status::Ok;
}
Status VoiceAudioWorker::takeOutput(uint32_t g,owned::VoiceCodec codec){
 // Keep the prepared pipeline across contention. Its input epoch has already
 // been admitted and cannot safely be configured again for this generation.
 bool admitted=false;{Lease lock(*this);if(lock&&mail_.wanted==g&&cancelled_.load(std::memory_order_acquire)!=g){preempting_.store(true,std::memory_order_release);admitted=true;}}
 if(!admitted)return Status::Busy;
 // Even a failed prepare may have retired the old DMA; restore physical output
 // while retaining the prepared pipeline when the failure is transient.
 output_taken_=true;const auto result=output_.prepare(codec,output_.user);
 if(result==Status::Busy){output_.restore(output_.user);output_taken_=false;return result;}
 if(result!=Status::Ok){stop(true);return result;}output_ready_=true;return Status::Ok;
}
void VoiceAudioWorker::publish(Status status){Lease lock(*this);if(!lock)return;auto& m=mail_;if(m.wanted!=generation_)return;m.snapshot.generation=generation_;m.snapshot.format=pipeline_.format();m.snapshot.pipeline=pipeline_.stats();m.snapshot.input=pipeline_.inputCounters();m.snapshot.status=status;}
void VoiceAudioWorker::step(){
 if(!enabled_.load())return;uint32_t wanted=0;owned::VoiceCodec codec=owned::VoiceCodec::Cvsd;bool terminal=false;
 // Only the worker changes output ownership. Recheck under the same mailbox
 // lease as prepare() so a pending newer request cannot be lost during restore.
 auto reopenIfIdle=[this](){Lease lock(*this);if(lock&&!output_ready_&&!output_taken_)preempting_.store(false,std::memory_order_release);};
 {Lease lock(*this);if(!lock)return;auto& m=mail_;const uint32_t cancelled=cancelled_.exchange(0,std::memory_order_acq_rel);if(cancelled){m.ended=cancelled;if(m.wanted==cancelled)m.wanted=0;}
  // A terminal outcome belongs to its original generation until the mailbox
  // accepts it, or that generation is cancelled or superseded.
  if(pending_failure_generation_){if(m.wanted==pending_failure_generation_){m.prepared=pending_failure_generation_;m.result=pending_failure_status_;}pending_failure_generation_=0;}
  if(!output_ready_&&!output_taken_)preempting_.store(false,std::memory_order_release);
  const unsigned extra=pending_demand_.exchange(0,std::memory_order_acq_rel);m.demand=extra>2-m.demand?2:m.demand+extra;
  wanted=m.wanted;codec=m.codec;terminal=m.prepared==wanted&&m.result!=Status::Ok&&m.result!=Status::Busy;
 }
 if(generation_&&generation_!=wanted)stop(wanted==0);
 if(!wanted){if(output_taken_)stop(true);reopenIfIdle();return;}
 if(terminal){if(generation_||output_taken_)stop(true);reopenIfIdle();return;}
 auto fail=[this,&reopenIfIdle](uint32_t g,Status result){
  pending_failure_generation_=g;pending_failure_status_=result;bool current=false;
  {Lease lock(*this);if(lock){if(mail_.wanted==g&&cancelled_.load(std::memory_order_acquire)!=g){mail_.prepared=g;mail_.result=result;current=true;}pending_failure_generation_=0;}}
  if(current&&output_taken_)stop(true);reopenIfIdle();
 };
 if(!generation_){const auto result=start(wanted,codec);if(result==Status::Busy){reopenIfIdle();return;}
  if(result!=Status::Ok){fail(wanted,result);return;}}
 if(!output_ready_){const auto result=takeOutput(wanted,codec);if(result==Status::Busy){reopenIfIdle();return;}
  if(result!=Status::Ok){fail(wanted,result);return;}}
 // A preparation acknowledgement can be delayed by mailbox contention.
 {Lease lock(*this);if(lock&&mail_.wanted==generation_&&!mail_.prepared){mail_.prepared=generation_;mail_.result=Status::Ok;}}
 for(unsigned budget=0;budget<4;++budget){const auto polled=pipeline_.poll(generation_);if(polled!=Status::Ok&&polled!=Status::Busy&&polled!=Status::NeedMoreData){worker_status_=polled;break;}
  Lease lock(*this);if(!lock||mail_.wanted!=generation_||!mail_.rx_count)break;const auto& rx=mail_.rx[mail_.rx_read];
  // receive() only performs a bounded copy; no decoder or sink callback here.
  const auto accepted=pipeline_.receive(generation_,rx.status,rx.bytes,rx.length);if(accepted==Status::OutputFull)continue;
  mail_.rx_read=uint8_t((mail_.rx_read+1)%4);--mail_.rx_count;if(accepted!=Status::Ok)worker_status_=accepted;
 }
 bool render=false;{Lease lock(*this);if(lock)render=!pending_tx_bytes_&&mail_.wanted==generation_&&mail_.demand&&!mail_.tx_size&&mail_.result==Status::Ok;}
 if(render){VoiceInputLease input;if(inputs_.tryAcquire(input)){const auto in=input.snapshot();Status result=Status::Ok;if(in.epoch!=input_epoch_){result=pipeline_.setInput(generation_,in.epoch,in.mode,in.provider);if(result==Status::Ok)input_epoch_=in.epoch;}
   pipeline_.microphoneGain(uint8_t(gains_.load(std::memory_order_acquire)));size_t n=0;if(result==Status::Ok)result=pipeline_.renderTx(generation_,pending_tx_,sizeof pending_tx_,n);
   if(result==Status::Ok&&n==60)pending_tx_bytes_=n;else worker_status_=result==Status::Ok?Status::HardwareError:result;
  }}
 {Lease lock(*this);if(lock&&mail_.wanted==generation_){if(pending_tx_bytes_&&!mail_.tx_size){if(mail_.serial==UINT32_MAX)worker_status_=Status::HardwareError;else{std::memcpy(mail_.tx,pending_tx_,pending_tx_bytes_);mail_.tx_size=uint8_t(pending_tx_bytes_);mail_.tx_cursor=0;++mail_.serial;if(mail_.demand)--mail_.demand;pending_tx_bytes_=0;}}mail_.result=worker_status_;}}
 publish(worker_status_);
}
}}
