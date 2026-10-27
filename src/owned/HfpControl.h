#ifndef GIGA_BREDR_HFP_CONTROL_H
#define GIGA_BREDR_HFP_CONTROL_H
#include "core/bth_packet.h"
#include "HfpAt.h"
#include "HfpAudio.h"
namespace giga_bredr { namespace owned {
enum class HfpState:uint8_t { Closed,Features,Codecs,IndicatorNames,IndicatorValues,EnableEvents,SpeakerGain,MicrophoneGain,Ready,Failed };
enum class HfpIndicator:uint8_t { Service,Call,CallSetup,CallHeld,Signal,Roam,Battery };
enum class HfpCommand:uint8_t { Answer,Hangup,Dial,Redial,Dtmf };
struct HfpStatus {
 uint32_t generation=0,agFeatures=0,rings=0,completedCommands=0;
 HfpState state=HfpState::Closed;VoiceCodec codec=VoiceCodec::Cvsd;
 uint8_t indicatorCount=0,indicators[16]{},speakerGain=0,microphoneGain=0;
 bool slcEstablished=false,lastCommandOk=false,inBandRing=false;
};
// HF control only; caller admits/queues complete RFCOMM data and owns its security.
// No profile registration, HCI, clock, callback, implicit reconnect or call is started.
class HfpControl {
public:
 bool begin(uint32_t generation,bool verifiedWidebandBackend,uint8_t speakerGain,uint8_t microphoneGain);
 void end();
 bth_status_t next(uint32_t generation,uint32_t now_ms,char* out,size_t capacity,size_t& written);
 bth_status_t receive(uint32_t generation,const uint8_t*,size_t,uint32_t now_ms);
 bth_status_t command(uint32_t generation,HfpCommand,const char* argument=nullptr);
 bth_status_t expire(uint32_t generation,uint32_t now_ms);
 bool ready()const{return status_.state==HfpState::Ready;}
 const HfpStatus& status()const{return status_;}
 bool indicator(HfpIndicator kind,uint8_t& value)const;
private:
 enum class Transaction:uint8_t { None,Slc,User,Codec,CodecList };
 bth_status_t line(const char*);
 bool names(const char*);bool values(const char*);
 void finish(bool ok);bool live(uint32_t g)const{return g&&g==status_.generation&&status_.state!=HfpState::Closed&&status_.state!=HfpState::Failed;}
 HfpStatus status_{};HfpAtLine lines_{};
 uint8_t indicatorKinds_[16]{},indicatorAllowed_[16][32]{},codecRequest_=0,codecInFlight_=0;
 uint32_t deadline_=0;Transaction transaction_=Transaction::None;
 bool wideband_=false,seen_=false,codecListPending_=false;
 char user_[48]{};
};
}}
#endif
