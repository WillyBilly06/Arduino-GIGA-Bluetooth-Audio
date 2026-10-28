// Explicit absent voice capability for legacy frontend radio mocks only.
// Real actor/voice tests supply their real or declared fake radio boundary.
#include "platform/GigaRadio.h"
namespace giga_bredr { namespace detail {
Status radioVoiceStatus(VoiceStatus&){return Status::Unsupported;}
Status radioVoiceCommand(HfpCommand,uint32_t,const char*){return Status::Unsupported;}
}}
