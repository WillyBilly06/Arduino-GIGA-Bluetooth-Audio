#include "bth_acl_tx.h"
#include <string.h>
void bth_acl_tx_init(bth_acl_tx_t *tx) { if (tx) memset(tx,0,sizeof(*tx)); }
bth_status_t bth_acl_tx_begin(bth_acl_tx_t *tx,uint16_t handle,uint32_t owner,uint16_t cid,bth_span_t data,uint16_t mtu) {
    if (!tx || !owner || handle>0xeff || !cid || (data.len && !data.data) || data.len>UINT16_MAX) return BTH_ERR_PARAM;
    if (tx->active) return BTH_ERR_STATE;
    if (data.len>mtu) return BTH_ERR_CAPACITY;
    *tx=(bth_acl_tx_t){.data=data,.owner=owner,.handle=handle,.cid=cid,.active=true};
    return BTH_OK;
}
bool bth_acl_tx_cancel(bth_acl_tx_t *tx,uint32_t owner) {
    if (!tx || !tx->active || tx->owner!=owner) return false;
    bth_acl_tx_init(tx); return true;
}
bth_status_t bth_acl_tx_next(bth_acl_tx_t *tx,uint32_t owner,bth_hci_flow_t *flow,uint8_t *out,size_t cap,size_t *written) {
    return bth_acl_tx_next_limited(tx,owner,flow,0,out,cap,written);
}
bth_status_t bth_acl_tx_next_limited(bth_acl_tx_t *tx,uint32_t owner,bth_hci_flow_t *flow,uint16_t max_payload,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!tx || !flow || !out || !written) return BTH_ERR_PARAM;
    if (!tx->active) return BTH_MORE;
    if (owner!=tx->owner) return BTH_ERR_STATE;
    bth_acl_pool_t *pool=NULL;
    for (unsigned i=0;i<BTH_HCI_LINK_LIMIT;++i) if (flow->links[i].active && flow->links[i].handle==tx->handle) {
        pool=flow->links[i].type==BTH_LINK_LE && flow->le.total?&flow->le:&flow->classic;
        break;
    }
    if (!pool || !pool->length || !pool->total) return BTH_ERR_STATE;
    uint32_t total=(uint32_t)tx->data.len+4;
    if (tx->position>=total) return BTH_ERR_STATE;
    uint32_t count=total-tx->position;
    if (count>pool->length) count=pool->length;
    if (max_payload && count>max_payload) count=max_payload;
    if (cap<5u+count) return BTH_ERR_CAPACITY;
    if (!pool->available) return BTH_MORE;
    if (!bth_hci_flow_take_acl(flow,tx->handle,(uint16_t)count)) return BTH_ERR_STATE;
    uint16_t flags=(uint16_t)(tx->handle|(tx->position?0x1000:0));
    out[0]=BTH_H4_ACL; out[1]=(uint8_t)flags; out[2]=(uint8_t)(flags>>8);
    out[3]=(uint8_t)count; out[4]=(uint8_t)(count>>8);
    const uint8_t header[4]={(uint8_t)tx->data.len,(uint8_t)(tx->data.len>>8),(uint8_t)tx->cid,(uint8_t)(tx->cid>>8)};
    uint32_t pos=tx->position,copied=0;
    while (pos<4 && copied<count) out[5+copied++]=header[pos++];
    if (copied<count) memcpy(out+5+copied,tx->data.data+(pos-4),count-copied);
    tx->position+=count;
    if (tx->position==total) { tx->active=false; tx->data=(bth_span_t){0}; }
    *written=5+count; return BTH_OK;
}
