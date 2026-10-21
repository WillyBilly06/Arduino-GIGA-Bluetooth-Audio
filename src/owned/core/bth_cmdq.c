#include "bth_cmdq.h"
#include <string.h>
void bth_cmdq_init(bth_cmdq_t *q) {
    if (!q) return;
    memset(q,0,sizeof *q); q->free_mask=0xff; q->initialized=true;
}
unsigned bth_cmdq_available(const bth_cmdq_t *q) {
    return q && q->initialized && q->used<=BTH_CMD_QUEUE_LIMIT ? BTH_CMD_QUEUE_LIMIT-q->used : 0;
}
bth_status_t bth_cmdq_push(bth_cmdq_t *q,uint16_t opcode,const uint8_t *params,size_t len,bool urgent) {
    return bth_cmdq_push_owned(q,opcode,params,len,urgent,0);
}
bth_status_t bth_cmdq_push_owned(bth_cmdq_t *q,uint16_t opcode,const uint8_t *params,size_t len,bool urgent,uint32_t owner) {
    if (!q || !q->initialized || (!params && len)) return BTH_ERR_PARAM;
    if (len>BTH_CMD_PARAM_LIMIT) return BTH_ERR_LENGTH;
    if (q->used>=BTH_CMD_QUEUE_LIMIT) return BTH_ERR_CAPACITY;
    if (!q->free_mask) return BTH_ERR_STATE;
    const unsigned index=31u-(unsigned)__builtin_clz((unsigned)q->free_mask);
    const unsigned group=urgent?1u:0u;
    bth_command_t *command=&q->slots[index];
    command->owner=owner; command->opcode=opcode; command->len=(uint8_t)len;
    if (len) memcpy(command->params,params,len);
    q->order[group][(q->read[group]+q->count[group])&7u]=(uint8_t)index;
    q->free_mask&=(uint8_t)~(1u<<index); ++q->count[group]; ++q->used;
    return BTH_OK;
}
static void clear_command(bth_command_t *c) {
    volatile uint8_t *p=(volatile uint8_t *)c;
    for (size_t n=0;n<sizeof *c;++n) p[n]=0;
}
static unsigned cancel(bth_cmdq_t *q,uint32_t owner,uint16_t opcode,bool all) {
    if (!q || !q->initialized || !owner) return 0;
    unsigned removed=0;
    for (unsigned group=0;group<2;++group) {
        uint8_t kept[8], count=0;
        for (unsigned n=0;n<q->count[group];++n) {
            const uint8_t i=q->order[group][(q->read[group]+n)&7u];
            if (q->slots[i].owner==owner && (all || q->slots[i].opcode==opcode)) {
                clear_command(&q->slots[i]); q->free_mask|=(uint8_t)(1u<<i); --q->used; ++removed;
            } else kept[count++]=i;
        }
        if (count) memcpy(q->order[group],kept,count);
        q->read[group]=0; q->count[group]=count;
    }
    return removed;
}
unsigned bth_cmdq_cancel_owner(bth_cmdq_t *q,uint32_t owner) {return cancel(q,owner,0,true);}
unsigned bth_cmdq_cancel_owned(bth_cmdq_t *q,uint32_t owner,uint16_t opcode) {return opcode?cancel(q,owner,opcode,false):0;}
bth_status_t bth_cmdq_complete(bth_cmdq_t *q,const bth_h4_frame_t *e,uint32_t *owner) {
    if (owner) *owner=0;
    if (!q || !q->initialized || !e || !owner) return BTH_ERR_PARAM;
    if (e->type!=BTH_H4_EVENT) return BTH_UNSUPPORTED;
    const bth_span_t p=e->event.params;
    if (!p.data) return BTH_ERR_PARAM;
    uint16_t opcode;
    if (e->event.code==0x0e) { if (p.len<3) return BTH_ERR_LENGTH; opcode=bth_read_le16(p.data+1); }
    else if (e->event.code==0x0f) { if (p.len!=4) return BTH_ERR_LENGTH; opcode=bth_read_le16(p.data+2); }
    else return BTH_UNSUPPORTED;
    for (unsigned n=0;n<q->sent_count;++n) if (q->sent[n].opcode==opcode) {
        *owner=q->sent[n].owner;
        for (unsigned j=n+1;j<q->sent_count;++j) q->sent[j-1]=q->sent[j];
        --q->sent_count; memset(&q->sent[q->sent_count],0,sizeof q->sent[0]);
        return BTH_OK;
    }
    return BTH_IGNORED;
}
bth_status_t bth_cmdq_next(bth_cmdq_t *q,bth_hci_flow_t *flow,uint8_t *out,size_t capacity,size_t *written) {
    if (!written) return BTH_ERR_PARAM;
    *written=0;
    if (!q || !q->initialized || !flow || !out) return BTH_ERR_PARAM;
    if (!q->used || !flow->command_credits) return BTH_MORE;
    const unsigned group=q->count[1]?1u:0u;
    if (!q->count[group]) return BTH_ERR_STATE;
    const unsigned index=q->order[group][q->read[group]];
    if (index>=BTH_CMD_QUEUE_LIMIT || (q->free_mask&(1u<<index))) return BTH_ERR_STATE;
    bth_command_t *command=&q->slots[index];
    if (command->owner && q->sent_count>=BTH_CMD_QUEUE_LIMIT) return BTH_ERR_CAPACITY;
    bth_status_t status=bth_hci_command_encode(out,capacity,command->opcode,command->params,command->len,written);
    if (status!=BTH_OK) return status;
    if (!bth_hci_flow_take_command(flow)) { *written=0; return BTH_ERR_STATE; }
    if (command->owner) q->sent[q->sent_count++]=(bth_command_sent_t){command->owner,command->opcode};
    // Queued commands can contain link keys and PINs. Clear their copied bytes
    // explicitly so the compiler cannot remove this cleanup as a dead store.
    clear_command(command);
    q->read[group]=(uint8_t)((q->read[group]+1u)&7u); --q->count[group]; --q->used;
    q->free_mask|=(uint8_t)(1u<<index);
    return BTH_OK;
}
