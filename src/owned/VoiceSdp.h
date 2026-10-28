#pragma once
#include "VoiceProfiles.h"
extern "C" {
#include "core/bth_sdp.h"
}
namespace giga_bredr { namespace detail {
// Immutable selected profile records. Outer actor registers these only after
// controller route/flow readback and a real worker backend have been admitted.
class VoiceSdp {
public:
 bool begin(const VoiceControlConfig&,bool wideband);
 const bth_sdp_record_t* records()const{return records_;}size_t count()const{return count_;}
private:uint8_t bytes_[2][112]{};bth_sdp_record_t records_[2]{};size_t count_=0;
};
}}
