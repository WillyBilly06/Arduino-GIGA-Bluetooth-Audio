#pragma once
#include <stdint.h>

// Pure scalar contracts. No state, SDK, clock or hardware access here.
namespace giga_bredr { namespace detail { namespace audio_supply {
struct alignas(4) HostCreditCell { uint32_t version, generation, host, transport; };
enum class HostCreditBoundary : uint32_t {
 Idle=0, BlockedHead=1, Receive=2, Callback=3, TxNotReady=4,
 HncCommit=5, Transport=6, Unknown=7
};
struct HostCreditScalars {
 uint32_t event=0, subevent=0, deferred=0, classic_held=0;
 uint32_t rx_outstanding=0, pending_hnc=0, command_queued=0, command_sent=0;
 uint32_t raw_barriered=0, classic_outstanding=0, classic_pending=0, command_credits=0;
 HostCreditBoundary boundary=HostCreditBoundary::Idle;
 bool incomplete=false;
};
constexpr uint32_t HostStage1Unavailable=0x3f3f0000u;
// Separate validity domain from the original Supply flags0..22 and point30..31.
enum HostCreditFlag : uint32_t {
 HostUnknown=1u<<23, HostEpochInvalid=1u<<24, HostVersionInvalid=1u<<25,
 HostIncomplete=1u<<26, HostCountsUnknown=1u<<27, HostIdentityClipped=1u<<28,
 HostViewUnavailable=1u<<29
};
constexpr uint32_t hostInvalidateVersion(uint32_t v) {
 return (v&1u)?v:v+1u;
}
constexpr uint32_t hostCloseVersion(uint32_t v) {
 return !(v&1u)||v==UINT32_MAX?UINT32_MAX:v+1u;
}
constexpr uint32_t hostStartVersion(uint32_t old_generation,uint32_t generation,uint32_t version) {
 return !generation||generation==UINT32_MAX?UINT32_MAX:
  old_generation!=generation?1u:hostInvalidateVersion(version);
}
constexpr uint32_t hostCount(uint32_t value,uint32_t maximum,uint32_t unknown) {
 return value<=maximum?value:unknown;
}
constexpr uint32_t hostCreditWord(const HostCreditScalars& s) {
 return hostCount(s.event,62,63) | (hostCount(s.subevent,14,15)<<6) |
  (hostCount(s.deferred,8,15)<<10) | (hostCount(s.classic_held,4,7)<<14) |
  (hostCount(s.rx_outstanding,4,7)<<17) | (hostCount(s.pending_hnc,4,7)<<20) |
  (hostCount(s.command_queued,8,15)<<23) | (hostCount(s.command_sent,8,15)<<27) |
  (s.incomplete?0x80000000u:0u);
}
constexpr uint32_t hostCreditTransportWord(const HostCreditScalars& s) {
 return hostCount(s.raw_barriered,4,7) | (hostCount(s.classic_outstanding,4,7)<<3) |
  (hostCount(s.classic_pending,4,7)<<6) | ((s.command_credits<15?s.command_credits:15u)<<9) |
  (hostCount(uint32_t(s.boundary),7,7)<<13) | HostStage1Unavailable |
  ((s.event>62||s.subevent>14)?0x40000000u:0u);
 // Bit31 remains reserved zero: per-generation history is UNQUALIFIED.
}
constexpr uint32_t hostCreditPublicationFlags(uint32_t expected,uint32_t generation,
 uint32_t before,uint32_t after,uint32_t host,uint32_t transport) {
 uint32_t flags=0;
 if(!expected||expected==UINT32_MAX||generation!=expected)flags|=HostEpochInvalid;
 if((before|after)&1u||before!=after||before==UINT32_MAX||after==UINT32_MAX)
  flags|=HostVersionInvalid;
 if(host&0x80000000u)flags|=HostIncomplete;
 if(((host>>10)&15u)>8||((host>>14)&7u)>4||((host>>17)&7u)>4||
    ((host>>20)&7u)>4||((host>>23)&15u)>8||((host>>27)&15u)>8||
    (transport&7u)>4||((transport>>3)&7u)>4||((transport>>6)&7u)>4)
  flags|=HostCountsUnknown;
 if(transport&0x40000000u)flags|=HostIdentityClipped;
 if((transport&0x3fff0000u)!=HostStage1Unavailable||(transport&0x80000000u))
  flags|=HostViewUnavailable;
 return flags?flags|HostUnknown:0u;
}
constexpr bool hostLeaseMatches(uint32_t packet_slot,uint32_t lease_slot,bool busy,
 uint32_t slot_owner,uint32_t current_owner,uint32_t slot_handle,uint32_t current_handle,
 uint32_t slot_serial,uint32_t lease_serial) {
 return packet_slot<8&&packet_slot==lease_slot&&busy&&current_owner&&slot_owner==current_owner&&
  current_handle<=0xeff&&slot_handle==current_handle&&lease_serial&&slot_serial==lease_serial;
}
}}} // namespace giga_bredr::detail::audio_supply
