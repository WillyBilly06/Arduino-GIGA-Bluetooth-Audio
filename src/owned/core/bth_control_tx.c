#include "bth_control_tx.h"
#include <string.h>
enum { FREE=0, RESERVED=1, QUEUED=2 };
void bth_control_tx_init(bth_control_tx_t *q) {
    if (!q) return;
    memset(q,0,sizeof(*q)); q->free_mask=15; q->initialized=true;
}
bool bth_control_tx_attach(bth_control_tx_t *q,uint8_t peer,uint16_t handle,uint32_t owner) {
    if (!q || !q->initialized || peer>=BTH_CONTROL_PEERS || handle>0xeff || !owner || q->peers[peer].attached) return false;
    for (unsigned i=0;i<BTH_CONTROL_PEERS;++i)
        if (q->peers[i].attached && (q->peers[i].handle==handle || q->peers[i].owner==owner)) return false;
    q->peers[peer]=(bth_control_peer_t){.owner=owner,.handle=handle,.attached=true}; return true;
}
static void release(bth_control_tx_t *q,unsigned i) {
    bth_control_slot_t *s=&q->slots[i]; const uint32_t serial=s->serial; const uint8_t peer=s->peer;
    memset(s,0,sizeof(*s)); s->serial=serial;
    --q->peers[peer].owned; --q->used; q->free_mask|=(uint8_t)(1u<<i);
}
bool bth_control_tx_detach(bth_control_tx_t *q,uint8_t peer,uint32_t owner) {
    if (!q || !q->initialized || peer>=BTH_CONTROL_PEERS || !q->peers[peer].attached || q->peers[peer].owner!=owner) return false;
    bth_acl_tx_init(&q->peers[peer].tx);
    for (unsigned i=0;i<BTH_CONTROL_SLOTS;++i)
        if (q->slots[i].state!=FREE && q->slots[i].peer==peer && q->slots[i].owner==owner) release(q,i);
    memset(&q->peers[peer],0,sizeof(q->peers[peer])); return true;
}
unsigned bth_control_tx_available(const bth_control_tx_t *q,uint8_t peer) {
    if (!q || !q->initialized || peer>=BTH_CONTROL_PEERS || !q->peers[peer].attached || q->peers[peer].owned>=BTH_CONTROL_PEER_LIMIT) return 0;
    unsigned free=0;
    for (unsigned i=0;i<BTH_CONTROL_SLOTS;++i) if ((q->free_mask&(1u<<i)) && q->slots[i].serial!=UINT32_MAX) ++free;
    unsigned limit=BTH_CONTROL_PEER_LIMIT-q->peers[peer].owned;
    return free<limit?free:limit;
}
bth_status_t bth_control_tx_reserve(bth_control_tx_t *q,uint8_t peer,bth_control_reservation_t *r) {
    if (r) memset(r,0,sizeof(*r));
    if (!q || !q->initialized || !r || peer>=BTH_CONTROL_PEERS || !q->peers[peer].attached) return BTH_ERR_PARAM;
    if (q->used>=BTH_CONTROL_SLOTS || q->peers[peer].owned>=BTH_CONTROL_PEER_LIMIT) return BTH_ERR_CAPACITY;
    for (unsigned i=0;i<BTH_CONTROL_SLOTS;++i) if ((q->free_mask&(1u<<i)) && q->slots[i].serial!=UINT32_MAX) {
        bth_control_slot_t *s=&q->slots[i];
        if (s->state!=FREE) return BTH_ERR_STATE;
        ++s->serial; s->state=RESERVED; s->peer=peer; s->owner=q->peers[peer].owner;
        ++q->peers[peer].owned; ++q->used; q->free_mask&=(uint8_t)~(1u<<i);
        *r=(bth_control_reservation_t){{s->serial,(uint8_t)i},s->data,sizeof(s->data)}; return BTH_OK;
    }
    return BTH_ERR_STATE;
}
static bth_control_slot_t *reserved(bth_control_tx_t *q,bth_control_token_t token) {
    if (token.slot>=BTH_CONTROL_SLOTS || !token.serial) return NULL;
    bth_control_slot_t *s=&q->slots[token.slot];
    if (s->serial!=token.serial || s->state!=RESERVED || s->peer>=BTH_CONTROL_PEERS ||
        !q->peers[s->peer].attached || q->peers[s->peer].owner!=s->owner) return NULL;
    return s;
}
bth_status_t bth_control_tx_commit(bth_control_tx_t *q,bth_control_token_t token,uint16_t cid,size_t length,uint16_t mtu) {
    if (!q || !q->initialized || !cid) return BTH_ERR_PARAM;
    bth_control_slot_t *s=reserved(q,token); if (!s) return BTH_ERR_STATE;
    if (length>BTH_CONTROL_BYTES || length>mtu) return BTH_ERR_CAPACITY;
    // Validate the complete nonempty CID1 bundle before queue publication.
    // No command bytes/IDs/options are interpreted or rewritten.
    if (cid==1) for (size_t pos=0;pos<length;) {
        if (length-pos<4) return BTH_ERR_LENGTH;
        const size_t command=4u+bth_read_le16(s->data+pos+2);
        if (command>length-pos) return BTH_ERR_LENGTH;
        pos+=command;
    }
    bth_control_peer_t *p=&q->peers[s->peer];
    if (p->count>=BTH_CONTROL_SLOTS) return BTH_ERR_STATE;
    s->length=(uint16_t)length; s->cid=cid; s->mtu=mtu; s->state=QUEUED;
    p->order[(p->read+p->count)&3u]=token.slot; ++p->count; return BTH_OK;
}
bth_status_t bth_control_tx_cancel(bth_control_tx_t *q,bth_control_token_t token) {
    if (!q || !q->initialized) return BTH_ERR_PARAM;
    if (!reserved(q,token)) return BTH_ERR_STATE;
    release(q,token.slot); return BTH_OK;
}
bth_status_t bth_control_tx_next(bth_control_tx_t *q,const uint32_t owners[BTH_CONTROL_PEERS],bth_hci_flow_t *flow,
    uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!q || !q->initialized || !owners || !flow || !out || !written) return BTH_ERR_PARAM;
    for (unsigned k=0;k<BTH_CONTROL_PEERS;++k) {
        unsigned peer=(q->next_peer+k)&1u; bth_control_peer_t *p=&q->peers[peer];
        if (!p->attached || !p->count) continue;
        if (owners[peer]!=p->owner) return BTH_ERR_STATE;
        unsigned i=p->order[p->read];
        if (i>=BTH_CONTROL_SLOTS) return BTH_ERR_STATE;
        const bth_control_slot_t *s=&q->slots[i];
        if (s->state!=QUEUED || s->peer!=peer || s->owner!=p->owner) return BTH_ERR_STATE;
        if (!p->tx.active) {
            if (p->cursor>s->length || (p->cursor && p->cursor==s->length)) return BTH_ERR_STATE;
            size_t bytes=s->length-p->cursor;
            if (s->cid==1 && bytes) {
                if (bytes<4) return BTH_ERR_LENGTH;
                const size_t command=4u+bth_read_le16(s->data+p->cursor+2);
                if (command>bytes) return BTH_ERR_LENGTH;
                bytes=command;
            }
            bth_status_t st=bth_acl_tx_begin(&p->tx,p->handle,p->owner,s->cid,(bth_span_t){s->data+p->cursor,bytes},s->mtu);
            if (st!=BTH_OK) return st;
        }
        const size_t sdu_bytes=p->tx.data.len;
        bth_status_t st=bth_acl_tx_next(&p->tx,owners[peer],flow,out,cap,written);
        if (st==BTH_MORE) continue;
        if (st!=BTH_OK) return st;
        q->next_peer=(uint8_t)(peer^1u);
        if (!p->tx.active) {
            p->cursor+=(uint16_t)sdu_bytes;
            if (p->cursor==s->length) { p->cursor=0; p->read=(uint8_t)((p->read+1)&3u); --p->count; release(q,i); }
        }
        return BTH_OK;
    }
    return BTH_MORE;
}
