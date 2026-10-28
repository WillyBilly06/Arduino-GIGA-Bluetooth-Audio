#pragma once
#include <stdint.h>
namespace giga_bredr { namespace detail {
// Caller serializes reservations with the exclusive receiver-owner admission.
// Zero is invalid; exhaustion is permanent for this process/boot, never wrap.
inline bool reserveVoiceInputLifetime(uint64_t& counter,uint64_t& value){
 if(counter==UINT64_MAX){value=0;return false;}
 value=++counter;return true;
}
}}
