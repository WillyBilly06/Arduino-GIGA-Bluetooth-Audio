#include "bth_sdp_client.h"
#include <string.h>
// Independently written from the Bluetooth Core SDP chapter. Big-endian.
typedef struct { uint8_t type; bth_span_t value; size_t total; } item_t;
static uint16_t be16(const uint8_t* p){return (uint16_t)(((unsigned)p[0]<<8)|p[1]);}
static uint32_t be32(const uint8_t* p){return ((uint32_t)be16(p)<<16)|be16(p+2);}
static void put16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static bool item(bth_span_t p,item_t* e){
 unsigned type,size;size_t header=1,count;
 if(!p.len||!p.data)return false;type=p.data[0]>>3;size=p.data[0]&7;
 if(type>8||(type==0&&size))return false;
 if(type==0)count=0;
 else if(size<=4)count=(size_t)1u<<size;
 else{header+=size==5?1:(size==6?2:4);if(p.len<header)return false;count=size==5?p.data[1]:(size==6?be16(p.data+1):be32(p.data+1));}
 if(count>p.len-header)return false;
 if(type==3&&count!=2&&count!=4&&count!=16)return false;
 e->type=(uint8_t)type;e->value.data=p.data+header;e->value.len=count;e->total=header+count;return true;
}
static bool next(bth_span_t* p,item_t* e){if(!item(*p,e))return false;p->data+=e->total;p->len-=e->total;return true;}
static bool uuid16(const item_t* e,uint16_t* v){
 static const uint8_t base[]={0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
 if(e->type!=3)return false;
 if(e->value.len==2){*v=be16(e->value.data);return true;}
 if(e->value.len==4){if(be16(e->value.data))return false;*v=be16(e->value.data+2);return true;}
 if(be16(e->value.data)||memcmp(e->value.data+4,base,12))return false;*v=be16(e->value.data+2);return true;
}
static bool uint_value(const item_t* e,uint32_t* v){
 if(e->type!=1)return false;
 switch(e->value.len){case 1:*v=e->value.data[0];return true;case 2:*v=be16(e->value.data);return true;case 4:*v=be32(e->value.data);return true;default:return false;}
}
bool bth_sdp_client_init(bth_sdp_client_t* c,uint16_t uuid,uint8_t* b,size_t capacity,uint16_t tid){
 if(!c)return false;memset(c,0,sizeof *c);if(!b||capacity<16||!uuid)return false;
 c->buffer=b;c->capacity=capacity;c->uuid=uuid;c->transaction=tid;c->initialized=true;return true;
}
bool bth_sdp_client_set_attributes(bth_sdp_client_t* c,const uint8_t* list,size_t n){
 if(!c||!c->initialized||c->rounds||!list||n<2||n>BTH_SDP_CLIENT_MAX_ATTRIBUTE_LIST||list[0]!=0x35||list[1]!=n-2)return false;
 c->attributes=list;c->attributes_len=n;return true;
}
bth_status_t bth_sdp_client_request(bth_sdp_client_t* c,uint8_t* out,size_t capacity,size_t* written){
 static const uint8_t defaults[]={0x35,15,0x09,0x00,0x04,0x09,0x00,0x09,0x09,0x02,0x00,0x09,0x03,0x14,0x09,0x03,0x17};
 const uint8_t* attributes;size_t length,n,at;
 if(written)*written=0;if(!c||!out||!written)return BTH_ERR_PARAM;if(!c->initialized||c->awaiting||c->complete)return BTH_ERR_STATE;
 if(c->rounds>=32)return BTH_ERR_CAPACITY;
 attributes=c->attributes?c->attributes:defaults;length=c->attributes?c->attributes_len:sizeof defaults;
 n=5+5+2+length+1+c->continuation[0];if(capacity<n)return BTH_ERR_CAPACITY;
 ++c->transaction;out[0]=0x06;put16(out+1,c->transaction);put16(out+3,(uint16_t)(n-5));
 out[5]=0x35;out[6]=3;out[7]=0x19;put16(out+8,c->uuid);put16(out+10,BTH_SDP_CLIENT_MAX_ATTRIBUTE_BYTES);
 memcpy(out+12,attributes,length);at=12+length;
 memcpy(out+at,c->continuation,1u+c->continuation[0]);
 c->awaiting=true;++c->rounds;*written=n;return BTH_OK;
}
static bth_status_t fail(bth_sdp_client_t* c,bth_status_t st){c->initialized=false;c->awaiting=false;return st;}
bth_status_t bth_sdp_client_response(bth_sdp_client_t* c,const uint8_t* p,size_t n){
 size_t count,cont;
 if(!c||(!p&&n))return BTH_ERR_PARAM;if(!c->initialized||!c->awaiting)return BTH_ERR_STATE;
 if(n<5||be16(p+1)!=c->transaction||be16(p+3)!=n-5)return fail(c,BTH_ERR_LENGTH);
 if(p[0]==0x01)return fail(c,BTH_UNSUPPORTED);
 if(p[0]!=0x07||n<5+2+1)return fail(c,BTH_ERR_LENGTH);
 count=be16(p+5);if(count>BTH_SDP_CLIENT_MAX_ATTRIBUTE_BYTES||count>n-8)return fail(c,BTH_ERR_LENGTH);
 cont=p[7+count];if(cont>BTH_SDP_CLIENT_CONTINUATION||8+count+cont!=n)return fail(c,BTH_ERR_LENGTH);
 if(count>c->capacity-c->used)return fail(c,BTH_ERR_CAPACITY);
 memcpy(c->buffer+c->used,p+7,count);c->used+=count;
 memcpy(c->continuation,p+7+count,1+cont);c->awaiting=false;
 if(cont)return BTH_MORE;
 c->complete=true;return BTH_OK;
}
static void protocols(bth_span_t list,bth_sdp_service_t* s,unsigned depth){
 item_t e;
 if(depth>3)return;
 while(list.len&&next(&list,&e)){
  if(e.type==7){protocols(e.value,s,depth+1);continue;} // Alternative lists.
  if(e.type!=6)continue;{
   bth_span_t proto=e.value;item_t id,param;uint16_t uuid;uint32_t v;
   memset(&id,0,sizeof id);
   if(next(&proto,&id)&&uuid16(&id,&uuid)&&uuid==0x0003&&next(&proto,&param)&&uint_value(&param,&v)&&v>=1&&v<=30&&!s->rfcomm_channel)s->rfcomm_channel=(uint8_t)v;
   if(id.type==6)protocols(e.value,s,depth+1);
  }
 }
}
static void profile(bth_span_t list,bth_sdp_service_t* s){
 item_t e;
 while(list.len&&next(&list,&e)){
  bth_span_t d=e.value;item_t id,ver;uint16_t uuid;uint32_t v;
  if(e.type==6&&next(&d,&id)&&uuid16(&id,&uuid)&&next(&d,&ver)&&uint_value(&ver,&v)&&ver.value.len==2&&!s->profile_version)s->profile_version=(uint16_t)v;
 }
}
bth_status_t bth_sdp_client_parse(const uint8_t* p,size_t n,bth_sdp_service_t* s){
 item_t root,record;bth_span_t records;
 if(!s)return BTH_ERR_PARAM;memset(s,0,sizeof *s);if(!p&&n)return BTH_ERR_PARAM;
 if(!item((bth_span_t){p,n},&root)||root.type!=6||root.total!=n)return BTH_ERR_LENGTH;
 records=root.value;
 while(records.len){
  bth_sdp_service_t candidate;bth_span_t attrs;
  if(!next(&records,&record)||record.type!=6)return BTH_ERR_LENGTH;
  memset(&candidate,0,sizeof candidate);attrs=record.value;
  while(attrs.len){
   item_t id,value;uint32_t v,attribute;
   if(!next(&attrs,&id)||!uint_value(&id,&attribute)||id.value.len!=2||!next(&attrs,&value))return BTH_ERR_LENGTH;
   if(attribute==0x0004&&(value.type==6||value.type==7))protocols(value.value,&candidate,0);
   else if(attribute==0x0009&&value.type==6)profile(value.value,&candidate);
   else if(attribute==0x0200&&uint_value(&value,&v)&&value.value.len==2)candidate.goep_l2cap_psm=(uint16_t)v;
   else if(attribute==0x0314&&uint_value(&value,&v)&&value.value.len==1)candidate.supported_repositories=(uint8_t)v;
   else if(attribute==0x0315&&uint_value(&value,&v)&&value.value.len==1){candidate.mas_instance_id=(uint8_t)v;candidate.has_mas_instance=true;}
   else if(attribute==0x0316&&uint_value(&value,&v)&&value.value.len==1)candidate.supported_message_types=(uint8_t)v;
   else if(attribute==0x0317&&uint_value(&value,&v)&&value.value.len==4){candidate.supported_features=v;candidate.has_supported_features=true;}
  }
  if(candidate.rfcomm_channel&&!s->found){*s=candidate;s->found=true;}
 }
 return BTH_OK;
}
