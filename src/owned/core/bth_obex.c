#include "bth_obex.h"
#include <string.h>
// Independently written from IrDA OBEX 1.5 and Bluetooth GOEP 1.1 (RFCOMM
// transport). Big-endian lengths throughout; every length is checked before use.
static uint16_t be16(const uint8_t* p){return (uint16_t)(((unsigned)p[0]<<8)|p[1]);}
static uint32_t be32(const uint8_t* p){return ((uint32_t)be16(p)<<16)|be16(p+2);}
size_t bth_obex_packet_length(const uint8_t* p,size_t n){return p&&n>=3?be16(p+1):0;}
static bth_status_t header_at(bth_span_t rest,bth_obex_header_t* h,size_t* used){
 const uint8_t id=rest.data[0];size_t length;
 switch(id>>6){
 case 0:case 1:
  if(rest.len<3)return BTH_ERR_LENGTH;length=be16(rest.data+1);
  if(length<3||length>rest.len)return BTH_ERR_LENGTH;
  if(!(id>>6)&&((length-3)&1))return BTH_ERR_LENGTH; // UTF-16 text is whole code units.
  h->data.data=rest.data+3;h->data.len=length-3;h->value=0;break;
 case 2:
  if(rest.len<2)return BTH_ERR_LENGTH;length=2;h->data.data=rest.data+1;h->data.len=1;h->value=rest.data[1];break;
 default:
  if(rest.len<5)return BTH_ERR_LENGTH;length=5;h->data.data=rest.data+1;h->data.len=4;h->value=be32(rest.data+1);break;
 }
 h->id=id;*used=length;return BTH_OK;
}
bth_status_t bth_obex_headers_validate(bth_span_t p){
 bth_obex_header_t h;size_t used;
 if(p.len&&!p.data)return BTH_ERR_PARAM;
 while(p.len){if(header_at(p,&h,&used)!=BTH_OK)return BTH_ERR_LENGTH;p.data+=used;p.len-=used;}
 return BTH_OK;
}
bth_status_t bth_obex_header_next(bth_span_t* rest,bth_obex_header_t* h){
 size_t used;bth_status_t st;
 if(!rest||!h)return BTH_ERR_PARAM;if(!rest->len)return BTH_MORE;if(!rest->data)return BTH_ERR_PARAM;
 st=header_at(*rest,h,&used);if(st!=BTH_OK)return st;rest->data+=used;rest->len-=used;return BTH_OK;
}
bth_status_t bth_obex_header_find(bth_span_t p,uint8_t id,bth_obex_header_t* h){
 bth_status_t st;if(!h)return BTH_ERR_PARAM;
 while((st=bth_obex_header_next(&p,h))==BTH_OK)if(h->id==id)return BTH_OK;
 return st;
}
static bth_status_t frame(const uint8_t* p,size_t n,size_t fixed){
 if(!p)return BTH_ERR_PARAM;if(n<3+fixed||be16(p+1)!=n)return BTH_ERR_LENGTH;return BTH_OK;
}
bth_status_t bth_obex_parse_request(const uint8_t* p,size_t n,bth_obex_request_t* r){
 size_t fixed=0;bth_status_t st;
 if(!r)return BTH_ERR_PARAM;memset(r,0,sizeof *r);if(!p)return BTH_ERR_PARAM;if(n<3)return BTH_ERR_LENGTH;
 if(p[0]==BTH_OBEX_CONNECT)fixed=4;else if(p[0]==BTH_OBEX_SETPATH)fixed=2;
 st=frame(p,n,fixed);if(st!=BTH_OK)return st;
 r->headers.data=p+3+fixed;r->headers.len=n-3-fixed;
 if(bth_obex_headers_validate(r->headers)!=BTH_OK){memset(r,0,sizeof *r);return BTH_ERR_LENGTH;}
 r->final=(p[0]&BTH_OBEX_FINAL)!=0;
 r->opcode=(p[0]==BTH_OBEX_CONNECT||p[0]==BTH_OBEX_DISCONNECT||p[0]==BTH_OBEX_SETPATH||p[0]==BTH_OBEX_ABORT)?p[0]:(uint8_t)(p[0]&0x7f);
 if(p[0]==BTH_OBEX_CONNECT){r->version=p[3];r->flags=p[4];r->max_packet=be16(p+5);}
 else if(p[0]==BTH_OBEX_SETPATH){r->flags=p[3];r->constants=p[4];}
 return BTH_OK;
}
bth_status_t bth_obex_parse_response(const uint8_t* p,size_t n,bool connect,bth_obex_response_t* r){
 size_t fixed=0;bth_status_t st;
 if(!r)return BTH_ERR_PARAM;memset(r,0,sizeof *r);if(!p)return BTH_ERR_PARAM;if(n<3)return BTH_ERR_LENGTH;
 if(connect&&(n>=7||(p[0]&0x7f)==(BTH_OBEX_SUCCESS&0x7f)))fixed=4;
 st=frame(p,n,fixed);if(st!=BTH_OK)return st;
 r->headers.data=p+3+fixed;r->headers.len=n-3-fixed;
 if(bth_obex_headers_validate(r->headers)!=BTH_OK){memset(r,0,sizeof *r);return BTH_ERR_LENGTH;}
 r->code=(uint8_t)(p[0]|BTH_OBEX_FINAL);r->final=(p[0]&BTH_OBEX_FINAL)!=0;
 if(fixed){r->version=p[3];r->flags=p[4];r->max_packet=be16(p+5);}
 return BTH_OK;
}
static uint8_t* reserve(bth_obex_writer_t* w,size_t n){
 uint8_t* p;if(!w||w->overflow||!w->data||w->capacity-w->used<n||w->used+n>65535u){if(w)w->overflow=true;return NULL;}
 p=w->data+w->used;w->used+=n;return p;
}
void bth_obex_writer_init(bth_obex_writer_t* w,uint8_t* b,size_t capacity,uint8_t opcode){
 uint8_t* p;if(!w)return;w->data=b;w->capacity=b?capacity:0;w->used=0;w->overflow=false;
 p=reserve(w,3);if(p){p[0]=opcode;p[1]=p[2]=0;}
}
void bth_obex_put_u8(bth_obex_writer_t* w,uint8_t v){uint8_t* p=reserve(w,1);if(p)p[0]=v;}
void bth_obex_put_u16(bth_obex_writer_t* w,uint16_t v){uint8_t* p=reserve(w,2);if(p){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}}
void bth_obex_add_u8(bth_obex_writer_t* w,uint8_t id,uint8_t v){uint8_t* p;if((id>>6)!=2){if(w)w->overflow=true;return;}p=reserve(w,2);if(p){p[0]=id;p[1]=v;}}
void bth_obex_add_u32(bth_obex_writer_t* w,uint8_t id,uint32_t v){
 uint8_t* p;if((id>>6)!=3){if(w)w->overflow=true;return;}p=reserve(w,5);
 if(p){p[0]=id;p[1]=(uint8_t)(v>>24);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>8);p[4]=(uint8_t)v;}
}
void bth_obex_add_bytes(bth_obex_writer_t* w,uint8_t id,const uint8_t* data,size_t n){
 uint8_t* p;if((id>>6)!=1||(n&&!data)||n>65532u){if(w)w->overflow=true;return;}p=reserve(w,3+n);
 if(p){p[0]=id;p[1]=(uint8_t)((n+3)>>8);p[2]=(uint8_t)(n+3);if(n)memcpy(p+3,data,n);}
}
// Strict UTF-8 decoding: rejects overlong forms, surrogates and >U+10FFFF.
static size_t decode_utf8(const uint8_t* s,size_t n,uint32_t* cp){
 uint32_t v;size_t k,i;
 if(!n)return 0;
 if(s[0]<0x80){*cp=s[0];return 1;}
 if(s[0]>=0xc2&&s[0]<0xe0){k=2;v=s[0]&0x1f;}else if(s[0]>=0xe0&&s[0]<0xf0){k=3;v=s[0]&0x0f;}else if(s[0]>=0xf0&&s[0]<0xf5){k=4;v=s[0]&0x07;}else return 0;
 if(n<k)return 0;for(i=1;i<k;++i){if((s[i]&0xc0)!=0x80)return 0;v=(v<<6)|(s[i]&0x3f);}
 if((k==3&&v<0x800)||(k==4&&v<0x10000)||v>0x10ffff||(v>=0xd800&&v<0xe000))return 0;
 *cp=v;return k;
}
void bth_obex_add_text(bth_obex_writer_t* w,uint8_t id,const char* text,size_t n){
 const uint8_t* s=(const uint8_t*)text;size_t at=0,units=0;uint8_t* p;uint32_t cp;
 if((id>>6)!=0||(n&&!text)){if(w)w->overflow=true;return;}
 while(at<n){const size_t k=decode_utf8(s+at,n-at,&cp);if(!k||!cp){if(w)w->overflow=true;return;}units+=cp>=0x10000?2:1;at+=k;}
 if(units&&units+1>32766u){if(w)w->overflow=true;return;}
 p=reserve(w,3+(units?2*(units+1):0));if(!p)return;
 p[0]=id;{const size_t length=3+(units?2*(units+1):0);p[1]=(uint8_t)(length>>8);p[2]=(uint8_t)length;}
 p+=3;at=0;
 while(at<n){const size_t k=decode_utf8(s+at,n-at,&cp);at+=k;
  if(cp>=0x10000){const uint32_t v=cp-0x10000;const uint16_t hi=(uint16_t)(0xd800+(v>>10)),lo=(uint16_t)(0xdc00+(v&0x3ff));
   p[0]=(uint8_t)(hi>>8);p[1]=(uint8_t)hi;p[2]=(uint8_t)(lo>>8);p[3]=(uint8_t)lo;p+=4;}
  else{p[0]=(uint8_t)(cp>>8);p[1]=(uint8_t)cp;p+=2;}
 }
 if(units){p[0]=p[1]=0;}
}
bth_status_t bth_obex_finish(bth_obex_writer_t* w,size_t* n){
 if(n)*n=0;if(!w||!n)return BTH_ERR_PARAM;if(w->overflow||w->used<3)return BTH_ERR_CAPACITY;
 w->data[1]=(uint8_t)(w->used>>8);w->data[2]=(uint8_t)w->used;*n=w->used;return BTH_OK;
}
size_t bth_obex_text_to_utf8(bth_span_t p,char* out,size_t capacity){
 size_t used=0,at=0;
 if(!out||!capacity)return 0;out[0]=0;if(!p.data)return 0;
 while(at+1<p.len){
  uint32_t cp=be16(p.data+at);size_t k;uint8_t b[4];at+=2;
  if(!cp)break;
  if(cp>=0xd800&&cp<0xdc00){
   if(at+1<p.len&&be16(p.data+at)>=0xdc00&&be16(p.data+at)<0xe000){cp=0x10000+((cp-0xd800)<<10)+(be16(p.data+at)-0xdc00);at+=2;}else cp='?';
  }else if(cp>=0xdc00&&cp<0xe000)cp='?';
  if(cp<0x80){b[0]=(uint8_t)cp;k=1;}
  else if(cp<0x800){b[0]=(uint8_t)(0xc0|(cp>>6));b[1]=(uint8_t)(0x80|(cp&0x3f));k=2;}
  else if(cp<0x10000){b[0]=(uint8_t)(0xe0|(cp>>12));b[1]=(uint8_t)(0x80|((cp>>6)&0x3f));b[2]=(uint8_t)(0x80|(cp&0x3f));k=3;}
  else{b[0]=(uint8_t)(0xf0|(cp>>18));b[1]=(uint8_t)(0x80|((cp>>12)&0x3f));b[2]=(uint8_t)(0x80|((cp>>6)&0x3f));b[3]=(uint8_t)(0x80|(cp&0x3f));k=4;}
  if(capacity-1-used<k)break;
  memcpy(out+used,b,k);used+=k;
 }
 out[used]=0;return used;
}
bth_status_t bth_obex_tlv_next(bth_span_t* rest,uint8_t* tag,bth_span_t* value){
 if(!rest||!tag||!value)return BTH_ERR_PARAM;if(!rest->len)return BTH_MORE;if(!rest->data)return BTH_ERR_PARAM;
 if(rest->len<2||rest->data[1]>rest->len-2)return BTH_ERR_LENGTH;
 *tag=rest->data[0];value->data=rest->data+2;value->len=rest->data[1];rest->data+=2u+rest->data[1];rest->len-=2u+value->len;return BTH_OK;
}
void bth_obex_assembler_init(bth_obex_assembler_t* a,uint8_t* b,size_t capacity){if(a){a->data=b;a->capacity=b?capacity:0;a->used=0;}}
void bth_obex_assembler_reset(bth_obex_assembler_t* a){if(a)a->used=0;}
bool bth_obex_assembler_complete(const bth_obex_assembler_t* a){
 return a&&a->used>=3&&be16(a->data+1)>=3&&a->used==be16(a->data+1);
}
bth_status_t bth_obex_assemble(bth_obex_assembler_t* a,const uint8_t* p,size_t n,size_t* length){
 size_t expected=0;
 if(length)*length=0;if(!a||!a->data||!length||(n&&!p))return BTH_ERR_PARAM;
 if(bth_obex_assembler_complete(a))return BTH_ERR_STATE; // Previous packet not consumed.
 if(n>a->capacity-a->used)return BTH_ERR_LENGTH;
 // Validate the declared length as soon as its three prefix bytes exist.
 if(a->used+n>=3){
  uint8_t prefix[3];size_t i;for(i=0;i<3;++i)prefix[i]=i<a->used?a->data[i]:p[i-a->used];
  expected=be16(prefix+1);if(expected<3||expected>a->capacity||a->used+n>expected)return BTH_ERR_LENGTH;
 }
 if(n)memcpy(a->data+a->used,p,n);a->used+=n;
 if(expected&&a->used==expected){*length=expected;return BTH_OK;}
 return BTH_MORE;
}
