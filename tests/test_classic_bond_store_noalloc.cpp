#include "detail/ClassicBondStore.h"
#include <cstdlib>
#include <cstdio>
#include <new>
static bool watching=false;static unsigned allocations=0;
void* operator new(size_t n){if(watching)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
using namespace giga_bredr;using namespace giga_bredr::detail;
struct App {ClassicBondRecord record;bool have=false;
 static BondStorageResult load(void* p,const ClassicBondIdentity&,ClassicBondRecord& r){auto& a=*static_cast<App*>(p);if(!a.have)return BondStorageResult::NotFound;r=a.record;return BondStorageResult::Ok;}
 static BondStorageResult save(void* p,const ClassicBondRecord& r){auto& a=*static_cast<App*>(p);a.record=r;a.have=true;return BondStorageResult::Ok;}
 static BondStorageResult remove(void* p,const ClassicBondIdentity&){static_cast<App*>(p)->have=false;return BondStorageResult::Ok;}
};
int main(){watching=true;auto* primer=new int(3);volatile int observed=*primer;(void)observed;delete primer;watching=false;if(allocations!=1)return 1;allocations=0;
 App app;ClassicBondStore store;uint8_t local[6]={9,8,7,6,5,4},peer[6]={1,2,3,4,5,6};bth_bond_t key{};key.key_type=4;for(unsigned i=0;i<16;++i)key.key[i]=uint8_t(i+1);bth_bond_t read{};
 watching=true;for(unsigned i=0;i<1000;++i){const uint8_t types[5]={0,4,5,7,8};key.key_type=types[i%5];key.pin_len=key.key_type==0?4:0;if(!store.begin({&app,App::load,App::save,App::remove},local)||!store.save(peer,key)||!store.load(peer,read)||memcmp(read.key,key.key,16)||read.key_type!=key.key_type||read.pin_len!=key.pin_len||!store.remove(peer))return 2;store.end();}watching=false;
 uint8_t secret[32];memset(secret,0x5a,sizeof secret);eraseBondBytes(secret,sizeof secret);for(auto byte:secret)if(byte)return 3;if(allocations)return 4;
 std::printf("BOND_STORE_NO_NEW_PASS operations=3000 lifetimes=1000 allocations=%u secure_erase_helper=1 no_durability_claim=1\n",allocations);return 0;}
