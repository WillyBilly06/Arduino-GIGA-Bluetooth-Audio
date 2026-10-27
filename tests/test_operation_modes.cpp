#include "GigaBredr.h"
#if defined(__has_include)
# if __has_include("detail/OperationModePolicy.h")
#  include "detail/OperationModePolicy.h"
#  define HAVE_OPERATION_MODE_POLICY 1
# endif
#endif
using namespace giga_bredr;
// Against the old public API this fallback reports the real missing selection;
// it does not pretend that enable_i2s=false disables Classic admission.
template<class C> constexpr auto selectA2dp(C& c,bool enabled,int)
 ->decltype(c.enable_a2dp=enabled,bool()) { c.enable_a2dp=enabled;return true; }
template<class C> constexpr bool selectA2dp(C&,bool,long) { return false; }
template<class C> constexpr auto selectedA2dp(const C& c,int)
 ->decltype(bool(c.enable_a2dp)) { return c.enable_a2dp; }
template<class C> constexpr bool selectedA2dp(const C&,long) { return true; }
constexpr bool defaultCompatibility() {
 ReceiverConfig config;
 return selectedA2dp(config,0)&&config.ble.enabled&&config.enable_i2s;
}
constexpr bool bleOnlySelection() {
 ReceiverConfig config;config.ble.enabled=true;
 return selectA2dp(config,false,0)&&!selectedA2dp(config,0)&&config.ble.enabled;
}
constexpr bool a2dpOnlySelection() {
 ReceiverConfig config;config.ble.enabled=false;
 return selectA2dp(config,true,0)&&selectedA2dp(config,0)&&!config.ble.enabled;
}
static_assert(defaultCompatibility(),"Default must remain A2DP+BLE with I2S");
static_assert(bleOnlySelection(),"BLE-only must disable Classic independently of I2S");
static_assert(a2dpOnlySelection(),"A2DP-only selection must remain explicit and BLE off");
#ifdef HAVE_OPERATION_MODE_POLICY
using giga_bredr::detail::OperationModePolicy;
constexpr OperationModePolicy dual{true,true},audio{true,false},ble{false,true},off{false,false};
static_assert(dual.valid()&&audio.valid()&&ble.valid()&&!off.valid(),"At least one profile is required");
static_assert(dual.needsDecoder()&&audio.needsDecoder()&&!ble.needsDecoder(),"BLE-only must create no audio worker");
static_assert(ble.classicScan(true,true)==0&&ble.classicScan(false,true)==0&&ble.classicScan(true,false)==0,"BLE-only must never enable inquiry/page scan even with stale reservation");
static_assert(dual.classicScan(true,true)==3&&audio.classicScan(true,true)==3,"Default disconnected admitted Classic scan must retain inquiry+page");
static_assert(dual.classicScan(false,true)==1&&dual.classicScan(true,false)==1,"Existing Classic inquiry-only behavior must remain when page admission unavailable");
static_assert(!ble.admitClassic(true)&&!ble.admitClassic(false)&&audio.admitClassic(true)&&!audio.admitClassic(false),"Classic request admission must follow mode and real reservation");
static_assert(ble.classicUuidCount(true)==0&&ble.classicUuidCount(false)==0&&audio.classicUuidCount(true)==3&&audio.classicUuidCount(false)==1,"Disabled Classic must publish no A2DP/AVRCP UUIDs; default UUID set remains");
#endif
