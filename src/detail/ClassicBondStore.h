#pragma once
#include "../BondStorage.h"
extern "C" {
#include "../owned/core/bth_links.h"
}
#include <string.h>
#include <limits.h>
namespace giga_bredr {namespace detail {
inline void eraseBondBytes(void* value,size_t count){volatile uint8_t* p=static_cast<volatile uint8_t*>(value);while(count--)*p++=0;}
class ClassicBondStore {
public:
 bool begin(const ClassicBondStorage& provider,const uint8_t local[6]){
  if(rejectReentry()||!provider.valid())return false;
  end();failed_=false;if(!provider.configured())return true;
  if(lifetime_==UINT64_MAX)return false;
  provider_=provider;memcpy(local_,local,6);++lifetime_;active_=true;return true;
 }
 void end(){if(rejectReentry())return;active_=false;eraseBondBytes(&provider_,sizeof provider_);eraseBondBytes(local_,sizeof local_);}
 bool enabled()const{return active_;}
 bool failed()const{return failed_;}
 bool rejectReentry()const{if(!inside_)return false;failed_=true;return true;}
 bool load(const uint8_t peer[6],bth_bond_t& output){
  eraseBondBytes(&output,sizeof output);if(!ready())return false;
  ClassicBondRecord record;eraseBondBytes(&record,sizeof record);record.format=1;ClassicBondIdentity id=identity(peer);Wipe wipeRecord(&record,sizeof record),wipeId(&id,sizeof id);BondStorageResult result=BondStorageResult::Error;const uint64_t lifetime=lifetime_;
  {Call call(*this);
#if defined(__cpp_exceptions)
   try{result=provider_.load(provider_.context,id,record);}catch(...){failed_=true;}
#else
   result=provider_.load(provider_.context,id,record);
#endif
  }
  if(!returned(lifetime))return false;
  if(result==BondStorageResult::NotFound)return false;
  const auto expected=identity(peer);
  if(result!=BondStorageResult::Ok||memcmp(&id,&expected,sizeof id)||!validRecord(record,expected)){failed_=true;return false;}
  memcpy(output.key,record.link_key,16);output.key_type=uint8_t(record.key_type);output.pin_len=record.pin_length;return true;
 }
 bool save(const uint8_t peer[6],const bth_bond_t& value){
  if(!ready())return false;ClassicBondRecord record;eraseBondBytes(&record,sizeof record);record.format=1;record.identity=identity(peer);memcpy(record.link_key,value.key,16);record.key_type=ClassicLinkKeyType(value.key_type);record.pin_length=value.pin_len;Wipe wipe(&record,sizeof record);
  if(!validRecord(record,record.identity)){failed_=true;return false;}
  BondStorageResult result=BondStorageResult::Error;const uint64_t lifetime=lifetime_;
  {Call call(*this);
#if defined(__cpp_exceptions)
   try{result=provider_.save(provider_.context,record);}catch(...){failed_=true;}
#else
   result=provider_.save(provider_.context,record);
#endif
  }
  if(!returned(lifetime))return false;if(result!=BondStorageResult::Ok){failed_=true;return false;}return true;
 }
 bool remove(const uint8_t peer[6]){
  if(!ready())return false;ClassicBondIdentity id=identity(peer);Wipe wipe(&id,sizeof id);BondStorageResult result=BondStorageResult::Error;const uint64_t lifetime=lifetime_;
  {Call call(*this);
#if defined(__cpp_exceptions)
   try{result=provider_.remove(provider_.context,id);}catch(...){failed_=true;}
#else
   result=provider_.remove(provider_.context,id);
#endif
  }
  const auto expected=identity(peer);
  if(!returned(lifetime))return false;if(memcmp(&id,&expected,sizeof id)||(result!=BondStorageResult::Ok&&result!=BondStorageResult::NotFound)){failed_=true;return false;}return true;
 }
private:
 struct Wipe {void* p;size_t n;Wipe(void* p,size_t n):p(p),n(n){}~Wipe(){eraseBondBytes(p,n);}Wipe(const Wipe&)=delete;Wipe&operator=(const Wipe&)=delete;};
 struct Call {ClassicBondStore& store;explicit Call(ClassicBondStore& s):store(s){store.inside_=true;}~Call(){store.inside_=false;}};
 ClassicBondIdentity identity(const uint8_t peer[6])const{ClassicBondIdentity id;memcpy(id.local_address,local_,6);memcpy(id.peer_address,peer,6);return id;}
 bool ready(){if(rejectReentry())return false;return active_&&!failed_;}
 bool returned(uint64_t lifetime){if(!active_||lifetime_!=lifetime||failed_){failed_=true;return false;}return true;}
 static bool validRecord(const ClassicBondRecord& r,const ClassicBondIdentity& id){
  if(r.format!=1||memcmp(r.identity.local_address,id.local_address,6)||memcmp(r.identity.peer_address,id.peer_address,6))return false;
  const uint8_t t=uint8_t(r.key_type);return t==0?(r.pin_length>=1&&r.pin_length<=16):((t==4||t==5||t==7||t==8)&&r.pin_length==0);
 }
 ClassicBondStorage provider_;uint8_t local_[6]{};uint64_t lifetime_=0;bool active_=false,inside_=false;mutable bool failed_=false;
};
}}
