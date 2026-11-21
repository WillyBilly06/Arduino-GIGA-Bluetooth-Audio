#pragma once
#include <stdint.h>
// Write_Scan_Enable value while a Classic peer is connected (1: inquiry scan
// only, the historical default; 0: none, frees radio time for the A2DP link).
#ifndef GIGA_BREDR_CONNECTED_SCAN
#define GIGA_BREDR_CONNECTED_SCAN 1
#endif
namespace giga_bredr { namespace detail {
// Profile intent only. Controller capability, live storage reservations and
// backend readiness remain separately checked by their existing owners.
struct OperationModePolicy {
 bool a2dp=true,ble=true;
 constexpr bool valid()const{return a2dp||ble;}
 constexpr bool needsDecoder()const{return a2dp;}
 constexpr bool admitClassic(bool reserved)const{return a2dp&&reserved;}
 constexpr uint8_t classicScan(bool reserved,bool empty)const{return a2dp?uint8_t(reserved&&empty?3:(empty?1:GIGA_BREDR_CONNECTED_SCAN)):0;}
 constexpr uint8_t classicUuidCount(bool avrcp)const{return a2dp?uint8_t(avrcp?3:1):0;}
};
}}
