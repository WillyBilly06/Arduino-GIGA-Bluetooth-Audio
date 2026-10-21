#include "bth_links.h"
#include <string.h>

static void clear_link(bth_peer_link_t *peer) {
    volatile uint8_t *p=(volatile uint8_t *)peer;
    for (size_t n=0;n<sizeof *peer;++n) p[n]=0;
}
static void erase_bond(bth_bond_t *bond) {
    volatile uint8_t *p=(volatile uint8_t *)bond;
    for (size_t n=0;n<sizeof *bond;++n) p[n]=0;
}
static bth_peer_link_t *classic_peer(bth_links_t *l,const uint8_t address[6]) {
    return l->classic.state==BTH_PEER_CONNECTED && !memcmp(l->classic.peer,address,6)?&l->classic:NULL;
}
static bool valid_bond(const bth_bond_t *b) {
    return (b->key_type==0 && b->pin_len && b->pin_len<=16) ||
           b->key_type==4 || b->key_type==5 || b->key_type==7 || b->key_type==8;
}
static void clear_security(bth_peer_link_t *p) {
    p->authenticated=false; p->key_size_verified=false; p->key_size=0;
    p->confirmation_pending=false; p->key_pending=false;
    p->cached_key_pending=p->cached_key_accepted=p->cached_key_encrypted=false;p->stale_size_reads=p->stale_key_replies=0;
    erase_bond(&p->pending_bond);
    p->retry_opcode=0;p->retry_at_ms=0;p->collision_retries=0;p->retry_results_pending=0;
}
static uint8_t retire_size_reads(bth_links_t *l,const bth_peer_link_t *peer) {
    uint8_t sent=0;
    for(unsigned i=0;i<l->commands->sent_count;++i)if(l->commands->sent[i].owner==peer->owner && l->commands->sent[i].opcode==0x1408)++sent;
    (void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x1408);return sent;
}
static uint8_t retire_key_replies(bth_links_t *l,const bth_peer_link_t *peer) {
    uint8_t sent=0;
    for(unsigned i=0;i<l->commands->sent_count;++i)if(l->commands->sent[i].owner==peer->owner && l->commands->sent[i].opcode==0x040b)++sent;
    (void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x040b);return sent;
}
static bool cached_key_submitted(const bth_links_t *l,const bth_peer_link_t *peer) {
    if(peer->stale_key_replies)return false;
    if(peer->cached_key_accepted)return true;
    if(peer->cached_key_pending)for(unsigned i=0;i<l->commands->sent_count;++i)
        if(l->commands->sent[i].owner==peer->owner && l->commands->sent[i].opcode==0x040b)return true;
    return false;
}
static bool cached_reply_outstanding(const bth_links_t *l,const bth_peer_link_t *peer) {
    unsigned sent=0;
    for(unsigned i=0;i<l->commands->sent_count;++i)if(l->commands->sent[i].owner==peer->owner && l->commands->sent[i].opcode==0x040b)++sent;
    if(sent>peer->stale_key_replies)return true;
    for(unsigned i=0;i<BTH_CMD_QUEUE_LIMIT;++i)if(l->commands->slots[i].owner==peer->owner && l->commands->slots[i].opcode==0x040b)return true;
    return false;
}
static void cached_encryption_verified(bth_links_t *l,bth_peer_link_t *peer) {
    // Remote-initiated authentication has no local Authentication Complete.
    // A validated stored key accepted by the controller, a subsequent actual
    // encryption transition and a current key-size result are all required.
    if(!peer->disconnecting && peer->bonded && peer->cached_key_accepted && peer->cached_key_encrypted &&
        peer->encryption && peer->key_size_verified && !peer->key_pending) {
        peer->authenticated=true;
        (void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x0411);
    }
}
static uint32_t new_owner(bth_links_t *l) {
    const uint32_t owner=l->next_owner++;
    if (!l->next_owner) l->next_owner=1;
    return owner;
}
static bth_status_t peer_command(bth_links_t *l,const bth_peer_link_t *p,uint16_t opcode,const uint8_t *data,size_t len) {
    return bth_cmdq_push_owned(l->commands,opcode,data,len,true,p?p->owner:0);
}
static bth_status_t disconnect_security(bth_links_t *l,bth_peer_link_t *p,uint8_t reason) {
    clear_security(p); p->last_error=reason;
    if (p->disconnecting) return BTH_IGNORED;
    const uint8_t data[]={(uint8_t)p->handle,(uint8_t)(p->handle>>8),reason};
    bth_status_t st=peer_command(l,p,0x0406,data,sizeof data);
    if (st==BTH_OK) p->disconnecting=true;
    return st;
}
static bth_status_t collision_retry(bth_links_t *l,bth_peer_link_t *p,uint16_t opcode) {
    if(p->disconnecting)return BTH_IGNORED;
    const uint8_t result_bit=opcode==0x0411?1:2;
    const bool superseded=(p->retry_results_pending&result_bit) && !p->key_pending && bth_links_secure(l,p->handle);
    p->retry_results_pending&=(uint8_t)~result_bit;
    // A retry already in flight can lose to actual remote encryption. Current
    // encryption+key-size proof for this owner supersedes its late collision.
    if(superseded)return BTH_IGNORED;
    p->encryption=0;p->key_size=0;p->key_size_verified=false;
    if(opcode==0x0411)p->authenticated=false;
    if(p->collision_retries>=5)return disconnect_security(l,p,5);
    ++p->collision_retries;p->retry_opcode=opcode;p->retry_at_ms=l->security_now_ms+1000;
    return BTH_OK;
}
bth_status_t bth_links_security_tick(bth_links_t *l,uint32_t now_ms) {
    if(!l || !l->initialized)return BTH_ERR_PARAM;
    l->security_now_ms=now_ms;bth_peer_link_t *p=&l->classic;
    if(p->state!=BTH_PEER_CONNECTED || p->disconnecting || !p->retry_opcode)return BTH_IGNORED;
    if((int32_t)(now_ms-p->retry_at_ms)<0)return BTH_MORE;
    uint8_t data[]={(uint8_t)p->handle,(uint8_t)(p->handle>>8),1};
    bth_status_t st=bth_cmdq_push_owned(l->commands,p->retry_opcode,data,p->retry_opcode==0x0411?2:3,false,p->owner);
    if(st==BTH_ERR_CAPACITY)return BTH_MORE;
    if(st==BTH_OK){p->retry_results_pending|=p->retry_opcode==0x0411?1:2;p->retry_opcode=0;}
    return st;
}
bool bth_links_init(bth_links_t *l,bth_hci_flow_t *f,bth_cmdq_t *q,
                    const bth_security_policy_t *p,const bth_security_callbacks_t *cb,void *context) {
    if (!l) return false;
    memset(l,0,sizeof *l);
    if (!f || !q || !q->initialized || !p || p->io_capability>4 || p->auth_requirement>5 ||
        p->min_key_size<7 || p->min_key_size>16 || p->pin_len>16 ||
        (p->io_capability==3 && (p->auth_requirement&1u))) return false;
    l->flow=f; l->commands=q; l->policy=*p; l->context=context;
    if (cb) l->callbacks=*cb;
    l->initialized=true; l->next_owner=1;
    return true;
}
const bth_peer_link_t *bth_links_find(const bth_links_t *l,uint16_t handle) {
    if (!l || !l->initialized) return NULL;
    if (l->classic.state==BTH_PEER_CONNECTED && l->classic.handle==handle) return &l->classic;
    if (l->le.state==BTH_PEER_CONNECTED && l->le.handle==handle) return &l->le;
    return NULL;
}
bool bth_links_secure(const bth_links_t *l,uint16_t handle) {
    const bth_peer_link_t *p=bth_links_find(l,handle);
    if (!p || p->disconnecting || p->type!=BTH_LINK_CLASSIC || !p->authenticated || !p->encryption ||
        !p->key_size_verified || p->key_size<l->policy.min_key_size || p->key_size>16 ||
        (p->key_type!=0 && p->key_type!=4 && p->key_type!=5 && p->key_type!=7 && p->key_type!=8)) return false;
    if ((p->key_type==7 || p->key_type==8) && (p->key_size!=16 || p->encryption!=2)) return false;
    if ((l->policy.auth_requirement&1u) && p->key_type!=5 && p->key_type!=8) return false;
    return true;
}
bth_status_t bth_links_disconnect(bth_links_t *l,uint16_t handle,uint8_t reason) {
    if (!l || !l->initialized || !reason) return BTH_ERR_PARAM;
    bth_peer_link_t *p=l->classic.state==BTH_PEER_CONNECTED && l->classic.handle==handle?&l->classic:
        l->le.state==BTH_PEER_CONNECTED && l->le.handle==handle?&l->le:NULL;
    return p?disconnect_security(l,p,reason):BTH_IGNORED;
}
static bth_status_t connection_request(bth_links_t *l,bth_span_t p) {
    if (p.len!=10) return BTH_ERR_LENGTH;
    uint8_t reply[7]; memcpy(reply,p.data,6);
    const bool busy=l->classic.state!=BTH_PEER_EMPTY;
    if (busy && l->classic.state==BTH_PEER_PENDING && !memcmp(p.data,l->classic.peer,6)) return BTH_IGNORED;
    const bool accept=!busy && p.data[9]==1;
    reply[6]=accept?1u:(p.data[9]!=1?0x0fu:0x0du);
    const uint32_t owner=accept?new_owner(l):0;
    bth_status_t status=bth_cmdq_push_owned(l->commands,accept?0x0409:0x040a,reply,sizeof reply,true,owner);
    if (status==BTH_OK && accept) {
        memcpy(l->classic.peer,p.data,6); l->classic.type=BTH_LINK_CLASSIC;
        l->classic.state=BTH_PEER_PENDING; l->classic.owner=owner;
    }
    return status;
}
static bth_status_t connected_classic(bth_links_t *l,bth_span_t p) {
    if (p.len!=11) return BTH_ERR_LENGTH;
    if (p.data[0]) {
        if (l->classic.state==BTH_PEER_PENDING && !memcmp(l->classic.peer,p.data+3,6)) {
            (void)bth_cmdq_cancel_owner(l->commands,l->classic.owner); clear_link(&l->classic);
        }
        return BTH_OK;
    }
    const uint16_t handle=bth_read_le16(p.data+1);
    if (handle>0x0effu || p.data[10]>1) return BTH_ERR_PARAM;
    if (p.data[9]!=1) return BTH_UNSUPPORTED;
    if (l->classic.state==BTH_PEER_CONNECTED || (l->classic.state==BTH_PEER_PENDING &&
        memcmp(l->classic.peer,p.data+3,6))) return BTH_ERR_STATE;
    if (!bth_cmdq_available(l->commands)) return BTH_ERR_CAPACITY;
    if (!bth_hci_flow_attach(l->flow,handle,BTH_LINK_CLASSIC)) return BTH_ERR_STATE;
    const uint8_t auth[]={(uint8_t)handle,(uint8_t)(handle>>8)};
    const uint32_t owner=l->classic.state==BTH_PEER_PENDING?l->classic.owner:new_owner(l);
    // A locally initiated authentication request must not overtake the remote
    // host's urgent key/IO-capability/confirmation responses.
    bth_status_t status=bth_cmdq_push_owned(l->commands,0x0411,auth,sizeof auth,false,owner);
    if (status!=BTH_OK) { (void)bth_hci_flow_detach(l->flow,handle); return status; }
    clear_link(&l->classic);
    memcpy(l->classic.peer,p.data+3,6); l->classic.handle=handle; l->classic.owner=owner;
    l->classic.type=BTH_LINK_CLASSIC; l->classic.state=BTH_PEER_CONNECTED;
    l->classic.encryption=p.data[10]; l->classic.key_type=0xff;
    return BTH_OK;
}
static bth_status_t connected_le(bth_links_t *l,bth_span_t p) {
    if (!p.len) return BTH_ERR_LENGTH;
    const bool enhanced=p.data[0]==0x0a;
    if (p.data[0]!=1 && !enhanced) return BTH_UNSUPPORTED;
    if (p.len!=(enhanced?31u:19u)) return BTH_ERR_LENGTH;
    if (p.data[1]) return BTH_IGNORED;
    const uint16_t handle=bth_read_le16(p.data+2);
    if (handle>0x0effu || p.data[4]>1 || p.data[5]>(enhanced?3u:1u)) return BTH_ERR_PARAM;
    if (l->le.state!=BTH_PEER_EMPTY || !bth_hci_flow_attach(l->flow,handle,BTH_LINK_LE)) return BTH_ERR_STATE;
    clear_link(&l->le); memcpy(l->le.peer,p.data+6,6);
    l->le.handle=handle; l->le.type=BTH_LINK_LE; l->le.state=BTH_PEER_CONNECTED;
    l->le.role=p.data[4]; l->le.address_type=p.data[5]; l->le.key_type=0xff; l->le.owner=new_owner(l);
    return BTH_OK;
}
static bth_status_t disconnected(bth_links_t *l,bth_span_t p) {
    if (p.len!=4) return BTH_ERR_LENGTH;
    if (p.data[0]) return BTH_IGNORED;
    const uint16_t handle=bth_read_le16(p.data+1);
    if (handle>0x0effu) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=l->classic.state==BTH_PEER_CONNECTED && l->classic.handle==handle?&l->classic:
                          l->le.state==BTH_PEER_CONNECTED && l->le.handle==handle?&l->le:NULL;
    if (!peer) return BTH_IGNORED;
    const bool detached=bth_hci_flow_detach(l->flow,handle);
    (void)bth_cmdq_cancel_owner(l->commands,peer->owner);
    clear_link(peer);
    return detached?BTH_OK:BTH_ERR_STATE;
}
static bth_status_t key_request(bth_links_t *l,bth_span_t p) {
    if (p.len!=6) return BTH_ERR_LENGTH;
    bth_peer_link_t *peer=classic_peer(l,p.data);
    if(peer && peer->cached_key_pending && cached_reply_outstanding(l,peer))return BTH_IGNORED;
    const uint8_t stale=peer?retire_size_reads(l,peer):0,old_keys=peer?retire_key_replies(l,peer):0;
    bth_bond_t bond={0};
    const bool present=peer && !peer->disconnecting && l->callbacks.load && l->callbacks.load(l->context,p.data,&bond) && valid_bond(&bond);
    uint8_t reply[22]; memcpy(reply,p.data,6);
    if (present) memcpy(reply+6,bond.key,16);
    bth_status_t result=peer_command(l,peer,present?0x040b:0x040c,reply,present?22:6);
    if (peer) {
        // A reauthentication attempt immediately revokes profile authorization.
        clear_security(peer);peer->stale_size_reads=stale;peer->stale_key_replies=old_keys;
        peer->bonded=present && result==BTH_OK;
        peer->cached_key_pending=peer->bonded;
        peer->key_type=peer->bonded?bond.key_type:0xff;
    }
    erase_bond(&bond);
    volatile uint8_t *secret=reply;
    for (size_t n=0;n<sizeof reply;++n) secret[n]=0;
    return result;
}
static bth_status_t pin_request(bth_links_t *l,bth_span_t p) {
    if (p.len!=6) return BTH_ERR_LENGTH;
    const bool accept=classic_peer(l,p.data) && l->policy.pin_len;
    uint8_t reply[23]={0}; memcpy(reply,p.data,6);
    if (accept) { reply[6]=l->policy.pin_len; memcpy(reply+7,l->policy.pin,l->policy.pin_len); }
    bth_status_t result=peer_command(l,classic_peer(l,p.data),accept?0x040d:0x040e,reply,accept?23:6);
    volatile uint8_t *secret=reply;
    for (size_t n=0;n<sizeof reply;++n) secret[n]=0;
    return result;
}
static bth_status_t io_request(bth_links_t *l,bth_span_t p) {
    if (p.len!=6) return BTH_ERR_LENGTH;
    const bool accept=classic_peer(l,p.data)!=NULL;
    uint8_t reply[9]; memcpy(reply,p.data,6);
    reply[6]=accept?l->policy.io_capability:0x18;
    reply[7]=0; reply[8]=l->policy.auth_requirement;
    return peer_command(l,classic_peer(l,p.data),accept?0x042b:0x0434,reply,accept?9:7);
}
bth_status_t bth_links_confirm(bth_links_t *l,const uint8_t address[6],bool accept) {
    if (!l || !l->initialized || !address) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=classic_peer(l,address);
    if (!peer) return BTH_IGNORED;
    if (!peer->confirmation_pending) return BTH_ERR_STATE;
    const bth_status_t result=peer_command(l,peer,accept?0x042c:0x042d,address,6);
    if (result==BTH_OK) peer->confirmation_pending=false;
    return result;
}
static bth_status_t confirmation_request(bth_links_t *l,bth_span_t p) {
    if (p.len!=10) return BTH_ERR_LENGTH;
    const uint32_t number=(uint32_t)p.data[6] | ((uint32_t)p.data[7]<<8) |
                          ((uint32_t)p.data[8]<<16) | ((uint32_t)p.data[9]<<24);
    if (number>999999) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=classic_peer(l,p.data);
    if (!peer) return bth_cmdq_push(l->commands,0x042d,p.data,6,true);
    if (peer->confirmation_pending) return BTH_ERR_STATE;
    const bth_confirmation_t choice=l->callbacks.confirm?l->callbacks.confirm(l->context,p.data,number):BTH_CONFIRM_REJECT;
    peer->confirmation_pending=true;
    if (choice==BTH_CONFIRM_DEFER) return BTH_MORE;
    return bth_links_confirm(l,p.data,choice==BTH_CONFIRM_ACCEPT);
}
static bth_status_t key_notification(bth_links_t *l,bth_span_t p) {
    if (p.len!=23) return BTH_ERR_LENGTH;
    bth_peer_link_t *peer=classic_peer(l,p.data);
    if (!peer || peer->disconnecting) return BTH_IGNORED;
    bth_bond_t bond={0}; memcpy(bond.key,p.data+6,16); bond.key_type=p.data[22];
    bond.pin_len=bond.key_type==0?l->policy.pin_len:0;
    const bool changed=bond.key_type==6;
    if (changed) {
        bth_bond_t old={0};
        const bool loaded=l->callbacks.load && l->callbacks.load(l->context,p.data,&old) && valid_bond(&old);
        if (loaded) { bond.key_type=old.key_type; bond.pin_len=old.pin_len; }
        erase_bond(&old);
    }
    if (!valid_bond(&bond)) {
        erase_bond(&bond);
        return disconnect_security(l,peer,5);
    }
    if (changed && peer->authenticated && peer->bonded) {
        // A remote-initiated key change has no Authentication Complete event.
        // Preserve the established link facts and atomically replace its bond.
        const bool stored=l->callbacks.save && l->callbacks.save(l->context,peer->peer,&bond);
        erase_bond(&bond);
        if (!stored) {
            peer->bonded=false; (void)disconnect_security(l,peer,5); return BTH_ERR_STATE;
        }
        return BTH_OK;
    }
    const uint8_t stale=retire_size_reads(l,peer),old_keys=retire_key_replies(l,peer);
    clear_security(peer);peer->stale_size_reads=stale;peer->stale_key_replies=old_keys;peer->bonded=false;
    peer->pending_bond=bond; peer->key_pending=true; peer->key_type=bond.key_type;
    erase_bond(&bond);
    return BTH_OK;
}
static bth_status_t authentication_complete(bth_links_t *l,bth_span_t p) {
    if (p.len!=3) return BTH_ERR_LENGTH;
    const uint16_t handle=bth_read_le16(p.data+1);
    if (handle>0x0eff) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=&l->classic;
    if (peer->state!=BTH_PEER_CONNECTED || peer->handle!=handle) return BTH_IGNORED;
    if (peer->disconnecting) return BTH_IGNORED;
    if(p.data[0]==0x23 || p.data[0]==0x2a)return collision_retry(l,peer,0x0411);
    if (p.data[0]) {
        peer->bonded=false;
        bool removed=true;
        if ((p.data[0]==5 || p.data[0]==6) && l->callbacks.remove) removed=l->callbacks.remove(l->context,peer->peer);
        const bth_status_t result=disconnect_security(l,peer,5);
        return result!=BTH_OK?result:(removed?BTH_OK:BTH_ERR_STATE);
    }
    if (!peer->key_pending && !peer->bonded) {
        (void)disconnect_security(l,peer,5); return BTH_ERR_STATE;
    }
    if(peer->authenticated && peer->cached_key_accepted && peer->cached_key_encrypted &&
        peer->key_size_verified && peer->encryption && !peer->key_pending)return BTH_OK;
    if (!bth_cmdq_available(l->commands)) { clear_security(peer); return BTH_ERR_CAPACITY; }
    if (peer->key_pending) {
        bool stored=false;
        if (l->policy.auth_requirement&6u) {
            stored=l->callbacks.save && l->callbacks.save(l->context,peer->peer,&peer->pending_bond);
            if (!stored) {
                peer->bonded=false; (void)disconnect_security(l,peer,5); return BTH_ERR_STATE;
            }
        }
        peer->key_type=peer->pending_bond.key_type; peer->bonded=stored;
        erase_bond(&peer->pending_bond); peer->key_pending=false;
    }
    peer->retry_opcode=0;(void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x0411);
    peer->authenticated=true; peer->key_size_verified=false; peer->key_size=0;
    const uint8_t reply[]={(uint8_t)handle,(uint8_t)(handle>>8),1};
    return peer_command(l,peer,0x0413,reply,sizeof reply);
}
static bth_status_t encryption_change(bth_links_t *l,bth_span_t p) {
    if (p.len!=4) return BTH_ERR_LENGTH;
    const uint16_t handle=bth_read_le16(p.data+1);
    if (handle>0x0eff || p.data[3]>2) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=l->classic.state==BTH_PEER_CONNECTED && l->classic.handle==handle?&l->classic:
                          l->le.state==BTH_PEER_CONNECTED && l->le.handle==handle?&l->le:NULL;
    if (!peer) return BTH_IGNORED;
    if (peer->type==BTH_LINK_LE && p.data[3]>1) return BTH_ERR_PARAM;
    if(peer->type==BTH_LINK_CLASSIC && (p.data[0]==0x23 || p.data[0]==0x2a))return collision_retry(l,peer,0x0413);
    peer->key_size_verified=false; peer->key_size=0;
    if (p.data[0]) { peer->encryption=0; return disconnect_security(l,peer,5); }
    if(peer->type==BTH_LINK_CLASSIC) {
        peer->retry_opcode=0;peer->collision_retries=0;
        (void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x0413);
        if(p.data[3]) (void)bth_cmdq_cancel_owned(l->commands,peer->owner,0x0411);
    }
    peer->encryption=p.data[3];
    if (!peer->encryption || peer->type==BTH_LINK_LE) return BTH_OK;
    peer->cached_key_encrypted=cached_key_submitted(l,peer);
    const uint8_t reply[]={(uint8_t)handle,(uint8_t)(handle>>8)};
    return peer_command(l,peer,0x1408,reply,sizeof reply);
}
static bth_status_t security_command_complete(bth_links_t *l,const bth_h4_frame_t *e) {
    const bth_span_t p=e->event.params;
    if (p.len<3) return BTH_ERR_LENGTH;
    const uint16_t opcode=bth_read_le16(p.data+1);
    const bool address_reply=opcode==0x040b || opcode==0x040c || opcode==0x040d || opcode==0x040e ||
                             opcode==0x042b || opcode==0x0434 || opcode==0x042c || opcode==0x042d;
    if ((opcode==0x1408 && p.len!=7) || (address_reply && p.len!=10)) return BTH_ERR_LENGTH;
    // Validate current-peer correlation before consuming its transaction. Old
    // owner tombstones can be consumed even though their peer is now absent.
    for (unsigned n=0;n<l->commands->sent_count;++n) if (l->commands->sent[n].opcode==opcode) {
        if (l->classic.state==BTH_PEER_CONNECTED && l->commands->sent[n].owner==l->classic.owner) {
            if (opcode==0x1408 && bth_read_le16(p.data+4)!=l->classic.handle) return BTH_IGNORED;
            if (address_reply && memcmp(p.data+4,l->classic.peer,6)) return BTH_ERR_PARAM;
        }
        break;
    }
    uint32_t owner=0;
    const bth_status_t match=bth_cmdq_complete(l->commands,e,&owner);
    if (match!=BTH_OK) return match;
    bth_peer_link_t *peer=&l->classic;
    if (peer->state!=BTH_PEER_CONNECTED || peer->owner!=owner) return BTH_IGNORED;
    if (address_reply) {
        if (memcmp(peer->peer,p.data+4,6)) return BTH_ERR_PARAM;
        if(opcode==0x040b && peer->stale_key_replies){--peer->stale_key_replies;return BTH_IGNORED;}
        if(p.data[3])return disconnect_security(l,peer,5);
        if(opcode==0x040b && peer->cached_key_pending) {
            peer->cached_key_pending=false;peer->cached_key_accepted=true;cached_encryption_verified(l,peer);
        }
        return BTH_OK;
    }
    if (opcode!=0x1408) return BTH_UNSUPPORTED;
    const uint16_t handle=bth_read_le16(p.data+4);
    if (handle>0x0eff) return BTH_ERR_PARAM;
    if (peer->handle!=handle) return BTH_IGNORED;
    if(peer->stale_size_reads){--peer->stale_size_reads;return BTH_IGNORED;}
    peer->key_size_verified=false; peer->key_size=0;
    const bool sc=peer->key_type==7 || peer->key_type==8;
    if (p.data[3] || p.data[6]<l->policy.min_key_size || p.data[6]>16 ||
        (sc && (p.data[6]!=16 || peer->encryption!=2))) return disconnect_security(l,peer,5);
    if (!peer->encryption) return BTH_OK;
    peer->key_size=p.data[6]; peer->key_size_verified=true;
    cached_encryption_verified(l,peer);
    return BTH_OK;
}
static bth_status_t security_command_status(bth_links_t *l,const bth_h4_frame_t *e) {
    const bth_span_t p=e->event.params;
    if (p.len!=4) return BTH_ERR_LENGTH;
    const uint16_t opcode=bth_read_le16(p.data+2);
    uint32_t owner=0;
    const bth_status_t match=bth_cmdq_complete(l->commands,e,&owner);
    if (match!=BTH_OK) return match;
    if (l->classic.state==BTH_PEER_EMPTY || l->classic.owner!=owner) return BTH_IGNORED;
    if (opcode==0x0409) {
        if (p.data[0] && l->classic.state==BTH_PEER_PENDING) {
            (void)bth_cmdq_cancel_owner(l->commands,owner); clear_link(&l->classic);
        }
        return BTH_OK;
    }
    if (opcode==0x0406) return !p.data[0] || (l->classic.disconnecting && (p.data[0]==0x0c || p.data[0]==2))?BTH_OK:BTH_ERR_STATE;
    if (opcode!=0x0411 && opcode!=0x0413) return BTH_UNSUPPORTED;
    if (!p.data[0]) return BTH_OK;
    if (l->classic.state!=BTH_PEER_CONNECTED) return BTH_IGNORED;
    if(p.data[0]==0x23 || p.data[0]==0x2a)return collision_retry(l,&l->classic,opcode);
    if(opcode==0x0411 && p.data[0]==0x0c && l->classic.bonded &&
        (l->classic.cached_key_pending || l->classic.cached_key_accepted) && !l->classic.disconnecting)return BTH_OK;
    // On an authenticated incoming link, the master may already be enabling
    // encryption. A disallowed duplicate request grants no authorization:
    // wait for Encryption Change and its key-size check under the owner timer.
    if (opcode==0x0413 && p.data[0]==0x0c && l->classic.authenticated && !l->classic.disconnecting) return BTH_OK;
    return disconnect_security(l,&l->classic,5);
}
static bth_status_t key_refresh(bth_links_t *l,bth_span_t p) {
    if (p.len!=3) return BTH_ERR_LENGTH;
    const uint16_t handle=bth_read_le16(p.data+1);
    if (handle>0x0eff) return BTH_ERR_PARAM;
    bth_peer_link_t *peer=&l->classic;
    if (peer->state!=BTH_PEER_CONNECTED || peer->handle!=handle) return BTH_IGNORED;
    peer->key_size_verified=false; peer->key_size=0;
    if (p.data[0]) return disconnect_security(l,peer,5);
    peer->cached_key_encrypted=cached_key_submitted(l,peer);
    const uint8_t reply[]={(uint8_t)handle,(uint8_t)(handle>>8)};
    return peer_command(l,peer,0x1408,reply,sizeof reply);
}
static bth_status_t pairing_complete(bth_links_t *l,bth_span_t p) {
    if (p.len!=7) return BTH_ERR_LENGTH;
    bth_peer_link_t *peer=classic_peer(l,p.data+1);
    if (!peer) return BTH_IGNORED;
    if (p.data[0]) return disconnect_security(l,peer,5);
    // SSP completion alone is not authentication or an encryption-size check.
    return BTH_OK;
}
bth_status_t bth_links_on_event(bth_links_t *l,const bth_h4_frame_t *event) {
    if (!l || !l->initialized || !event) return BTH_ERR_PARAM;
    if (event->type!=BTH_H4_EVENT) return BTH_UNSUPPORTED;
    bth_span_t p=event->event.params;
    if (p.len && !p.data) return BTH_ERR_PARAM;
    switch (event->event.code) {
    case 3: return connected_classic(l,p);
    case 4: return connection_request(l,p);
    case 5: return disconnected(l,p);
    case 6: return authentication_complete(l,p);
    case 8: return encryption_change(l,p);
    case 0x0e: return security_command_complete(l,event);
    case 0x0f: return security_command_status(l,event);
    case 0x16: return pin_request(l,p);
    case 0x17: return key_request(l,p);
    case 0x18: return key_notification(l,p);
    case 0x30: return key_refresh(l,p);
    case 0x31: return io_request(l,p);
    case 0x33: return confirmation_request(l,p);
    case 0x36: return pairing_complete(l,p);
    case 0x3e: return connected_le(l,p);
    default: return BTH_UNSUPPORTED;
    }
}
