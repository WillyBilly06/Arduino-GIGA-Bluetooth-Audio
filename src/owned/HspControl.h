#ifndef GIGA_BREDR_HSP_CONTROL_H
#define GIGA_BREDR_HSP_CONTROL_H
#include "core/bth_packet.h"
#include "HfpAt.h"
namespace giga_bredr { namespace owned {
struct HspStatus { uint32_t generation=0,rings=0,completedCommands=0;uint8_t speakerGain=0,microphoneGain=0;bool connected=false,failed=false,lastCommandOk=false; };
// HS control: button is AG/state dependent, never silently mapped to answer.
class HspControl {
public:
 bool begin(uint32_t generation,uint8_t speakerGain,uint8_t microphoneGain);
 void end();bool ready()const{return status_.connected&&!status_.failed;}
 bth_status_t button(uint32_t generation);
 bth_status_t setGain(uint32_t generation,bool speaker,uint8_t value);
 bth_status_t next(uint32_t generation,uint32_t now_ms,char*,size_t,size_t&);
 bth_status_t receive(uint32_t generation,const uint8_t*,size_t,uint32_t now_ms);
 bth_status_t expire(uint32_t generation,uint32_t now_ms);
 const HspStatus& status()const{return status_;}
private:
 bool live(uint32_t g)const{return g&&g==status_.generation&&ready();}
 bth_status_t queue(uint32_t,const char*);void finish(bool);
 HspStatus status_{};HfpAtLine lines_{};char pending_[16]{};uint32_t deadline_=0;bool sent_=false;
};
}}
#endif
