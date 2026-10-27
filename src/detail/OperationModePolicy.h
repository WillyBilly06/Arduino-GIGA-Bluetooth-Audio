#pragma once
#include <stdint.h>
namespace giga_bredr { namespace detail {
// Profile intent only. Controller capability, live storage reservations and
// backend readiness remain separately checked by their existing owners.
struct OperationModePolicy {
 bool a2dp=true,ble=true;
 constexpr bool valid()const{return a2dp||ble;}
 constexpr bool needsDecoder()const{return a2dp;}
 constexpr bool admitClassic(bool reserved)const{return a2dp&&reserved;}
 constexpr uint8_t classicScan(bool reserved,bool empty)const{return a2dp?uint8_t(reserved&&empty?3:1):0;}
 constexpr uint8_t classicUuidCount(bool avrcp)const{return a2dp?uint8_t(avrcp?3:1):0;}
};
}}
