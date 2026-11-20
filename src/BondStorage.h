#pragma once
#include <stdint.h>
namespace giga_bredr {
// Classic BR/EDR link-key records only. Addresses are the six bytes in HCI
// order (least-significant address byte first), not printed MAC order.
struct ClassicBondIdentity {uint8_t local_address[6]{},peer_address[6]{};};
enum class ClassicLinkKeyType:uint8_t {
 LegacyCombination=0,UnauthenticatedP192=4,AuthenticatedP192=5,
 UnauthenticatedP256=7,AuthenticatedP256=8
};
struct ClassicBondRecord {
 uint32_t format=1;
 ClassicBondIdentity identity;
 uint8_t link_key[16]{};
 ClassicLinkKeyType key_type=ClassicLinkKeyType::LegacyCombination;
 uint8_t pin_length=0; // LegacyCombination:1..16; SSP types:0.
};
enum class BondStorageResult:uint8_t {Ok,NotFound,Error};
struct ClassicBondStorage {
 // Borrowed context and callbacks must remain alive until end() returns to a
 // legitimate lifecycle caller outside a callback. Portable poll()/end() calls
 // retain their serialized-caller contract; target end() joins its owner.
 // Callbacks execute synchronously on the
 // serialized host actor, never in an IRQ. They must be bounded/nonblocking,
 // must not throw, re-enter any receiver API, delete the receiver/context,
 // retain record references, or call flash erase/program on the audio path.
 void* context=nullptr;
 BondStorageResult (*load)(void*,const ClassicBondIdentity&,ClassicBondRecord&)=nullptr;
 BondStorageResult (*save)(void*,const ClassicBondRecord&)=nullptr;
 BondStorageResult (*remove)(void*,const ClassicBondIdentity&)=nullptr;
 bool configured()const{return context||load||save||remove;}
 bool valid()const{return !configured()||(load&&save&&remove);}
 // load:Ok requires exact format/identity plus valid key metadata; NotFound is
 // an ordinary cache miss. save:only Ok succeeds; remove:Ok/NotFound succeed.
 // Error/unknown enum/re-entry fails closed; no RAM fallback or callback retry.
 // Ok acknowledges a complete provider-owned copy. Durability is application
 // defined: this interface chooses no storage, partition or automatic writes.
 // Serialize fields explicitly; C++ layout/padding is not a stable NV format.
};
}
