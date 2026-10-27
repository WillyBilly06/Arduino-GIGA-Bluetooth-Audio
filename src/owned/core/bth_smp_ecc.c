#include "bth_smp_ecc.h"
#include "bth_smp_crypto.h"
#include <string.h>
enum {TIMEOUT_MS=5000};
static bool waiting(const bth_smp_ecc_t *e) {return e->state==BTH_SMP_ECC_WAIT_PUBLIC || e->state==BTH_SMP_ECC_WAIT_DHKEY;}
static bool sent_record(const bth_smp_ecc_t *e) {
    for(unsigned i=0;i<e->commands->sent_count;++i)if(e->commands->sent[i].owner==e->owner && e->commands->sent[i].opcode==e->opcode)return true;
    return false;
}
static void clear_public(bth_smp_ecc_t *e) {bth_smp_secret_clear(e->public_key,sizeof e->public_key);e->have_public=false;e->public_owner=0;}
static void clear_job(bth_smp_ecc_t *e) {
    bth_smp_secret_clear(e->dhkey,sizeof e->dhkey);e->owner=0;e->kind=BTH_SMP_ECC_NONE;e->opcode=0;e->error=0;
    e->submitted=e->accepted=e->abandoned=e->async_received=e->result_pending=false;
}
static bth_status_t finish(bth_smp_ecc_t *e,uint8_t error) {
    if(error){bth_smp_secret_clear(e->dhkey,sizeof e->dhkey);if(e->kind==BTH_SMP_ECC_PUBLIC)clear_public(e);}
    else if(e->kind==BTH_SMP_ECC_PUBLIC){e->have_public=true;e->public_owner=e->owner;}
    if(e->abandoned){if(e->public_owner==e->owner)clear_public(e);clear_job(e);e->state=BTH_SMP_ECC_IDLE;}
    else{e->error=error;e->result_pending=true;e->state=BTH_SMP_ECC_DONE;}
    return BTH_OK;
}
static void poison(bth_smp_ecc_t *e,uint8_t error) {
    clear_public(e);bth_smp_secret_clear(e->dhkey,sizeof e->dhkey);
    e->error=error;e->result_pending=!e->abandoned;e->state=BTH_SMP_ECC_POISONED;
    if(e->abandoned)clear_job(e);
}
bool bth_smp_ecc_after_reset(bth_smp_ecc_t *e,bth_cmdq_t *commands) {
    if(!e)return false;
    bth_smp_secret_clear(e,sizeof *e);
    if(!commands || !commands->initialized || commands->used || commands->sent_count)return false;
    e->commands=commands;e->initialized=true;return true;
}
static bth_status_t request(bth_smp_ecc_t *e,uint32_t owner,const uint8_t *remote,uint32_t now,bool public_key) {
    if(!e || !e->initialized || !owner || (!public_key && !remote))return BTH_ERR_PARAM;
    if(e->state==BTH_SMP_ECC_POISONED)return BTH_ERR_STATE;
    if(e->state!=BTH_SMP_ECC_IDLE)return BTH_MORE;
    if(public_key && e->have_public && e->public_owner!=owner)return BTH_MORE;
    if(!public_key && (!e->have_public || e->public_owner!=owner))return BTH_ERR_STATE;
    const uint16_t opcode=public_key?0x2025:0x2026;
    bth_status_t st=bth_cmdq_push_owned(e->commands,opcode,remote,public_key?0:64,false,owner);
    if(st==BTH_ERR_CAPACITY)return BTH_MORE;
    if(st!=BTH_OK)return st;
    clear_job(e);if(public_key)clear_public(e);
    e->owner=owner;e->opcode=opcode;e->kind=public_key?BTH_SMP_ECC_PUBLIC:BTH_SMP_ECC_DHKEY;
    e->state=public_key?BTH_SMP_ECC_WAIT_PUBLIC:BTH_SMP_ECC_WAIT_DHKEY;e->deadline=now+TIMEOUT_MS;return BTH_OK;
}
bth_status_t bth_smp_ecc_read_public(bth_smp_ecc_t *e,uint32_t owner,uint32_t now) {return request(e,owner,NULL,now,true);}
bth_status_t bth_smp_ecc_generate(bth_smp_ecc_t *e,uint32_t owner,const uint8_t remote[64],uint32_t now) {return request(e,owner,remote,now,false);}
bth_status_t bth_smp_ecc_command_sent(bth_smp_ecc_t *e,uint32_t owner,uint16_t opcode,uint32_t now) {
    if(!e || !e->initialized)return BTH_ERR_PARAM;
    if(!waiting(e) || e->owner!=owner || e->opcode!=opcode || e->submitted)return BTH_IGNORED;
    if(!sent_record(e))return BTH_ERR_STATE;
    e->submitted=true;e->deadline=now+TIMEOUT_MS;return BTH_OK;
}
bth_status_t bth_smp_ecc_on_event(bth_smp_ecc_t *e,const bth_h4_frame_t *event,uint32_t command_owner) {
    if(!e || !e->initialized || !event)return BTH_ERR_PARAM;
    if(!waiting(e) || event->type!=BTH_H4_EVENT)return BTH_IGNORED;
    const bth_span_t p=event->event.params;if(p.len && !p.data)return BTH_ERR_PARAM;
    if(event->event.code==0x0f || event->event.code==0x0e) {
        const bool status=event->event.code==0x0f;
        if(p.len<(status?4u:3u))return BTH_ERR_LENGTH;
        const uint16_t opcode=bth_read_le16(p.data+(status?2:1));
        if(opcode!=e->opcode || command_owner!=e->owner || e->accepted || (!e->submitted && !sent_record(e)))return BTH_IGNORED;
        if(p.len!=4){poison(e,0xfe);return BTH_ERR_LENGTH;}
        e->submitted=true;const uint8_t error=p.data[status?0:3];
        if(!status && !error){poison(e,0xfe);return BTH_ERR_STATE;}
        if(error){if(e->async_received){poison(e,0xfe);return BTH_ERR_STATE;}return finish(e,error);}
        e->accepted=true;return e->async_received?finish(e,e->error):BTH_OK;
    }
    if(event->event.code!=0x3e || !p.len || (p.data[0]!=8 && p.data[0]!=9))return BTH_IGNORED;
    if(!e->submitted && !sent_record(e))return BTH_IGNORED;
    const bool public_key=e->kind==BTH_SMP_ECC_PUBLIC;
    if(p.data[0]!=(public_key?8:9)){poison(e,0xfe);return BTH_ERR_STATE;}
    if(p.len!=(public_key?66u:34u))return BTH_ERR_LENGTH;
    if(e->async_received){poison(e,0xfe);return BTH_ERR_STATE;}
    e->submitted=true;e->async_received=true;e->error=p.data[1];
    if(!e->error)memcpy(public_key?e->public_key:e->dhkey,p.data+2,public_key?64:32);
    else if(public_key)clear_public(e);else bth_smp_secret_clear(e->dhkey,sizeof e->dhkey);
    return e->accepted?finish(e,e->error):BTH_MORE;
}
bth_status_t bth_smp_ecc_take(bth_smp_ecc_t *e,uint32_t owner,bth_smp_ecc_result_t *result) {
    if(result)bth_smp_secret_clear(result,sizeof *result);
    if(!e || !e->initialized || !result || !owner)return BTH_ERR_PARAM;
    if(!e->result_pending)return BTH_MORE;
    if(e->owner!=owner)return BTH_IGNORED;
    result->owner=owner;result->kind=e->kind;result->status=e->error;
    if(!e->error){result->length=e->kind==BTH_SMP_ECC_PUBLIC?64:32;memcpy(result->data,e->kind==BTH_SMP_ECC_PUBLIC?e->public_key:e->dhkey,result->length);}
    clear_job(e);if(e->state!=BTH_SMP_ECC_POISONED)e->state=BTH_SMP_ECC_IDLE;return BTH_OK;
}
bth_status_t bth_smp_ecc_abandon(bth_smp_ecc_t *e,uint32_t owner) {
    if(!e || !e->initialized || !owner)return BTH_ERR_PARAM;
    const bool key=e->public_owner==owner;if(key)clear_public(e);
    if(e->owner!=owner)return key?BTH_OK:BTH_IGNORED;
    if(waiting(e) && (e->submitted || sent_record(e))){e->abandoned=true;return BTH_OK;}
    (void)bth_cmdq_cancel_owned(e->commands,owner,e->opcode);
    clear_job(e);if(e->state!=BTH_SMP_ECC_POISONED)e->state=BTH_SMP_ECC_IDLE;return BTH_OK;
}
bth_status_t bth_smp_ecc_expire(bth_smp_ecc_t *e,uint32_t now) {
    if(!e || !e->initialized)return BTH_ERR_PARAM;
    if(!waiting(e))return BTH_IGNORED;
    if((int32_t)(now-e->deadline)<0)return BTH_MORE;
    if(e->submitted || sent_record(e))poison(e,0xff);
    else{(void)bth_cmdq_cancel_owned(e->commands,e->owner,e->opcode);(void)finish(e,0xff);}
    return BTH_OK;
}
