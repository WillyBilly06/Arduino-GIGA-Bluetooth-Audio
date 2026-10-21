#include "bth_att.h"
#include <string.h>
enum { INVALID_HANDLE=1,READ_DENIED=2,WRITE_DENIED=3,INVALID_PDU=4,AUTHENTICATION=5,UNSUPPORTED=6,
    INVALID_OFFSET=7,PREPARE_FULL=9,NOT_FOUND=10,KEY_SIZE=12,VALUE_LENGTH=13,ENCRYPTION=15,UNSUPPORTED_GROUP=16,VALUE_NOT_ALLOWED=19 };
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static bool short_uuid(bth_span_t uuid,uint16_t *value) {
    if (!uuid.data) return false;
    if (uuid.len==2) { *value=bth_read_le16(uuid.data); return true; }
    static const uint8_t base[]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0};
    if (uuid.len!=16 || memcmp(uuid.data,base,sizeof base) || uuid.data[14] || uuid.data[15]) return false;
    *value=bth_read_le16(uuid.data+12); return true;
}
static bool uuid_is(bth_span_t uuid,uint16_t value) { uint16_t actual; return short_uuid(uuid,&actual) && actual==value; }
static bool uuid_equal(bth_span_t a,bth_span_t b) {
    if (a.len==b.len) return !memcmp(a.data,b.data,a.len);
    uint16_t x,y; return short_uuid(a,&x) && short_uuid(b,&y) && x==y;
}
static const bth_att_attribute_t *find(const bth_att_t *s,uint16_t handle) {
    size_t low=0,high=s->count;
    while (low<high) { size_t middle=low+(high-low)/2; if (s->attributes[middle].handle<handle) low=middle+1; else high=middle; }
    return low<s->count && s->attributes[low].handle==handle?&s->attributes[low]:NULL;
}
static size_t maximum(const bth_att_attribute_t *a) { return a->kind==BTH_ATT_CCCD?2:a->maximum_length?a->maximum_length:a->value.len; }
static bth_span_t value(const bth_att_t *s,const bth_att_attribute_t *a,uint8_t cccd[2]) {
    if (a->kind!=BTH_ATT_CCCD) return a->value;
    put16(cccd,s->cccd[a->cccd_index]); return (bth_span_t){cccd,2};
}
static uint8_t security_error(const bth_att_attribute_t *a,bth_att_security_t sec) {
    if ((a->permissions&BTH_ATT_AUTHENTICATED) && (!sec.authenticated || !sec.encrypted)) return AUTHENTICATION;
    if ((a->permissions&BTH_ATT_ENCRYPTED) && !sec.encrypted) return ENCRYPTION;
    if ((a->permissions&(BTH_ATT_ENCRYPTED|BTH_ATT_AUTHENTICATED)) &&
        (sec.key_size<(a->minimum_key_size?a->minimum_key_size:7) || sec.key_size>16)) return KEY_SIZE;
    return 0;
}
static uint8_t read_error(const bth_att_attribute_t *a,bth_att_security_t sec) {
    if (!a) return INVALID_HANDLE;
    return a->permissions&BTH_ATT_READ?security_error(a,sec):READ_DENIED;
}
static uint8_t write_error(const bth_att_attribute_t *a,bth_att_security_t sec,bool command) {
    if (!a) return INVALID_HANDLE;
    return a->permissions&(command?BTH_ATT_WRITE_COMMAND:BTH_ATT_WRITE)?security_error(a,sec):WRITE_DENIED;
}
static bth_status_t error(uint8_t opcode,uint16_t handle,uint8_t reason,uint8_t *out,size_t cap,size_t *written) {
    if (opcode&0x40) return BTH_IGNORED;
    if (cap<5) return BTH_ERR_CAPACITY;
    out[0]=1; out[1]=opcode; put16(out+2,handle); out[4]=reason; *written=5; return BTH_OK;
}
static uint16_t group_end(const bth_att_t *s,size_t index) {
    const bth_att_attribute_t *a=&s->attributes[index];
    if (a->group_end) return a->group_end;
    if (uuid_is(a->uuid,0x2800) || uuid_is(a->uuid,0x2801)) {
        for (size_t i=index+1;i<s->count;++i) if (uuid_is(s->attributes[i].uuid,0x2800) || uuid_is(s->attributes[i].uuid,0x2801)) return (uint16_t)(s->attributes[i].handle-1);
        return s->attributes[s->count-1].handle;
    }
    if (uuid_is(a->uuid,0x2803)) {
        for (size_t i=index+1;i<s->count;++i) if (uuid_is(s->attributes[i].uuid,0x2800) || uuid_is(s->attributes[i].uuid,0x2801) || uuid_is(s->attributes[i].uuid,0x2803)) return (uint16_t)(s->attributes[i].handle-1);
        return s->attributes[s->count-1].handle;
    }
    return a->handle;
}
bool bth_att_init(bth_att_t *s,const bth_att_attribute_t *attributes,size_t count,uint16_t maximum_mtu) {
    if (!s) return false;
    memset(s,0,sizeof(*s));
    if (!attributes || !count || count>BTH_ATT_ATTRIBUTE_LIMIT || maximum_mtu<23 || maximum_mtu>517) return false;
    s->attributes=attributes; s->count=count; uint16_t previous=0; uint8_t cccd_indices=0;
    for (size_t i=0;i<count;++i) {
        const bth_att_attribute_t *a=&attributes[i];
        if (!a->handle || a->handle<=previous || !a->uuid.data || (a->uuid.len!=2 && a->uuid.len!=16) ||
            (a->value.len && !a->value.data) || a->kind>BTH_ATT_CCCD || (a->permissions&~31u) ||
            maximum(a)>512 || a->value.len>maximum(a) || (a->group_end && a->group_end<a->handle) ||
            (a->minimum_key_size && (a->minimum_key_size<7 || a->minimum_key_size>16))) return false;
        if (a->kind==BTH_ATT_CONSTANT && (a->permissions&(BTH_ATT_WRITE|BTH_ATT_WRITE_COMMAND))) return false;
        if ((uuid_is(a->uuid,0x2800) || uuid_is(a->uuid,0x2801)) && a->value.len!=2 && a->value.len!=16) return false;
        if (a->kind==BTH_ATT_CCCD) {
            if (!uuid_is(a->uuid,0x2902) || a->cccd_index>=8 || !a->cccd_mask || (a->cccd_mask&~3u) ||
                (cccd_indices&(1u<<a->cccd_index)) || !find(s,a->related_handle) || a->related_handle>=a->handle) return false;
            cccd_indices|=(uint8_t)(1u<<a->cccd_index);
        }
        previous=a->handle;
    }
    s->mtu=23; s->maximum_mtu=maximum_mtu; s->initialized=true; return true;
}
static bth_status_t discovery(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written) {
    const uint8_t opcode=in[0];
    const bool information=opcode==4,by_value=opcode==6,by_group=opcode==0x10;
    if ((information && length!=5) || (by_value && length<7) || (!information && !by_value && length!=7 && length!=21))
        return error(opcode,0,INVALID_PDU,out,cap,written);
    const uint16_t start=bth_read_le16(in+1),end=bth_read_le16(in+3);
    if (!start || end<start) return error(opcode,start,INVALID_HANDLE,out,cap,written);
    bth_span_t type=information?(bth_span_t){NULL,0}:(bth_span_t){in+5,by_value?2:length-5};
    if (by_group && !uuid_is(type,0x2800) && !uuid_is(type,0x2801)) return error(opcode,start,UNSUPPORTED_GROUP,out,cap,written);
    size_t used=information || (!by_value)?2:1,entry_size=0,full_length=0;
    bool any=false; uint8_t format=0;
    for (size_t i=0;i<s->count;++i) {
        const bth_att_attribute_t *a=&s->attributes[i];
        if (a->handle<start) continue;
        if (a->handle>end) break;
        uint8_t cccd[2]; bth_span_t v=value(s,a,cccd); size_t bytes=0;
        if (information) {
            if (any && a->uuid.len!=(format==1?2u:16u)) break;
            bytes=2+a->uuid.len;
        } else {
            if (!uuid_equal(a->uuid,type)) continue;
            if (by_value) {
                // Discovery by value exposes grouping attributes, not arbitrary
                // protected application values. Non-readable entries do not match.
                if (!(a->permissions&BTH_ATT_READ) || security_error(a,sec)) continue;
                const bth_span_t sought={in+7,length-7};
                const bool service=uuid_is(type,0x2800) || uuid_is(type,0x2801);
                if (service) { if (!uuid_equal(v,sought)) continue; }
                else if (sought.len!=v.len || (v.len && memcmp(v.data,sought.data,v.len))) continue;
                bytes=4;
            } else {
                uint8_t reason=read_error(a,sec);
                if (reason) { if (any) break; return error(opcode,a->handle,reason,out,cap,written); }
                if (by_group) {
                    if (v.len!=2 && v.len!=16) return error(opcode,a->handle,UNSUPPORTED_GROUP,out,cap,written);
                    bytes=4+v.len;
                } else {
                    size_t count=v.len;
                    if (count>s->mtu-4u) count=s->mtu-4u;
                    if (count>253) count=253;
                    bytes=2+count;
                }
                if (any && (bytes!=entry_size || (!by_group && v.len!=full_length))) break;
            }
        }
        if (used+bytes>s->mtu) break;
        if (used+bytes>cap) return BTH_ERR_CAPACITY;
        if (!any) { entry_size=bytes; full_length=v.len; format=a->uuid.len==2?1:2; }
        uint8_t *p=out+used; put16(p,a->handle);
        if (information) memcpy(p+2,a->uuid.data,a->uuid.len);
        else if (by_value || by_group) {
            const uint16_t last=group_end(s,i);
            put16(p+2,last); if (by_group) memcpy(p+4,v.data,v.len);
        } else if (bytes>2) memcpy(p+2,v.data,bytes-2);
        used+=bytes; any=true;
    }
    if (!any) return error(opcode,start,NOT_FOUND,out,cap,written);
    out[0]=(uint8_t)(opcode+1);
    if (!by_value) out[1]=information?format:(uint8_t)entry_size;
    *written=used; return BTH_OK;
}
static bth_status_t read_value(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written) {
    const bool blob=in[0]==0x0c;
    if (length!=(blob?5u:3u)) return error(in[0],0,INVALID_PDU,out,cap,written);
    const uint16_t handle=bth_read_le16(in+1),offset=blob?bth_read_le16(in+3):0;
    const bth_att_attribute_t *a=find(s,handle); uint8_t reason=read_error(a,sec);
    if (reason) return error(in[0],handle,reason,out,cap,written);
    uint8_t cccd[2]; bth_span_t v=value(s,a,cccd);
    if (offset>v.len) return error(in[0],handle,INVALID_OFFSET,out,cap,written);
    size_t bytes=v.len-offset; if (bytes>s->mtu-1u) bytes=s->mtu-1u;
    if (cap<bytes+1) return BTH_ERR_CAPACITY;
    out[0]=(uint8_t)(in[0]+1); if (bytes) memcpy(out+1,v.data+offset,bytes); *written=bytes+1; return BTH_OK;
}
static bth_status_t read_multiple(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written) {
    if (length<5 || !(length&1)) return error(in[0],0,INVALID_PDU,out,cap,written);
    // Validate all handles, including those beyond the truncated response.
    for (size_t i=1;i<length;i+=2) {
        const uint16_t handle=bth_read_le16(in+i); uint8_t reason=read_error(find(s,handle),sec);
        if (reason) return error(in[0],handle,reason,out,cap,written);
    }
    if (!cap) return BTH_ERR_CAPACITY;
    size_t used=1; const bool variable=in[0]==0x20;
    for (size_t i=1;i<length && used<s->mtu;i+=2) {
        const bth_att_attribute_t *a=find(s,bth_read_le16(in+i)); uint8_t cccd[2]; bth_span_t v=value(s,a,cccd);
        if (variable) {
            if (s->mtu-used<2) break;
            if (cap-used<2) return BTH_ERR_CAPACITY;
            put16(out+used,(uint16_t)v.len); used+=2;
        }
        size_t bytes=v.len; if (bytes>s->mtu-used) bytes=s->mtu-used;
        if (bytes>cap-used) return BTH_ERR_CAPACITY;
        if (bytes) memcpy(out+used,v.data,bytes);
        used+=bytes;
    }
    out[0]=(uint8_t)(in[0]+1); *written=used; return BTH_OK;
}
static void discard_prepared(bth_att_t *s) {
    s->prepared_active=false; s->prepared_handle=s->prepared_length=0;
    memset(s->prepared,0,sizeof s->prepared); memset(s->prepared_seen,0,sizeof s->prepared_seen);
}
static uint8_t write_value(bth_att_t *s,const bth_att_attribute_t *a,bth_span_t v,bool prepared,bth_att_action_t *action) {
    if (v.len>maximum(a)) return VALUE_LENGTH;
    action->handle=a->handle;
    if (a->kind==BTH_ATT_CCCD) {
        if (v.len!=2) return VALUE_LENGTH;
        uint16_t flags=bth_read_le16(v.data);
        if (flags&~a->cccd_mask) return VALUE_NOT_ALLOWED;
        s->cccd[a->cccd_index]=flags; action->kind=BTH_ATT_ACTION_CCCD; action->cccd_value=flags;
    } else if (a->kind==BTH_ATT_APPLICATION) {
        action->kind=BTH_ATT_ACTION_WRITE; action->value=v; action->prepared=prepared;
    } else return WRITE_DENIED;
    return 0;
}
static bth_status_t write_request(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written,bth_att_action_t *action) {
    const bool command=in[0]==0x52;
    if (length<3) return error(in[0],0,INVALID_PDU,out,cap,written);
    const uint16_t handle=bth_read_le16(in+1); const bth_att_attribute_t *a=find(s,handle);
    uint8_t reason=write_error(a,sec,command);
    if (reason) return error(in[0],handle,reason,out,cap,written);
    if (!command && !cap) return BTH_ERR_CAPACITY;
    reason=write_value(s,a,(bth_span_t){in+3,length-3},false,action);
    if (reason) { memset(action,0,sizeof(*action)); return error(in[0],handle,reason,out,cap,written); }
    if (!command) { out[0]=0x13; *written=1; }
    return BTH_OK;
}
static bth_status_t prepare_write(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written) {
    if (length<5) return error(in[0],0,INVALID_PDU,out,cap,written);
    const uint16_t handle=bth_read_le16(in+1),offset=bth_read_le16(in+3); const bth_att_attribute_t *a=find(s,handle);
    uint8_t reason=write_error(a,sec,false);
    if (reason) return error(in[0],handle,reason,out,cap,written);
    if (s->execute_pending || (s->prepared_active && s->prepared_handle!=handle)) return error(in[0],handle,PREPARE_FULL,out,cap,written);
    const size_t bytes=length-5;
    if (offset>maximum(a)) return error(in[0],handle,INVALID_OFFSET,out,cap,written);
    if (bytes>maximum(a)-offset) return error(in[0],handle,VALUE_LENGTH,out,cap,written);
    if (cap<length) return BTH_ERR_CAPACITY;
    if (!s->prepared_active) { discard_prepared(s); s->prepared_active=true; s->prepared_handle=handle; }
    if (bytes) memcpy(s->prepared+offset,in+5,bytes);
    for (size_t i=offset;i<(size_t)offset+bytes;++i) s->prepared_seen[i/8]|=(uint8_t)(1u<<(i&7));
    if (offset+bytes>s->prepared_length) s->prepared_length=(uint16_t)(offset+bytes);
    memcpy(out,in,length); out[0]=0x17; *written=length; return BTH_OK;
}
static bth_status_t execute_write(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t sec,
    uint8_t *out,size_t cap,size_t *written,bth_att_action_t *action) {
    if (length!=2 || in[1]>1) return error(in[0],0,INVALID_PDU,out,cap,written);
    if (s->execute_pending) return BTH_MORE;
    if (!cap) return BTH_ERR_CAPACITY;
    if (!in[1] || !s->prepared_active) { discard_prepared(s); out[0]=0x19; *written=1; return BTH_OK; }
    const uint16_t handle=s->prepared_handle; const bth_att_attribute_t *a=find(s,handle);
    uint8_t reason=write_error(a,sec,false);
    for (unsigned i=0;!reason && i<s->prepared_length;++i) if (!(s->prepared_seen[i/8]&(1u<<(i&7)))) reason=INVALID_OFFSET;
    if (!reason) reason=write_value(s,a,(bth_span_t){s->prepared,s->prepared_length},true,action);
    if (reason) {
        memset(action,0,sizeof(*action)); bth_status_t st=error(in[0],handle,reason,out,cap,written);
        if (st==BTH_OK) discard_prepared(s);
        return st;
    }
    if (action->kind==BTH_ATT_ACTION_WRITE) { s->execute_pending=true; s->prepared_active=false; }
    else discard_prepared(s);
    out[0]=0x19; *written=1; return BTH_OK;
}
bth_status_t bth_att_process(bth_att_t *s,const uint8_t *in,size_t length,bth_att_security_t security,
    uint8_t *out,size_t capacity,size_t *written,bth_att_action_t *action) {
    if (written) *written=0;
    if (action) memset(action,0,sizeof(*action));
    if (!s || !s->initialized || !in || !out || !written || !action) return BTH_ERR_PARAM;
    if (s->failed) return BTH_ERR_STATE;
    if (!length) return BTH_ERR_LENGTH;
    if (in[0]==0x1e) {
        if (length!=1 || !s->indication_handle) return BTH_IGNORED;
        action->kind=BTH_ATT_ACTION_CONFIRMED; action->handle=s->indication_handle; s->indication_handle=0; return BTH_OK;
    }
    if ((in[0]&1) && !(in[0]&0x40)) return BTH_IGNORED;
    if (length>s->mtu) return error(in[0],0,INVALID_PDU,out,capacity,written);
    bth_att_t next=*s; bth_att_action_t result={0}; bth_status_t st; size_t count=0;
    switch (in[0]) {
    case 2:
        if (length!=3) { st=error(2,0,INVALID_PDU,out,capacity,&count); break; }
        if (s->mtu_exchanged) { st=error(2,0,UNSUPPORTED,out,capacity,&count); break; }
        if (capacity<3) { st=BTH_ERR_CAPACITY; break; }
        next.mtu=bth_read_le16(in+1); if (next.mtu<23) next.mtu=23;
        if (next.mtu>s->maximum_mtu) next.mtu=s->maximum_mtu;
        next.mtu_exchanged=true; result.kind=BTH_ATT_ACTION_MTU;
        out[0]=3; put16(out+1,s->maximum_mtu); count=3; st=BTH_OK; break;
    case 4: case 6: case 8: case 0x10: st=discovery(&next,in,length,security,out,capacity,&count); break;
    case 0x0a: case 0x0c: st=read_value(&next,in,length,security,out,capacity,&count); break;
    case 0x0e: case 0x20: st=read_multiple(&next,in,length,security,out,capacity,&count); break;
    case 0x12: case 0x52: st=write_request(&next,in,length,security,out,capacity,&count,&result); break;
    case 0x16: st=prepare_write(&next,in,length,security,out,capacity,&count); break;
    case 0x18: st=execute_write(&next,in,length,security,out,capacity,&count,&result); break;
    default: st=error(in[0],0,UNSUPPORTED,out,capacity,&count); break;
    }
    if (st==BTH_OK) {
        *s=next; *written=count; *action=result;
        if (action->kind==BTH_ATT_ACTION_WRITE && action->prepared) action->value.data=s->prepared;
    }
    return st;
}
void bth_att_execute_release(bth_att_t *s) {
    if (s && s->initialized && s->execute_pending) { s->execute_pending=false; discard_prepared(s); }
}
bth_status_t bth_att_notify(bth_att_t *s,uint16_t value_handle,uint16_t cccd_handle,
    const uint8_t *value,size_t length,bool indication,bth_att_security_t security,
    uint8_t *out,size_t capacity,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || (!value && length) || !out || !written) return BTH_ERR_PARAM;
    if (s->failed) return BTH_ERR_STATE;
    const bth_att_attribute_t *a=find(s,value_handle),*cccd=find(s,cccd_handle);
    if (!a || !cccd || cccd->kind!=BTH_ATT_CCCD || cccd->related_handle!=value_handle) return BTH_ERR_PARAM;
    if (security_error(a,security)) return BTH_ERR_STATE;
    const uint16_t flag=indication?2:1;
    if (!(cccd->cccd_mask&flag) || !(s->cccd[cccd->cccd_index]&flag)) return BTH_IGNORED;
    if (indication && s->indication_handle) return BTH_MORE;
    if (length>maximum(a) || length>s->mtu-3u) return BTH_ERR_LENGTH;
    if (capacity<length+3) return BTH_ERR_CAPACITY;
    out[0]=indication?0x1d:0x1b; put16(out+1,value_handle); if (length) memcpy(out+3,value,length);
    *written=length+3; if (indication) s->indication_handle=value_handle; return BTH_OK;
}
void bth_att_expire_indication(bth_att_t *s) { if (s && s->initialized && s->indication_handle) s->failed=true; }
