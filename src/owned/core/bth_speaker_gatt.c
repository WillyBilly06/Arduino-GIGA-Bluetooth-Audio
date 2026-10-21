#include "bth_speaker_gatt.h"
#include <string.h>
#define SPAN(a) {(a),sizeof(a)}
#define UUID(n) (n),0x90,0x78,0x56,0x34,0x12,0x34,0x12,0x34,0x12,0x34,0x12,0x78,0x56,0x34,0x12
static const uint8_t primary[]={0,0x28},declaration[]={3,0x28},cccd[]={2,0x29};
static const uint8_t gap[]={0,0x18},gatt[]={1,0x18},app[]={UUID(0)},cmd[]={UUID(1)},status[]={UUID(2)},meter[]={UUID(3)};
static const uint8_t name_type[]={0,0x2a},appearance_type[]={1,0x2a},changed_type[]={5,0x2a};
static const uint8_t name_decl[]={2,3,0,0,0x2a},appearance_decl[]={2,5,0,1,0x2a},changed_decl[]={0x20,8,0,5,0x2a};
static const uint8_t cmd_decl[]={0x0c,12,0,UUID(1)},status_decl[]={0x12,14,0,UUID(2)},meter_decl[]={0x12,17,0,UUID(3)};
static const uint8_t appearance[]={0,0},changed[]={1,0,18,0};
static const bth_att_attribute_t model[]={
    {.handle=1,.group_end=5,.permissions=BTH_ATT_READ,.uuid=SPAN(primary),.value=SPAN(gap)},
    {.handle=2,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(name_decl)},
    {.handle=3,.maximum_length=63,.permissions=BTH_ATT_READ,.uuid=SPAN(name_type)},
    {.handle=4,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(appearance_decl)},
    {.handle=5,.permissions=BTH_ATT_READ,.uuid=SPAN(appearance_type),.value=SPAN(appearance)},
    {.handle=6,.group_end=9,.permissions=BTH_ATT_READ,.uuid=SPAN(primary),.value=SPAN(gatt)},
    {.handle=7,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(changed_decl)},
    {.handle=8,.uuid=SPAN(changed_type),.value=SPAN(changed)},
    {.handle=9,.related_handle=8,.maximum_length=2,.permissions=BTH_ATT_READ|BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND,
        .kind=BTH_ATT_CCCD,.cccd_index=0,.cccd_mask=2,.uuid=SPAN(cccd)},
    {.handle=10,.group_end=18,.permissions=BTH_ATT_READ,.uuid=SPAN(primary),.value=SPAN(app)},
    {.handle=11,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(cmd_decl)},
    {.handle=12,.maximum_length=512,.permissions=BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND,.kind=BTH_ATT_APPLICATION,.uuid=SPAN(cmd)},
    {.handle=13,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(status_decl)},
    {.handle=14,.maximum_length=512,.permissions=BTH_ATT_READ,.kind=BTH_ATT_APPLICATION,.uuid=SPAN(status)},
    {.handle=15,.related_handle=14,.maximum_length=2,.permissions=BTH_ATT_READ|BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND,
        .kind=BTH_ATT_CCCD,.cccd_index=1,.cccd_mask=1,.uuid=SPAN(cccd)},
    {.handle=16,.permissions=BTH_ATT_READ,.uuid=SPAN(declaration),.value=SPAN(meter_decl)},
    {.handle=17,.maximum_length=3,.permissions=BTH_ATT_READ,.uuid=SPAN(meter)},
    {.handle=18,.related_handle=17,.maximum_length=2,.permissions=BTH_ATT_READ|BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND,
        .kind=BTH_ATT_CCCD,.cccd_index=2,.cccd_mask=1,.uuid=SPAN(cccd)}
};
bool bth_speaker_gatt_init(bth_att_t *att,bth_att_attribute_t attributes[BTH_SPEAKER_GATT_COUNT],bth_span_t name) {
    if (!att || !attributes || !name.data || !name.len || name.len>63) return false;
    memcpy(attributes,model,sizeof model);attributes[2].value=name;
    return bth_att_init(att,attributes,BTH_SPEAKER_GATT_COUNT,517);
}
