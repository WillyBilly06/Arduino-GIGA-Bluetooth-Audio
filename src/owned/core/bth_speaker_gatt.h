#ifndef SPEAKER_BTH_SPEAKER_GATT_H
#define SPEAKER_BTH_SPEAKER_GATT_H
#include "bth_att.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BTH_SPEAKER_GATT_COUNT=18,BTH_SPEAKER_SERVICE=10,BTH_SPEAKER_CMD=12,
    BTH_SPEAKER_STATUS=14,BTH_SPEAKER_STATUS_CCCD=15,BTH_SPEAKER_METER=17,BTH_SPEAKER_METER_CCCD=18 };
// Install only when the real command/status/meter backend is bound. Name and
// caller-owned attributes must outlive the ATT bearer. Permissions match the
// existing public app service; this alone does not implement SMP security.
bool bth_speaker_gatt_init(bth_att_t *att,bth_att_attribute_t attributes[BTH_SPEAKER_GATT_COUNT],bth_span_t name);
#ifdef __cplusplus
}
#endif
#endif
