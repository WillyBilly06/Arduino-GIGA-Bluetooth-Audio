#include "bth_rx_flow.h"
#include <string.h>
static bth_rx_link_t *find(bth_rx_flow_t *rx,uint16_t handle,uint32_t owner) {
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i)
        if (rx->links[i].active && rx->links[i].handle==handle && rx->links[i].owner==owner) return &rx->links[i];
    return NULL;
}
bool bth_rx_flow_init(bth_rx_flow_t *rx,uint8_t slots,uint16_t budget) {
    if (!rx || !slots || slots>BTH_RX_SLOT_LIMIT || !budget || 2u*budget>slots) return false;
    memset(rx,0,sizeof(*rx)); rx->slot_count=slots; rx->budget=budget; rx->initialized=true;
    return true;
}
bool bth_rx_flow_can_attach(const bth_rx_flow_t *rx) {
    if (!rx || !rx->initialized) return false;
    unsigned active=0;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) if (rx->links[i].active || rx->links[i].reserved) ++active;
    if (active==BTH_RX_LINK_LIMIT) return false;
    for (unsigned i=0;i<rx->slot_count;++i) if (rx->slots[i].busy) {
        bool attached=false;
        for (unsigned j=0;j<BTH_RX_LINK_LIMIT;++j)
            if (rx->links[j].active && rx->links[j].handle==rx->slots[i].handle && rx->links[j].owner==rx->slots[i].owner) attached=true;
        if (!attached) return false;
    }
    return true;
}
bool bth_rx_flow_attach(bth_rx_flow_t *rx,uint16_t handle,uint32_t owner) {
    if (!bth_rx_flow_can_attach(rx) || !owner || handle>0xeff) return false;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i)
        if ((rx->links[i].active && rx->links[i].handle==handle) ||
            ((rx->links[i].active || rx->links[i].reserved) && rx->links[i].owner==owner)) return false;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) if (!rx->links[i].active && !rx->links[i].reserved) {
        rx->links[i]=(bth_rx_link_t){.owner=owner,.handle=handle,.active=true}; return true;
    }
    return false;
}
bool bth_rx_flow_reserve(bth_rx_flow_t *rx,uint32_t owner,bth_rx_admission_t *admission) {
    if (admission) memset(admission,0,sizeof(*admission));
    if (!admission || !bth_rx_flow_can_attach(rx) || rx->next_admission_serial==UINT32_MAX) return false;
    for (unsigned i=0;owner && i<BTH_RX_LINK_LIMIT;++i)
        if ((rx->links[i].active || rx->links[i].reserved) && rx->links[i].owner==owner) return false;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) if (!rx->links[i].active && !rx->links[i].reserved) {
        const uint32_t serial=++rx->next_admission_serial;
        rx->links[i]=(bth_rx_link_t){.owner=owner,.admission_serial=serial,.reserved=true};
        *admission=(bth_rx_admission_t){serial,(uint8_t)i}; return true;
    }
    return false;
}
bool bth_rx_flow_cancel_admission(bth_rx_flow_t *rx,bth_rx_admission_t admission) {
    if (!rx || !rx->initialized || !admission.serial || admission.slot>=BTH_RX_LINK_LIMIT) return false;
    bth_rx_link_t *link=&rx->links[admission.slot];
    if (!link->reserved || link->active || link->admission_serial!=admission.serial) return false;
    memset(link,0,sizeof(*link)); return true;
}
bool bth_rx_flow_attach_reserved(bth_rx_flow_t *rx,bth_rx_admission_t admission,uint16_t handle,uint32_t owner) {
    if (!rx || !rx->initialized || !admission.serial || admission.slot>=BTH_RX_LINK_LIMIT || !owner || handle>0xeff) return false;
    bth_rx_link_t *link=&rx->links[admission.slot];
    if (!link->reserved || link->active || link->admission_serial!=admission.serial || (link->owner && link->owner!=owner)) return false;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) if (i!=admission.slot &&
        ((rx->links[i].active && rx->links[i].handle==handle) ||
        ((rx->links[i].active || rx->links[i].reserved) && rx->links[i].owner==owner))) return false;
    unsigned free_slots=0;
    for (unsigned i=0;i<rx->slot_count;++i) if (!rx->slots[i].busy) ++free_slots;
    if (free_slots<rx->budget) return false;
    *link=(bth_rx_link_t){.owner=owner,.handle=handle,.active=true}; return true;
}
bool bth_rx_flow_detach(bth_rx_flow_t *rx,uint16_t handle,uint32_t owner) {
    if (!rx || !rx->initialized) return false;
    bth_rx_link_t *link=find(rx,handle,owner);
    if (!link || link->outstanding>rx->outstanding) return false;
    rx->outstanding-=link->outstanding;
    memset(link,0,sizeof(*link));
    // Slots remain busy, carrying their old owner until the decoder releases.
    return true;
}
bth_status_t bth_rx_flow_received(bth_rx_flow_t *rx,uint8_t slot,uint16_t handle,uint32_t owner,bth_rx_lease_t *lease) {
    if (lease) memset(lease,0,sizeof(*lease));
    if (!rx || !rx->initialized || !lease || slot>=rx->slot_count) return BTH_ERR_PARAM;
    bth_rx_link_t *link=find(rx,handle,owner);
    bth_rx_slot_t *s=&rx->slots[slot];
    if (!link || s->busy || s->serial==UINT32_MAX) return BTH_ERR_STATE;
    if (rx->outstanding>=rx->budget) return BTH_ERR_CAPACITY;
    ++s->serial; s->owner=owner; s->handle=handle; s->busy=true;
    ++link->outstanding; ++rx->outstanding;
    *lease=(bth_rx_lease_t){s->serial,slot};
    return BTH_OK;
}
bth_status_t bth_rx_flow_release(bth_rx_flow_t *rx,bth_rx_lease_t lease) {
    if (!rx || !rx->initialized || lease.slot>=rx->slot_count || !lease.serial) return BTH_ERR_PARAM;
    bth_rx_slot_t *s=&rx->slots[lease.slot];
    if (!s->busy || s->serial!=lease.serial) return BTH_ERR_STATE;
    bth_rx_link_t *link=find(rx,s->handle,s->owner);
    if (link && link->pending>=link->outstanding) return BTH_ERR_STATE;
    s->busy=false;
    if (!link) return BTH_IGNORED;
    ++link->pending; return BTH_OK;
}
bth_status_t bth_rx_flow_next_completed(bth_rx_flow_t *rx,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!rx || !rx->initialized || !out || !written) return BTH_ERR_PARAM;
    uint8_t params[1+4*BTH_RX_LINK_LIMIT]={0}; size_t length=1;
    unsigned total=0;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) {
        const bth_rx_link_t *l=&rx->links[i];
        if (!l->active || !l->pending) continue;
        if (l->pending>l->outstanding) return BTH_ERR_STATE;
        params[length++]=(uint8_t)l->handle; params[length++]=(uint8_t)(l->handle>>8);
        params[length++]=(uint8_t)l->pending; params[length++]=(uint8_t)(l->pending>>8);
        ++params[0]; total+=l->pending;
    }
    if (!params[0]) return BTH_MORE;
    if (total>rx->outstanding) return BTH_ERR_STATE;
    bth_status_t st=bth_hci_command_encode(out,cap,0x0c35,params,length,written);
    if (st!=BTH_OK) return st;
    for (unsigned i=0;i<BTH_RX_LINK_LIMIT;++i) {
        rx->links[i].outstanding-=rx->links[i].pending; rx->links[i].pending=0;
    }
    rx->outstanding-=(uint16_t)total;
    return BTH_OK;
}
