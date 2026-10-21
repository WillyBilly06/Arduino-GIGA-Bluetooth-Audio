#include "bth_smp.h"
#include <string.h>
enum { SMP_TIMEOUT=30000 };
bool bth_smp_ack_sent(bth_smp_ack_t *a,const bth_acl_view_t *packet) {
    if(!a || !packet || !packet->payload.data || (packet->pb!=0 && packet->pb!=1 && packet->pb!=2))return false;
    size_t bytes=packet->payload.len;
    if(packet->pb!=1) {
        if(a->remaining || bytes<4)return false;
        a->remaining=bth_read_le16(packet->payload.data);a->smp_fragment=bth_read_le16(packet->payload.data+2)==6;bytes-=4;
    } else if(!a->remaining)return false;
    if(bytes>a->remaining)return false;
    a->remaining=(uint16_t)(a->remaining-bytes);++a->sent;
    if(!a->remaining && a->smp_fragment){a->smp_target=a->sent;a->marked=true;}
    return true;
}
bool bth_smp_ack_completed(bth_smp_ack_t *a,uint16_t packets) {
    if(!a || (uint32_t)packets>a->sent-a->completed)return false;
    a->completed+=packets;return true;
}
bool bth_smp_ack_ready(const bth_smp_ack_t *a) {return a && a->marked && (int32_t)(a->completed-a->smp_target)>=0;}
static const uint8_t zero[16]={0};
static bool active(const bth_smp_t *s) {return s->state>BTH_SMP_IDLE && s->state<BTH_SMP_COMPLETE;}
static void reverse(uint8_t *out,const uint8_t *in,size_t length) {for(size_t i=0;i<length;++i)out[i]=in[length-1-i];}
static bool nonzero(const uint8_t *p,size_t n) {uint8_t v=0;for(size_t i=0;i<n;++i)v|=p[i];return v!=0;}
static bool identity(bth_smp_address_t a) {return a.type==0 || (a.type==1 && (a.value[5]&0xc0)==0xc0);}
static bool valid_bond(const bth_le_bond_t *b) {
    return b && b->has_ltk && b->key_size>=7 && b->key_size<=16 && b->connection.type<=1 &&
        (!b->has_identity || identity(b->identity));
}
static void erase_pair(bth_smp_t *s) {
    bth_smp_secret_clear(s->local_public,sizeof s->local_public);bth_smp_secret_clear(s->remote_public,sizeof s->remote_public);
    bth_smp_secret_clear(s->local_random,sizeof s->local_random);bth_smp_secret_clear(s->remote_random,sizeof s->remote_random);
    bth_smp_secret_clear(s->remote_confirm,sizeof s->remote_confirm);bth_smp_secret_clear(s->dhkey,sizeof s->dhkey);
    bth_smp_secret_clear(s->check_a,sizeof s->check_a);bth_smp_secret_clear(s->check_b,sizeof s->check_b);
    bth_smp_secret_clear(s->session_key,sizeof s->session_key);s->ecc_requested=false;s->phase2_verified=false;
}
static void release_ecc(bth_smp_t *s) {if(s->config.ecc)(void)bth_smp_ecc_abandon(s->config.ecc,s->owner);}
static void queue(bth_smp_t *s,uint8_t opcode,const uint8_t *p,size_t n) {
    s->tx[0]=opcode;
    if(n)memcpy(s->tx+1,p,n);
    s->tx_length=(uint8_t)(n+1);
}
static bth_status_t fail(bth_smp_t *s,uint8_t error,uint32_t now,bool send) {
    release_ecc(s);erase_pair(s);bth_smp_secret_clear(&s->bond,sizeof s->bond);
    bth_smp_secret_clear(s->tx,sizeof s->tx);s->tx_length=0;s->error=error;s->state=BTH_SMP_FAILED;
    s->key_issued=s->verified=s->encrypted=s->bond_pending=s->timer_active=false;
    if(s->failures<6)++s->failures;
    s->cooldown_until=now+(1000u<<(s->failures-1));
    if(s->config.rate)*s->config.rate=(bth_smp_rate_t){s->cooldown_until,s->failures};
    if(send)queue(s,5,&error,1);
    return BTH_OK;
}
static bool expired(bth_smp_t *s,uint32_t now) {
    if(!s->timer_active || (int32_t)(now-s->deadline)<0)return false;
    (void)fail(s,8,now,false);s->state=BTH_SMP_TIMED_OUT;return true;
}
static void address_msb(uint8_t out[7],bth_smp_address_t a) {out[0]=a.type;reverse(out+1,a.value,6);}
static void mask_key(uint8_t key[16],uint8_t size) {memset(key+size,0,16-size);}
static bool random_bytes(bth_smp_t *s,uint8_t *out,size_t n) {return s->config.random(s->config.random_context,out,n);}
static bool legacy_confirm(bth_smp_t *s,const uint8_t random_le[16],uint8_t out_le[16]) {
    uint8_t r[16],req[7],rsp[7],ia[6],ra[6],result[16];
    reverse(r,random_le,16);reverse(req,s->request,7);reverse(rsp,s->response,7);
    reverse(ia,s->config.peer.value,6);reverse(ra,s->config.local.value,6);
    const bool ok=bth_smp_c1(s->config.crypto,zero,r,req,rsp,s->config.peer.type,s->config.local.type,ia,ra,result);
    if(ok)reverse(out_le,result,16);
    bth_smp_secret_clear(r,sizeof r);bth_smp_secret_clear(result,sizeof result);return ok;
}
static bool legacy_stk(bth_smp_t *s) {
    uint8_t r1[16],r2[16],out[16];reverse(r1,s->local_random,16);reverse(r2,s->remote_random,16);
    const bool ok=bth_smp_s1(s->config.crypto,zero,r1,r2,out);
    if(ok){reverse(s->session_key,out,16);mask_key(s->session_key,s->key_size);}
    bth_smp_secret_clear(r1,sizeof r1);bth_smp_secret_clear(r2,sizeof r2);bth_smp_secret_clear(out,sizeof out);return ok;
}
static bool sc_confirm(bth_smp_t *s,uint8_t out_le[16]) {
    uint8_t x[32],y[32],n[16],out[16];reverse(x,s->local_public,32);reverse(y,s->remote_public,32);reverse(n,s->local_random,16);
    const bool ok=bth_smp_f4(s->config.crypto,x,y,n,0,out);if(ok)reverse(out_le,out,16);
    bth_smp_secret_clear(n,sizeof n);bth_smp_secret_clear(out,sizeof out);return ok;
}
static bool sc_keys(bth_smp_t *s) {
    uint8_t na[16],nb[16],a1[7],a2[7],mac[16],key[16],ea[16],eb[16];
    const uint8_t ioa[]={s->request[3],s->request[2],s->request[1]},iob[]={s->response[3],s->response[2],s->response[1]};
    reverse(na,s->remote_random,16);reverse(nb,s->local_random,16);address_msb(a1,s->config.peer);address_msb(a2,s->config.local);
    const bool ok=bth_smp_f5(s->config.crypto,s->dhkey,na,nb,a1,a2,mac,key) &&
        bth_smp_f6(s->config.crypto,mac,na,nb,zero,ioa,a1,a2,ea) &&
        bth_smp_f6(s->config.crypto,mac,nb,na,zero,iob,a2,a1,eb);
    if(ok){reverse(s->session_key,key,16);mask_key(s->session_key,s->key_size);reverse(s->check_a,ea,16);reverse(s->check_b,eb,16);}
    bth_smp_secret_clear(na,sizeof na);bth_smp_secret_clear(nb,sizeof nb);bth_smp_secret_clear(mac,sizeof mac);
    bth_smp_secret_clear(key,sizeof key);bth_smp_secret_clear(ea,sizeof ea);bth_smp_secret_clear(eb,sizeof eb);
    bth_smp_secret_clear(s->dhkey,sizeof s->dhkey);return ok;
}
static bool debug_key(const uint8_t p[64]) {
    static const uint8_t debug_le[64]={
        0xe6,0x9d,0x35,0x0e,0x48,0x01,0x03,0xcc,0xdb,0xfd,0xf4,0xac,0x11,0x91,0xf4,0xef,
        0xb9,0xa5,0xf9,0xe9,0xa7,0x83,0x2c,0x5e,0x2c,0xbe,0x97,0xf2,0xd2,0x03,0xb0,0x20,
        0x8b,0xd2,0x89,0x15,0xd0,0x8e,0x1c,0x74,0x24,0x30,0xed,0x8f,0xc2,0x45,0x63,0x76,
        0x5c,0x15,0x52,0x5a,0xbf,0x9a,0x32,0x63,0x6d,0xeb,0x2a,0x65,0x49,0x9c,0x80,0xdc};
    return bth_smp_secure_equal(p,debug_le,64);
}
bool bth_smp_init(bth_smp_t *s,uint32_t owner,const bth_smp_config_t *config,const bth_le_bond_t *bond) {
    if(!s)return false;
    bth_smp_secret_clear(s,sizeof *s);
    if(!owner || !config || !config->crypto || !config->crypto->aes || !config->crypto->cmac || !config->random ||
        config->minimum_key_size<7 || config->minimum_key_size>16 || !identity(config->local) || config->peer.type>1 ||
        (config->secure_connections && (!config->ecc || !config->ecc->initialized)) || (bond && !valid_bond(bond)))return false;
    s->config=*config;s->owner=owner;s->initialized=true;if(bond)s->previous_bond=*bond;
    if(config->rate){s->cooldown_until=config->rate->cooldown_until;s->failures=config->rate->failures;}
    return true;
}
void bth_smp_detach(bth_smp_t *s) {if(s){if(s->initialized)release_ecc(s);bth_smp_secret_clear(s,sizeof *s);}}
static void complete(bth_smp_t *s) {
    if(s->state!=BTH_SMP_KEYS || !s->own_keys_acked || s->peer_keys)return;
    s->state=BTH_SMP_COMPLETE;s->timer_active=false;s->failures=0;s->cooldown_until=0;
    if(s->config.rate)*s->config.rate=(bth_smp_rate_t){0};
    s->bond_pending=s->bonding && s->bond.has_ltk;
    if(s->bond_pending)s->previous_bond=s->bond;
    release_ecc(s);erase_pair(s);
}
static bth_status_t request(bth_smp_t *s,const uint8_t p[7],uint32_t now) {
    if(active(s))return fail(s,8,now,true);
    if(s->failures && (int32_t)(now-s->cooldown_until)<0) {const uint8_t reason=9;queue(s,5,&reason,1);return BTH_OK;}
    if(p[1]>4 || p[2]>1 || (p[3]&3)>1 || p[4]<7 || p[4]>16)return fail(s,10,now,true);
    if(p[4]<s->config.minimum_key_size)return fail(s,6,now,true);
    const bool sc=(p[3]&8)!=0 && s->config.secure_connections;
    // No OOB channel is exposed by this speaker. NoInputNoOutput chooses JW;
    // its result never carries MITM/authenticated permissions.
    if(sc && p[2])return fail(s,3,now,true);
    if(valid_bond(&s->previous_bond) && ((s->previous_bond.secure_connections && !sc) || p[4]<s->previous_bond.key_size))return fail(s,3,now,true);
    release_ecc(s);erase_pair(s);memset(&s->bond,0,sizeof s->bond);s->bond_pending=false;
    memcpy(s->request,p,7);s->secure_connections=sc;s->bonding=(p[3]&1)!=0;s->key_size=p[4];
    s->own_keys=s->bonding?(uint8_t)(p[6]&(sc?2:3)):0;s->peer_keys=s->bonding?(uint8_t)(p[5]&2):0;
    const uint8_t response[]={2,3,0,(uint8_t)((s->bonding?1:0)|(s->config.secure_connections?8:0)),16,s->peer_keys,s->own_keys};
    memcpy(s->response,response,7);s->error=0;s->key_issued=s->phase2_verified=s->verified=false;
    s->distribution_step=s->peer_step=0;s->own_keys_acked=false;
    if(!random_bytes(s,s->local_random,16))return fail(s,8,now,true);
    s->state=sc?BTH_SMP_PUBLIC:BTH_SMP_CONFIRM;s->timer_active=true;s->deadline=now+SMP_TIMEOUT;
    queue(s,2,response+1,6);return BTH_OK;
}
bth_status_t bth_smp_receive(bth_smp_t *s,const uint8_t *p,size_t length,uint32_t now) {
    if(!s || !s->initialized || (length && !p))return BTH_ERR_PARAM;
    if(expired(s,now) || s->state==BTH_SMP_TIMED_OUT)return BTH_IGNORED;
    if(s->tx_length)return BTH_MORE;
    if(!length)return fail(s,10,now,true);
    const uint8_t opcode=p[0];
    static const uint8_t sizes[]={0,7,7,17,17,2,17,11,17,8,17,2,65,17,2};
    if(opcode>=sizeof sizes || !opcode)return fail(s,7,now,true);
    if(length!=sizes[opcode])return fail(s,10,now,true);
    if(opcode==5)return fail(s,p[1],now,false);
    if(opcode==1)return request(s,p,now);
    // Keypress is irrelevant to JustWorks and never extends its timer.
    if(opcode==14)return BTH_IGNORED;
    if(s->state==BTH_SMP_CONFIRM && opcode==3) {
        uint8_t confirm[16];memcpy(s->remote_confirm,p+1,16);
        if(!legacy_confirm(s,s->local_random,confirm))return fail(s,8,now,true);
        queue(s,3,confirm,16);bth_smp_secret_clear(confirm,sizeof confirm);s->state=BTH_SMP_RANDOM;return BTH_OK;
    }
    if(s->state==BTH_SMP_RANDOM && opcode==4) {
        uint8_t confirm[16];memcpy(s->remote_random,p+1,16);
        const bool correct=legacy_confirm(s,s->remote_random,confirm) && bth_smp_secure_equal(confirm,s->remote_confirm,16);
        bth_smp_secret_clear(confirm,sizeof confirm);if(!correct)return fail(s,4,now,true);
        if(!legacy_stk(s))return fail(s,8,now,true);
        queue(s,4,s->local_random,16);s->state=BTH_SMP_ENCRYPTION;s->phase2_verified=true;return BTH_OK;
    }
    if(s->state==BTH_SMP_PUBLIC && opcode==12) {
        if(debug_key(p+1))return fail(s,10,now,true);
        memcpy(s->remote_public,p+1,64);s->state=BTH_SMP_LOCAL_PUBLIC;return BTH_OK;
    }
    if(s->state==BTH_SMP_SC_RANDOM && opcode==4) {
        memcpy(s->remote_random,p+1,16);if(!sc_keys(s))return fail(s,8,now,true);
        queue(s,4,s->local_random,16);s->state=BTH_SMP_CHECK;return BTH_OK;
    }
    if(s->state==BTH_SMP_CHECK && opcode==13) {
        if(!bth_smp_secure_equal(s->check_a,p+1,16))return fail(s,11,now,true);
        queue(s,13,s->check_b,16);s->state=BTH_SMP_ENCRYPTION;s->phase2_verified=true;return BTH_OK;
    }
    if(s->state==BTH_SMP_KEYS && s->encrypted && (s->peer_keys&2)) {
        if(s->peer_step==0 && opcode==8) {memcpy(s->bond.irk,p+1,16);s->peer_step=1;return BTH_OK;}
        if(s->peer_step==1 && opcode==9) {
            bth_smp_address_t a={.type=p[1]};memcpy(a.value,p+2,6);if(!identity(a))return fail(s,10,now,true);
            s->bond.identity=a;s->bond.has_identity=true;s->bond.identity_verified=false;
            s->bond.has_irk=nonzero(s->bond.irk,16);s->peer_keys=0;complete(s);return BTH_OK;
        }
    }
    return fail(s,8,now,true);
}
static bth_status_t ecc_step(bth_smp_t *s,uint32_t now) {
    const bool pub=s->state==BTH_SMP_LOCAL_PUBLIC;
    if(!s->ecc_requested) {
        const bth_status_t st=pub?bth_smp_ecc_read_public(s->config.ecc,s->owner,now):
            bth_smp_ecc_generate(s->config.ecc,s->owner,s->remote_public,now);
        if(st==BTH_MORE)return st;
        if(st!=BTH_OK)return fail(s,8,now,true);
        s->ecc_requested=true;return BTH_OK;
    }
    bth_smp_ecc_result_t result;const bth_status_t st=bth_smp_ecc_take(s->config.ecc,s->owner,&result);
    if(st!=BTH_OK)return st<0?fail(s,8,now,true):BTH_MORE;
    bool ok=!result.status && result.kind==(pub?BTH_SMP_ECC_PUBLIC:BTH_SMP_ECC_DHKEY) && result.length==(pub?64:32);
    if(ok && pub) {
        memcpy(s->local_public,result.data,64);
        ok=!debug_key(s->local_public) && !bth_smp_secure_equal(s->local_public,s->remote_public,32);
        if(ok){queue(s,12,s->local_public,64);s->state=BTH_SMP_DHKEY;s->ecc_requested=false;}
    } else if(ok) {
        uint8_t confirm[16];reverse(s->dhkey,result.data,32);ok=sc_confirm(s,confirm);
        if(ok){queue(s,3,confirm,16);s->state=BTH_SMP_SC_RANDOM;}
        bth_smp_secret_clear(confirm,sizeof confirm);
    }
    bth_smp_secret_clear(&result,sizeof result);return ok?BTH_OK:fail(s,pub?10:11,now,true);
}
bth_status_t bth_smp_advance(bth_smp_t *s,uint32_t now) {
    if(!s || !s->initialized)return BTH_ERR_PARAM;
    if(expired(s,now) || s->state==BTH_SMP_TIMED_OUT)return BTH_IGNORED;
    if(s->tx_length)return BTH_MORE;
    if(s->state==BTH_SMP_LOCAL_PUBLIC || s->state==BTH_SMP_DHKEY)return ecc_step(s,now);
    if(s->state!=BTH_SMP_DISTRIBUTE)return BTH_IGNORED;
    while(s->distribution_step<4) {
        const unsigned step=s->distribution_step++;
        if(step<2 && !(s->own_keys&1))continue;
        if(step>=2 && !(s->own_keys&2))continue;
        if(step==0)queue(s,6,s->bond.ltk,16);
        else if(step==1){uint8_t id[10]={(uint8_t)s->bond.ediv,(uint8_t)(s->bond.ediv>>8)};memcpy(id+2,s->bond.rand,8);queue(s,7,id,10);}
        else if(step==2)queue(s,8,s->config.local_irk,16);
        else{uint8_t a[7]={s->config.local.type};memcpy(a+1,s->config.local.value,6);queue(s,9,a,7);}
        return BTH_OK;
    }
    s->state=BTH_SMP_KEYS;if(!s->own_keys)s->own_keys_acked=true;complete(s);return BTH_OK;
}
bth_status_t bth_smp_take_tx(bth_smp_t *s,uint8_t *out,size_t capacity,size_t *length,uint32_t now) {
    if(length)*length=0;
    if(!s || !s->initialized || !out || !length)return BTH_ERR_PARAM;
    if(expired(s,now) || s->state==BTH_SMP_TIMED_OUT)return BTH_IGNORED;
    if(!s->tx_length)return BTH_MORE;
    if(capacity<s->tx_length)return BTH_ERR_CAPACITY;
    *length=s->tx_length;memcpy(out,s->tx,s->tx_length);bth_smp_secret_clear(s->tx,sizeof s->tx);s->tx_length=0;
    if(active(s)){s->timer_active=true;s->deadline=now+SMP_TIMEOUT;}return BTH_OK;
}
void bth_smp_transport_drained(bth_smp_t *s) {
    if(s && s->initialized && s->state==BTH_SMP_KEYS && !s->tx_length){s->own_keys_acked=true;complete(s);}
}
bool bth_smp_ltk_request(bth_smp_t *s,uint16_t ediv,const uint8_t rand[8],uint8_t out[16],uint32_t now) {
    if(!out)return false;
    memset(out,0,16);
    if(!s || !s->initialized || !rand || s->key_issued || s->state==BTH_SMP_TIMED_OUT || s->state==BTH_SMP_FAILED)return false;
    if(expired(s,now))return false;
    if(s->state==BTH_SMP_ENCRYPTION && s->phase2_verified && !s->tx_length && !ediv && !nonzero(rand,8))memcpy(out,s->session_key,16);
    else if((s->state==BTH_SMP_IDLE || s->state==BTH_SMP_COMPLETE) && valid_bond(&s->previous_bond) &&
        ediv==s->previous_bond.ediv && bth_smp_secure_equal(rand,s->previous_bond.rand,8)) {
        memcpy(out,s->previous_bond.ltk,16);s->key_size=s->previous_bond.key_size;s->secure_connections=s->previous_bond.secure_connections;
    } else return false;
    s->key_issued=true;return true;
}
void bth_smp_encryption(bth_smp_t *s,bool enabled,uint8_t status,uint32_t now) {
    if(!s || !s->initialized)return;
    if(expired(s,now) || s->state==BTH_SMP_TIMED_OUT)return;
    if(status || !enabled){s->verified=s->encrypted=s->key_issued=false;if(active(s))(void)fail(s,8,now,false);return;}
    if(!s->key_issued)return;
    s->key_issued=false;s->encrypted=s->verified=true;
    if(s->state==BTH_SMP_IDLE || s->state==BTH_SMP_COMPLETE) {
        s->bond=s->previous_bond;s->state=BTH_SMP_COMPLETE;s->bonding=true;
        // Resolving an RPA proves the distributed IRK/LTK, not ownership of an
        // arbitrary public identity claimed during pairing. Promote only after
        // reconnecting at that exact identity address using the validated LTK.
        if(s->bond.has_identity && !s->bond.identity_verified && s->config.peer.type==s->bond.identity.type &&
            !memcmp(s->config.peer.value,s->bond.identity.value,6)) {
            s->bond.identity_verified=true;s->previous_bond=s->bond;s->bond_pending=true;
        }
        return;
    }
    if(s->state!=BTH_SMP_ENCRYPTION || !s->phase2_verified){s->verified=s->encrypted=false;return;}
    s->bond.connection=s->config.peer;s->bond.key_size=s->key_size;s->bond.secure_connections=s->secure_connections;
    if(s->bonding && s->secure_connections){memcpy(s->bond.ltk,s->session_key,16);s->bond.has_ltk=true;}
    if(s->bonding && !s->secure_connections && (s->own_keys&1)) {
        uint8_t id[10];const bool ok=random_bytes(s,s->bond.ltk,16) && random_bytes(s,id,10) && nonzero(id,10);
        if(ok){mask_key(s->bond.ltk,s->key_size);s->bond.ediv=bth_read_le16(id);memcpy(s->bond.rand,id+2,8);s->bond.has_ltk=true;}
        bth_smp_secret_clear(id,sizeof id);if(!ok){(void)fail(s,8,now,true);return;}
    }
    s->state=s->own_keys?BTH_SMP_DISTRIBUTE:BTH_SMP_KEYS;
    if(!s->own_keys){s->own_keys_acked=true;complete(s);}
}
bool bth_smp_take_bond(bth_smp_t *s,bth_le_bond_t *out) {
    if(!s || !s->initialized || !out || !s->bond_pending || !s->verified || s->state!=BTH_SMP_COMPLETE)return false;
    *out=s->bond;s->bond_pending=false;return true;
}
bool bth_smp_bond_matches(const bth_smp_crypto_t *crypto,const bth_le_bond_t *bond,bth_smp_address_t address) {
    if(!valid_bond(bond) || address.type>1)return false;
    if(address.type==bond->connection.type && !memcmp(address.value,bond->connection.value,6))return true;
    if(bond->has_identity && address.type==bond->identity.type && !memcmp(address.value,bond->identity.value,6))return true;
    if(address.type!=1 || (address.value[5]&0xc0)!=0x40 || !bond->has_irk)return false;
    uint8_t irk[16],prand[3],hash[3],expected[3];reverse(irk,bond->irk,16);reverse(prand,address.value+3,3);reverse(expected,address.value,3);
    const bool ok=bth_smp_ah(crypto,irk,prand,hash) && bth_smp_secure_equal(hash,expected,3);
    bth_smp_secret_clear(irk,sizeof irk);bth_smp_secret_clear(hash,sizeof hash);return ok;
}
