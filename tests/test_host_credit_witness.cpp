// Actual pure-header ARM compile assertions, no native execution.
#include "platform/HostCreditWitness.h"
using namespace giga_bredr::detail::audio_supply;

static_assert(sizeof(HostCreditCell)==16 && alignof(HostCreditCell)==4,"exact state budget");
static_assert(hostInvalidateVersion(2)==3,"invalidate an even publication");
static_assert(hostInvalidateVersion(3)==3,"repeated Main invalidation stays odd");
static_assert(hostInvalidateVersion(UINT32_MAX)==UINT32_MAX,"poison never wraps");
static_assert(hostCloseVersion(3)==4,"Host alone closes an odd version");
static_assert(hostCloseVersion(UINT32_MAX)==UINT32_MAX,"poison cannot close");
static_assert(hostCloseVersion(2)==UINT32_MAX,"closing an even version is invalid");
static_assert(hostStartVersion(7,8,UINT32_MAX)==1,"new epoch can reset exhausted version");
static_assert(hostStartVersion(8,8,UINT32_MAX)==UINT32_MAX,"same epoch cannot reset poison");
static_assert(hostCreditPublicationFlags(8,8,2,2,0,HostStage1Unavailable)==0,"coherent empty view");
static_assert(hostCreditPublicationFlags(8,7,2,2,0,HostStage1Unavailable)&HostEpochInvalid,"old generation");
static_assert(hostCreditPublicationFlags(8,8,3,3,0,HostStage1Unavailable)&HostVersionInvalid,"interrupted mutation");
static_assert(hostCreditPublicationFlags(8,8,2,4,0,HostStage1Unavailable)&HostVersionInvalid,"changed during IRQ copy");
static_assert(hostCreditPublicationFlags(8,8,UINT32_MAX,UINT32_MAX,0,HostStage1Unavailable)&HostVersionInvalid,"no wrap alias");
static_assert((HostStage1Unavailable&0x3fff0000u)==0x3f3f0000u,"transport explicitly unavailable");
static_assert(!(HostStage1Unavailable&0x80000000u),"history disabled zero has no absence meaning");
static_assert((HostUnknown&0x007fffffu)==0,"Host unknown must not alter original validity");

constexpr bool packing() {
 HostCreditScalars s;
 s.event=0x3e;s.subevent=1;s.deferred=8;s.classic_held=4;s.rx_outstanding=4;s.pending_hnc=4;
 s.command_queued=8;s.command_sent=8;s.raw_barriered=4;s.classic_outstanding=4;s.classic_pending=3;
 s.command_credits=14;s.boundary=HostCreditBoundary::BlockedHead;
 const uint32_t h=hostCreditWord(s),t=hostCreditTransportWord(s);
 return (h&63u)==62 && ((h>>6)&15u)==1 && ((h>>10)&15u)==8 && ((h>>14)&7u)==4 &&
  ((h>>17)&7u)==4 && ((h>>20)&7u)==4 && ((h>>23)&15u)==8 && ((h>>27)&15u)==8 &&
  !(h&0x80000000u) && (t&7u)==4 && ((t>>3)&7u)==4 && ((t>>6)&7u)==3 &&
  ((t>>9)&15u)==14 && ((t>>13)&7u)==1 && (t&0x3fff0000u)==HostStage1Unavailable;
}
static_assert(packing(),"all shared-window counts and command fields retain their domain");
constexpr bool clipping() {
 HostCreditScalars s;s.event=255;s.subevent=255;s.deferred=9;s.classic_held=5;
 s.rx_outstanding=5;s.pending_hnc=5;s.command_queued=9;s.command_sent=9;
 s.raw_barriered=5;s.classic_outstanding=5;s.classic_pending=5;s.command_credits=255;
 const auto h=hostCreditWord(s),t=hostCreditTransportWord(s);
 return (h&63u)==63 && ((h>>6)&15u)==15 && ((h>>10)&15u)==15 && ((h>>14)&7u)==7 &&
  ((h>>17)&7u)==7 && ((h>>20)&7u)==7 && ((h>>23)&15u)==15 && ((h>>27)&15u)==15 &&
  (t&7u)==7 && ((t>>3)&7u)==7 && ((t>>6)&7u)==7 && ((t>>9)&15u)==15 &&
  (hostCreditPublicationFlags(8,8,2,2,h,t)&HostCountsUnknown) && (t&0x40000000u);
}
static_assert(clipping(),"invalid counts never wrap to zero; identity clipping explicit");
constexpr bool credits() {
 HostCreditScalars s;s.command_credits=0;
 if((hostCreditTransportWord(s)>>9&15u)!=0)return false;
 s.command_credits=15;if((hostCreditTransportWord(s)>>9&15u)!=15)return false;
 s.command_credits=255;return (hostCreditTransportWord(s)>>9&15u)==15;
}
static_assert(credits(),"zero credits exact; >=15 saturates to documented sentinel");
static_assert(hostCreditPublicationFlags(8,8,2,2,0x80000000u,HostStage1Unavailable)&HostIncomplete,"in-flight callback cannot falsify retention");
static_assert(hostLeaseMatches(3,3,true,10,10,0x12,0x12,6,6),"exact live lease identity");
static_assert(!hostLeaseMatches(3,3,true,9,10,0x12,0x12,6,6),"reused handle with old owner rejected");
static_assert(!hostLeaseMatches(3,3,true,10,10,0x12,0x12,5,6),"stale lease serial rejected");
static_assert(!hostLeaseMatches(3,3,false,10,10,0x12,0x12,6,6),"released slot rejected");
static_assert(!hostLeaseMatches(3,2,true,10,10,0x12,0x12,6,6),"wrong physical slot rejected");
static_assert(!hostLeaseMatches(3,3,true,0,0,0x12,0x12,6,6),"zero owner rejected");
