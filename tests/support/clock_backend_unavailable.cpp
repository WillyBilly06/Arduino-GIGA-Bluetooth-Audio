// Explicitly absent clock capability for legacy frontend boundary mocks only.
// Actual clock/frontend/register tests must not link this translation unit.
#include "platform/GigaI2s.h"
namespace giga_bredr { namespace detail {
OutputSourceSnapshot i2sSourceSnapshot(){return {};}
ClockTrimResult i2sClockTrim(uint32_t,int32_t,int32_t,int32_t){ClockTrimResult r;r.status=ClockTrimCode::Unsupported;return r;}
I2sClockTrimSnapshot i2sClockTrimStats(){return {};}
}}
