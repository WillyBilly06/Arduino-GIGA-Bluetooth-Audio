#include "GigaBredr.h"
#include "BondStorage.h"
#include <type_traits>
using namespace giga_bredr;
static BondStorageResult load(void*,const ClassicBondIdentity&,ClassicBondRecord&){return BondStorageResult::NotFound;}
static BondStorageResult save(void*,const ClassicBondRecord&){return BondStorageResult::Ok;}
static BondStorageResult remove(void*,const ClassicBondIdentity&){return BondStorageResult::NotFound;}
static_assert(std::is_trivially_copyable<ClassicBondRecord>::value,"Typed bounded copy");
static_assert(sizeof(ClassicBondRecord)<=40,"Fixed bounded public record");
int main(){ReceiverConfig config;if(config.classic_bonds.configured()||!config.classic_bonds.valid())return 1;config.classic_bonds={nullptr,load,save,remove};return config.classic_bonds.configured()&&config.classic_bonds.valid()?0:2;}
