#pragma once
#include "../Types.h"
#include "../GigaVoice.h"
#include "HfpAudio.h"
#include <atomic>
namespace giga_bredr { namespace detail {
using VoiceInputToken=::giga_bredr::VoiceInputToken;
struct VoiceInputSnapshot {uint32_t epoch=1;owned::MicrophoneMode mode=owned::MicrophoneMode::None;owned::VoicePcmProvider provider{};};
class VoiceInputRegistry;
class VoiceInputLease {
public:
 VoiceInputLease()=default;~VoiceInputLease();
 VoiceInputLease(const VoiceInputLease&)=delete;VoiceInputLease& operator=(const VoiceInputLease&)=delete;
 const VoiceInputSnapshot& snapshot()const{return value_;}
private:
 friend class VoiceInputRegistry;VoiceInputRegistry* owner_=nullptr;VoiceInputSnapshot value_{};
};
// Nonblocking registration boundary. The worker must hold a lease while
// synchronizing its applied input epoch AND invoking available/read. A setter
// never operates on the worker's pipeline directly. Successful replacement or
// detach fences old callbacks; already captured/transmitted audio is not recalled.
class VoiceInputRegistry {
public:
 explicit VoiceInputRegistry(uint64_t lifetime):lifetime_(lifetime){}
 VoiceInputRegistry(const VoiceInputRegistry&)=delete;VoiceInputRegistry& operator=(const VoiceInputRegistry&)=delete;
 Status set(owned::VoicePcmProvider provider,VoiceInputToken& token){
  if(!lifetime_||!provider.available||!provider.read)return Status::InvalidArgument;
  if(flag_.test_and_set(std::memory_order_acquire))return Status::Busy;
  if(value_.epoch==UINT32_MAX){unlock();return Status::Unsupported;}
  ++value_.epoch;value_.mode=owned::MicrophoneMode::Provider;value_.provider=provider;retired_={};token={lifetime_,value_.epoch};unlock();return Status::Ok;
 }
 Status detach(VoiceInputToken expected){
  if(!expected.lifetime||expected.lifetime!=lifetime_||!expected.epoch)return Status::InvalidArgument;
  if(flag_.test_and_set(std::memory_order_acquire))return Status::Busy;
  if(value_.mode==owned::MicrophoneMode::None&&equal(expected,retired_)){unlock();return Status::Ok;}
  if(value_.mode!=owned::MicrophoneMode::Provider||expected.epoch!=value_.epoch){unlock();return Status::InvalidArgument;}
  // Refuse exhaustion instead of wrapping an identity or promising a fence
  // the worker cannot apply. end()/begin() establishes a fresh lifetime.
  if(value_.epoch==UINT32_MAX){unlock();return Status::Unsupported;}
  ++value_.epoch;value_.mode=owned::MicrophoneMode::None;value_.provider={};retired_=expected;unlock();return Status::Ok;
 }
 bool tryAcquire(VoiceInputLease& lease){
  if(lease.owner_||flag_.test_and_set(std::memory_order_acquire))return false;lease.owner_=this;lease.value_=value_;return true;
 }
private:
 friend class VoiceInputLease;
 static bool equal(VoiceInputToken a,VoiceInputToken b){return a.lifetime==b.lifetime&&a.epoch==b.epoch;}
 void unlock(){flag_.clear(std::memory_order_release);}
 uint64_t lifetime_;VoiceInputSnapshot value_{};VoiceInputToken retired_{};std::atomic_flag flag_=ATOMIC_FLAG_INIT;
};
inline VoiceInputLease::~VoiceInputLease(){if(owner_)owner_->unlock();}
}}
