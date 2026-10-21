#include "bth_sdp.h"
#include <string.h>

typedef struct { uint8_t type; bth_span_t value; size_t total; } element_t;
typedef struct { uint8_t *out; size_t offset, limit, position, copied; } stream_t;
static uint16_t read16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8)|p[1]; }
static uint32_t read32(const uint8_t *p) { return ((uint32_t)read16(p)<<16)|read16(p+2); }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void put32(uint8_t *p,uint32_t v) { put16(p,(uint16_t)(v>>16)); put16(p+2,(uint16_t)v); }
static uint32_t hash_bytes(uint32_t h,bth_span_t p) {
    for (size_t n=0;n<p.len;++n) { h^=p.data[n]; h*=16777619u; } return h;
}
static bool element(bth_span_t p,element_t *e) {
    if (!p.len || !p.data) return false;
    const unsigned type=p.data[0]>>3, size=p.data[0]&7;
    size_t header=1, count=0;
    if (type>8 || (type==0 && size!=0)) return false;
    if (type==0) count=0;
    else if (size<=4) count=(size_t)1u<<size;
    else {
        header+=size==5?1:(size==6?2:4);
        if (p.len<header) return false;
        count=size==5?p.data[1]:(size==6?read16(p.data+1):read32(p.data+1));
    }
    if (count>p.len-header) return false;
    if (((type==1 || type==2) && size>4) || (type==3 && (size>4 || (count!=2 && count!=4 && count!=16))) ||
        (type==5 && (size!=0 || count!=1)) || ((type==4 || type==6 || type==7 || type==8) && size<5)) return false;
    e->type=(uint8_t)type; e->value=(bth_span_t){p.data+header,count}; e->total=header+count; return true;
}
static bool tree(bth_span_t p,unsigned depth) {
    if (depth>8) return false;
    while (p.len) {
        element_t e; if (!element(p,&e)) return false;
        if ((e.type==6 || e.type==7) && !tree(e.value,depth+1)) return false;
        p.data+=e.total; p.len-=e.total;
    }
    return true;
}
static bool sequence(bth_span_t p,element_t *e) { return element(p,e) && e->type==6; }
static bool record_attributes(const bth_sdp_record_t *r,bth_span_t *p) {
    *p=r->attributes;
    if (p->data && p->len && (p->data[0]>>3)==6) {
        element_t root;
        if (!sequence(*p,&root) || root.total!=p->len) return false;
        *p=root.value;
    }
    return true;
}
bool bth_sdp_init_empty(bth_sdp_t *s,uint32_t token) {
    if (!s) return false;
    memset(s,0,sizeof *s);s->fingerprint=2166136261u^token;s->initialized=true;return true;
}
bool bth_sdp_init(bth_sdp_t *s,const bth_sdp_record_t *records,size_t count,uint32_t token) {
    if (!s) return false;
    memset(s,0,sizeof *s);
    if (!records || !count || count>8) return false;
    uint32_t hash=2166136261u^token;
    for (size_t n=0;n<count;++n) {
        bth_span_t p;
        if (!record_attributes(&records[n],&p)) return false;
        if (!p.data || !p.len || p.len>65532 || !records[n].handle) return false;
        for (size_t j=0;j<n;++j) if (records[j].handle==records[n].handle) return false;
        uint32_t previous=0; bool first=true, handle_found=false;
        hash=hash_bytes(hash,p);
        while (p.len) {
            if (p.len<3 || p.data[0]!=9) return false;
            const uint16_t id=read16(p.data+1);
            if (!first && id<=previous) return false;
            first=false; previous=id;
            element_t value;
            if (!element((bth_span_t){p.data+3,p.len-3},&value) || !tree((bth_span_t){p.data+3,value.total},0)) return false;
            if (!id) {
                if (value.type!=1 || value.value.len!=4 || read32(value.value.data)!=records[n].handle) return false;
                handle_found=true;
            }
            p.data+=3+value.total; p.len-=3+value.total;
        }
        if (!handle_found) return false;
    }
    s->records=records; s->count=count; s->fingerprint=hash; s->initialized=true; return true;
}
static void uuid(const element_t *e,uint8_t value[16]) {
    static const uint8_t base[]={0,0,0,0,0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    if (e->value.len==16) memcpy(value,e->value.data,16);
    else { memcpy(value,base,16); memcpy(value+4-e->value.len,e->value.data,e->value.len); }
}
static bool contains_uuid(bth_span_t p,const uint8_t value[16]) {
    while (p.len) {
        element_t e; if (!element(p,&e)) return false; // Database was validated at init.
        if (e.type==3) { uint8_t candidate[16]; uuid(&e,candidate); if (!memcmp(candidate,value,16)) return true; }
        if ((e.type==6 || e.type==7) && contains_uuid(e.value,value)) return true;
        p.data+=e.total; p.len-=e.total;
    }
    return false;
}
static bool valid_pattern(bth_span_t p) {
    unsigned count=0;
    while (p.len) {
        element_t e; if (!element(p,&e) || e.type!=3 || ++count>12) return false;
        p.data+=e.total; p.len-=e.total;
    }
    return count!=0;
}
static bool matches(const bth_sdp_record_t *r,bth_span_t pattern) {
    while (pattern.len) {
        element_t e; if (!element(pattern,&e)) return false;
        uint8_t value[16]; uuid(&e,value);
        if (!contains_uuid(r->attributes,value)) return false;
        pattern.data+=e.total; pattern.len-=e.total;
    }
    return true;
}
static bool valid_ranges(bth_span_t ranges) {
    uint32_t previous=0; bool first=true;
    while (ranges.len) {
        element_t e; if (!element(ranges,&e) || e.type!=1 || (e.value.len!=2 && e.value.len!=4)) return false;
        const uint16_t low=read16(e.value.data), high=e.value.len==2?low:read16(e.value.data+2);
        if (low>high || (!first && low<=previous)) return false;
        first=false; previous=high; ranges.data+=e.total; ranges.len-=e.total;
    }
    return !first;
}
static bool selected(uint16_t id,bth_span_t ranges) {
    while (ranges.len) {
        element_t e; if (!element(ranges,&e)) return false;
        const uint16_t low=read16(e.value.data), high=e.value.len==2?low:read16(e.value.data+2);
        if (id>=low && id<=high) return true;
        ranges.data+=e.total; ranges.len-=e.total;
    }
    return false;
}
/* The request ranges were validated above and the immutable record at init.
 * A universal range can borrow/copy its complete attribute span directly.
 * This also avoids rescanning every attribute on each continuation page. */
static bool all_attributes(bth_span_t ranges) {
    return ranges.len==5 && ranges.data[0]==10 && read16(ranges.data+1)==0 && read16(ranges.data+3)==0xffff;
}
static size_t attribute_size(const bth_sdp_record_t *r,bth_span_t ranges) {
    size_t size=0; bth_span_t p;if (!record_attributes(r,&p)) return 0;
    if (all_attributes(ranges)) return p.len;
    while (p.len) {
        element_t e; if (!element((bth_span_t){p.data+3,p.len-3},&e)) return 0;
        if (selected(read16(p.data+1),ranges)) size+=3+e.total;
        p.data+=3+e.total; p.len-=3+e.total;
    }
    return size;
}
static void emit(stream_t *s,const uint8_t *data,size_t n) {
    const size_t begin=s->position<s->offset?s->offset-s->position:0;
    if (begin<n && s->copied<s->limit) {
        size_t count=n-begin; if (count>s->limit-s->copied) count=s->limit-s->copied;
        memcpy(s->out+s->copied,data+begin,count); s->copied+=count;
    }
    s->position+=n;
}
static size_t sequence_size(size_t n) { return n+(n<256?2:3); }
static void emit_sequence(stream_t *s,size_t n) {
    uint8_t header[3]; header[0]=n<256?0x35:0x36;
    if (n<256) header[1]=(uint8_t)n; else put16(header+1,(uint16_t)n);
    emit(s,header,n<256?2:3);
}
static void emit_attributes(stream_t *s,const bth_sdp_record_t *r,bth_span_t ranges) {
    bth_span_t p;if (!record_attributes(r,&p)) return;
    if (all_attributes(ranges)) { emit(s,p.data,p.len); return; }
    while (p.len) {
        element_t e; if (!element((bth_span_t){p.data+3,p.len-3},&e)) return;
        if (selected(read16(p.data+1),ranges)) emit(s,p.data,3+e.total);
        p.data+=3+e.total; p.len-=3+e.total;
    }
}
static bth_status_t error_response(uint16_t transaction,uint16_t error,uint8_t *out,size_t cap,size_t *written) {
    if (cap<7) return BTH_ERR_CAPACITY;
    out[0]=1; put16(out+1,transaction); put16(out+3,2); put16(out+5,error); *written=7; return BTH_OK;
}
static bool continuation(bth_span_t p,uint32_t fingerprint,const bth_sdp_cursor_t *cursor,size_t *offset) {
    *offset=0;
    if (p.len==1 && p.data[0]==0) return true;
    if (cursor) {
        if (!cursor->valid || cursor->fingerprint!=fingerprint || p.len!=3 || p.data[0]!=2) return false;
        *offset=read16(p.data+1);return *offset!=0;
    }
    if (p.len!=9 || p.data[0]!=8 || read32(p.data+1)!=fingerprint) return false;
    *offset=read32(p.data+5); return *offset!=0;
}
static void finish(uint8_t *out,uint8_t code,uint16_t tid,size_t data_end,size_t next,uint32_t fingerprint,bth_sdp_cursor_t *cursor,size_t *written) {
    out[0]=code; put16(out+1,tid);
    out[data_end]=next?(cursor?2:8):0;
    if (next) {
        if (cursor) put16(out+data_end+1,(uint16_t)next);
        else { put32(out+data_end+1,fingerprint); put32(out+data_end+5,(uint32_t)next); }
    }
    *written=data_end+(next?(cursor?3:9):1); put16(out+3,(uint16_t)(*written-5));
    if (cursor) { cursor->fingerprint=fingerprint;cursor->valid=true; }
}
static uint16_t sequence_failure(bth_span_t p) {
    if (!p.len) return 4;
    unsigned size=p.data[0]&7;
    if ((p.data[0]>>3)!=6 || size<5) return 3;
    size_t header=size==5?2u:(size==6?3u:5u);
    if (p.len<header) return 4;
    size_t body=size==5?p.data[1]:(size==6?read16(p.data+1):read32(p.data+1));
    return body>p.len-header?4:3;
}
static bth_status_t process(const bth_sdp_t *s,bth_sdp_cursor_t *cursor,const uint8_t *in,size_t len,uint8_t *out,size_t cap,size_t *written) {
    if (written) *written=0;
    if (!s || !s->initialized || !in || !out || !written) return BTH_ERR_PARAM;
    if (len<5) return BTH_ERR_LENGTH;
    const uint16_t tid=read16(in+1);
    if (cap<7) return BTH_ERR_CAPACITY;
    if ((uintptr_t)in<=(uintptr_t)out?(uintptr_t)out-(uintptr_t)in<len:(uintptr_t)in-(uintptr_t)out<cap) return BTH_ERR_PARAM;
    if (len-5!=read16(in+3)) return error_response(tid,4,out,cap,written);
    if (in[0]!=2 && in[0]!=4 && in[0]!=6) return error_response(tid,3,out,cap,written);
    const bool search=in[0]==2, combined=in[0]==6;
    bth_span_t p={in+5,len-5}, pattern={NULL,0}, ranges={NULL,0};
    uint32_t fingerprint=s->fingerprint^in[0];
    const bth_sdp_record_t *single=NULL;
    element_t e;
    if (search || combined) {
        if (!sequence(p,&e)) return error_response(tid,cursor?sequence_failure(p):3,out,cap,written);
        if (!valid_pattern(e.value)) return error_response(tid,3,out,cap,written);
        fingerprint=hash_bytes(fingerprint,(bth_span_t){p.data,e.total}); pattern=e.value;
        p.data+=e.total; p.len-=e.total;
    } else {
        if (p.len<4) return error_response(tid,3,out,cap,written);
        const uint32_t handle=read32(p.data);
        fingerprint=hash_bytes(fingerprint,(bth_span_t){p.data,4});
        for (size_t n=0;n<s->count;++n) if (s->records[n].handle==handle) single=&s->records[n];
        if (!single) return error_response(tid,2,out,cap,written);
        p.data+=4; p.len-=4;
    }
    if (p.len<2) return error_response(tid,3,out,cap,written);
    const size_t maximum=read16(p.data);
    if (maximum<(search?1u:7u)) return error_response(tid,3,out,cap,written);
    if (search) fingerprint=hash_bytes(fingerprint,(bth_span_t){p.data,2});
    p.data+=2; p.len-=2;
    if (!search) {
        if (!sequence(p,&e)) return error_response(tid,cursor?sequence_failure(p):3,out,cap,written);
        if (!valid_ranges(e.value)) return error_response(tid,3,out,cap,written);
        fingerprint=hash_bytes(fingerprint,(bth_span_t){p.data,e.total}); ranges=e.value;
        p.data+=e.total; p.len-=e.total;
    }
    size_t offset;
    if (!continuation(p,fingerprint,cursor,&offset)) return error_response(tid,5,out,cap,written);
    if (search) {
        const bth_sdp_record_t *found[8]; size_t count=0;
        for (size_t n=0;n<s->count && count<maximum;++n) if (matches(&s->records[n],pattern)) found[count++]=&s->records[n];
        if (offset>count || (offset==count && offset)) return error_response(tid,5,out,cap,written);
        if (cap<10) return BTH_ERR_CAPACITY;
        size_t send=count-offset;
        if (send>(cap-10)/4) {
            size_t overhead=cursor?12u:18u;
            if (cap<overhead+4) return BTH_ERR_CAPACITY;
            send=(cap-overhead)/4; if (send>count-offset) send=count-offset;
        }
        put16(out+5,(uint16_t)count); put16(out+7,(uint16_t)send);
        for (size_t n=0;n<send;++n) put32(out+9+4*n,found[offset+n]->handle);
        finish(out,3,tid,9+4*send,offset+send<count?offset+send:0,fingerprint,cursor,written); return BTH_OK;
    }
    size_t lengths[8]={0}, total=0;
    for (size_t n=0;n<s->count;++n) {
        const bth_sdp_record_t *r=&s->records[n];
        if (r==single || (combined && matches(r,pattern))) {
            lengths[n]=attribute_size(r,ranges);
            if (!combined) total=lengths[n];
            else if (lengths[n]) total+=sequence_size(lengths[n]);
        }
    }
    if (total>65532) return error_response(tid,6,out,cap,written);
    const size_t logical=sequence_size(total);
    if (offset>=logical && offset) return error_response(tid,5,out,cap,written);
    if (cap<8) return BTH_ERR_CAPACITY;
    size_t send=logical-offset;
    if (send>maximum || send>cap-8 || send>65532) {
        size_t overhead=cursor?10u:16u;
        if (cap<overhead+1) return BTH_ERR_CAPACITY;
        if (send>maximum) send=maximum;
        if (send>cap-overhead) send=cap-overhead;
        if (send>65540u-overhead) send=65540u-overhead;
    }
    stream_t stream={out+7,offset,send,0,0}; emit_sequence(&stream,total);
    for (size_t n=0;n<s->count;++n) {
        if (combined && lengths[n]) { emit_sequence(&stream,lengths[n]); emit_attributes(&stream,&s->records[n],ranges); }
        else if (!combined && &s->records[n]==single) emit_attributes(&stream,single,ranges);
    }
    if (stream.copied!=send) return BTH_ERR_STATE;
    put16(out+5,(uint16_t)send);
    finish(out,combined?7:5,tid,7+send,offset+send<logical?offset+send:0,fingerprint,cursor,written); return BTH_OK;
}
bth_status_t bth_sdp_process(const bth_sdp_t *s,const uint8_t *in,size_t len,uint8_t *out,size_t cap,size_t *written) {
    return process(s,NULL,in,len,out,cap,written);
}
bth_status_t bth_sdp_process_compact(const bth_sdp_t *s,bth_sdp_cursor_t *cursor,const uint8_t *in,size_t len,uint8_t *out,size_t cap,size_t *written) {
    if (!cursor) { if (written) *written=0;return BTH_ERR_PARAM; }
    return process(s,cursor,in,len,out,cap,written);
}
