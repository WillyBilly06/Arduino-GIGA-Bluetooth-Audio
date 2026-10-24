#pragma once
#include <stdint.h>
namespace giga_bredr { namespace detail {
// Single serialized host owner. Metadata only; apply solely to optional
// application notifications, never ATT replies, security or control work.
class InitialBleNotificationGate {
 uint32_t owner_=0,attached_ms_=0;bool released_=false;
public:
 static constexpr uint32_t MaximumHoldMs=10000u;
 // Authoritative attach starts a fresh gate, including a repeated owner value.
 // Owners identify physical link generations and must not be reused by callers.
 void attach(uint32_t owner,uint32_t now_ms){owner_=owner;attached_ms_=now_ms;released_=false;}
 void detach(uint32_t expected_owner){
  if(expected_owner&&expected_owner==owner_){owner_=0;attached_ms_=0;released_=false;}
 }
 bool allow(uint32_t owner,uint32_t now_ms,bool audio_preference_active,bool actual_matches){
  if(!owner||owner!=owner_)return false;
  // Unsigned subtraction permits a short clock wrap. A release is sticky even
  // through subsequent wraps, until authoritative attach/matching detach.
  if(!released_&&(actual_matches||uint32_t(now_ms-attached_ms_)>=MaximumHoldMs))released_=true;
  // Inactive audio alone does not retire the gate: it can start during hold.
  return released_||!audio_preference_active;
 }
};
}}
