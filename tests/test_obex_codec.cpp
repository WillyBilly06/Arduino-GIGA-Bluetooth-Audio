// OBEX packet codec and SDP-client parser checks: exact encodings, UTF-16
// names, fragmentation/reassembly and malformed input. Pure portable C core;
// run under the runner's UBSan (and ASan with --asan). No radio or phone.
extern "C" {
#include "bth_obex.h"
#include "bth_sdp_client.h"
#include "bth_sdp.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace {
unsigned checks;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"OBEX CODEC CHECK line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Bytes=std::vector<uint8_t>;
Bytes finish(bth_obex_writer_t& w){size_t n=0;REQUIRE(bth_obex_finish(&w,&n)==BTH_OK);return Bytes(w.data,w.data+n);}
uint32_t lcg(uint32_t& s){s=s*1664525u+1013904223u;return s>>8;}

void writer_exact(){
 uint8_t b[256];bth_obex_writer_t w;
 // PBAP CONNECT with Target.
 static const uint8_t target[16]={0x79,0x61,0x35,0xf0,0xf0,0xc5,0x11,0xd8,0x09,0x66,0x08,0x00,0x20,0x0c,0x9a,0x66};
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_CONNECT);bth_obex_put_u8(&w,0x10);bth_obex_put_u8(&w,0);bth_obex_put_u16(&w,1024);bth_obex_add_bytes(&w,BTH_OBEX_H_TARGET,target,16);
 Bytes connect={0x80,0,26,0x10,0,4,0,0x46,0,19};connect.insert(connect.end(),target,target+16);REQUIRE(finish(w)==connect);
 // PUT final: Name "a.txt", Length 5, End-of-Body "hello".
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT|BTH_OBEX_FINAL);bth_obex_add_text(&w,BTH_OBEX_H_NAME,"a.txt",5);bth_obex_add_u32(&w,BTH_OBEX_H_LENGTH,5);
 bth_obex_add_bytes(&w,BTH_OBEX_H_END_OF_BODY,reinterpret_cast<const uint8_t*>("hello"),5);
 const Bytes put={0x82,0,31,0x01,0,15,0,'a',0,'.',0,'t',0,'x',0,'t',0,0,0xc3,0,0,0,5,0x49,0,8,'h','e','l','l','o'};
 REQUIRE(finish(w)==put);
 // Empty name is the 3-byte header; one-byte header encoding.
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_GET|BTH_OBEX_FINAL);bth_obex_add_text(&w,BTH_OBEX_H_NAME,"",0);bth_obex_add_u8(&w,0x97,1);
 REQUIRE(finish(w)==Bytes({0x83,0,8,0x01,0,3,0x97,1}));
 // Wrong encoding class for an id, invalid UTF-8 and overflow are rejected whole.
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_u32(&w,BTH_OBEX_H_NAME,1);size_t n=9;REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY&&n==0);
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_text(&w,BTH_OBEX_H_NAME,"\xc3\x28",2);REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY);
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_text(&w,BTH_OBEX_H_NAME,"\xed\xa0\x80",3);REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY); // Encoded surrogate.
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_text(&w,BTH_OBEX_H_NAME,"\xc0\x80",2);REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY);   // Overlong NUL.
 uint8_t small[10];bth_obex_writer_init(&w,small,sizeof small,BTH_OBEX_PUT);bth_obex_add_bytes(&w,BTH_OBEX_H_BODY,b,8);REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY);
 bth_obex_writer_init(&w,small,2,BTH_OBEX_PUT);REQUIRE(bth_obex_finish(&w,&n)==BTH_ERR_CAPACITY);
}
void utf16_names(){
 uint8_t b[128];bth_obex_writer_t w;
 // U+00E9, U+20AC and U+1F600 (surrogate pair) round trip.
 const char text[]="\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80.vcf";
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_text(&w,BTH_OBEX_H_NAME,text,strlen(text));const Bytes p=finish(w);
 const Bytes header={0x01,0,21,0,0xe9,0x20,0xac,0xd8,0x3d,0xde,0x00,0,'.',0,'v',0,'c',0,'f',0,0};
 REQUIRE(Bytes(p.begin()+3,p.end())==header);
 char out[64];REQUIRE(bth_obex_text_to_utf8({p.data()+6,p.size()-6},out,sizeof out)==strlen(text)&&!strcmp(out,text));
 // Truncation only on a code-point boundary; NUL always written.
 REQUIRE(bth_obex_text_to_utf8({p.data()+6,p.size()-6},out,4)==2&&!strcmp(out,"\xc3\xa9"));
 REQUIRE(bth_obex_text_to_utf8({p.data()+6,p.size()-6},out,1)==0&&out[0]==0);
 // Unpaired surrogates become '?', odd trailing byte ignored, NUL stops.
 const uint8_t odd[]={0xd8,0x00,0,'A',0xdc,0x01,0,'B',0,0,0,'C',0x41};
 REQUIRE(bth_obex_text_to_utf8({odd,sizeof odd},out,sizeof out)==4&&!strcmp(out,"?A?B"));
 REQUIRE(bth_obex_text_to_utf8({nullptr,0},out,sizeof out)==0&&out[0]==0);
}
void parse_packets(){
 bth_obex_request_t r;bth_obex_response_t s;bth_obex_header_t h;
 const uint8_t connect[]={0x80,0,12,0x10,0,0x10,0,0xcb,0,0,0,7};
 REQUIRE(bth_obex_parse_request(connect,sizeof connect,&r)==BTH_OK&&r.opcode==BTH_OBEX_CONNECT&&r.final&&r.version==0x10&&r.max_packet==0x1000&&r.headers.len==5);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_CONNECTION_ID,&h)==BTH_OK&&h.value==7);
 REQUIRE(bth_obex_header_find(r.headers,BTH_OBEX_H_NAME,&h)==BTH_MORE);
 const uint8_t setpath[]={0x85,0,8,0x02,0,0x01,0,3};
 REQUIRE(bth_obex_parse_request(setpath,sizeof setpath,&r)==BTH_OK&&r.opcode==BTH_OBEX_SETPATH&&r.flags==2&&r.headers.len==3);
 const uint8_t put[]={0x02,0,8,0x48,0,5,'x','y'};
 REQUIRE(bth_obex_parse_request(put,sizeof put,&r)==BTH_OK&&r.opcode==BTH_OBEX_PUT&&!r.final);
 const uint8_t abort_[]={0xff,0,3};REQUIRE(bth_obex_parse_request(abort_,3,&r)==BTH_OK&&r.opcode==BTH_OBEX_ABORT&&r.final);
 // Malformed: length field mismatch, truncated CONNECT, header overrun,
 // header length <3, odd UTF-16 payload, truncated 4-byte and 1-byte headers.
 const uint8_t bad_length[]={0x82,0,9,0x49,0,3};REQUIRE(bth_obex_parse_request(bad_length,sizeof bad_length,&r)==BTH_ERR_LENGTH);
 const uint8_t short_connect[]={0x80,0,5,0x10,0};REQUIRE(bth_obex_parse_request(short_connect,sizeof short_connect,&r)==BTH_ERR_LENGTH);
 const uint8_t overrun[]={0x82,0,8,0x48,0,9,1,2};REQUIRE(bth_obex_parse_request(overrun,sizeof overrun,&r)==BTH_ERR_LENGTH&&!r.opcode);
 const uint8_t tiny[]={0x82,0,6,0x48,0,2};REQUIRE(bth_obex_parse_request(tiny,sizeof tiny,&r)==BTH_ERR_LENGTH);
 const uint8_t odd[]={0x82,0,7,0x01,0,4,'a'};REQUIRE(bth_obex_parse_request(odd,sizeof odd,&r)==BTH_ERR_LENGTH);
 const uint8_t cut32[]={0x82,0,6,0xc3,0,0};REQUIRE(bth_obex_parse_request(cut32,sizeof cut32,&r)==BTH_ERR_LENGTH);
 const uint8_t cut8[]={0x82,0,4,0x97};REQUIRE(bth_obex_parse_request(cut8,sizeof cut8,&r)==BTH_ERR_LENGTH);
 REQUIRE(bth_obex_parse_request(put,2,&r)==BTH_ERR_LENGTH&&bth_obex_parse_request(nullptr,3,&r)==BTH_ERR_PARAM);
 // CONNECT responses: success needs the 7-byte layout, failure may omit it.
 const uint8_t ok[]={0xa0,0,12,0x10,0,0x04,0,0xcb,0,0,0,1};
 REQUIRE(bth_obex_parse_response(ok,sizeof ok,true,&s)==BTH_OK&&s.code==0xa0&&s.max_packet==1024&&s.headers.len==5);
 const uint8_t ok_short[]={0xa0,0,3};REQUIRE(bth_obex_parse_response(ok_short,3,true,&s)==BTH_ERR_LENGTH&&bth_obex_parse_response(ok_short,3,false,&s)==BTH_OK);
 const uint8_t refused[]={0xc3,0,3};REQUIRE(bth_obex_parse_response(refused,3,true,&s)==BTH_OK&&s.code==0xc3);
 const uint8_t cont[]={0x90,0,9,0x48,0,6,'a','b','c'};REQUIRE(bth_obex_parse_response(cont,sizeof cont,false,&s)==BTH_OK&&s.code==BTH_OBEX_CONTINUE);
 // Application-parameter TLVs.
 const uint8_t tlv[]={0x08,2,0x01,0x2c,0x04,1};bth_span_t rest={tlv,sizeof tlv};uint8_t tag;bth_span_t value;
 REQUIRE(bth_obex_tlv_next(&rest,&tag,&value)==BTH_OK&&tag==8&&value.len==2&&value.data[1]==0x2c);
 REQUIRE(bth_obex_tlv_next(&rest,&tag,&value)==BTH_ERR_LENGTH);rest={tlv,4};REQUIRE(bth_obex_tlv_next(&rest,&tag,&value)==BTH_OK&&bth_obex_tlv_next(&rest,&tag,&value)==BTH_MORE);
}
void fragmentation(){
 // Build one 1000-byte PUT and reassemble it from every RFCOMM-like split.
 uint8_t b[1100];bth_obex_writer_t w;Bytes body(980);for(size_t i=0;i<body.size();++i)body[i]=uint8_t(i*7+3);
 bth_obex_writer_init(&w,b,sizeof b,BTH_OBEX_PUT);bth_obex_add_bytes(&w,BTH_OBEX_H_BODY,body.data(),body.size());const Bytes packet=finish(w);
 REQUIRE(packet.size()==986);
 for(size_t frame:{size_t(1),size_t(2),size_t(3),size_t(127),size_t(500),packet.size()}){
  uint8_t storage[1024];bth_obex_assembler_t a;bth_obex_assembler_init(&a,storage,sizeof storage);size_t at=0,length=0;bth_status_t st=BTH_MORE;
  while(at<packet.size()){const size_t k=packet.size()-at<frame?packet.size()-at:frame;st=bth_obex_assemble(&a,packet.data()+at,k,&length);at+=k;if(at<packet.size())REQUIRE(st==BTH_MORE&&!bth_obex_assembler_complete(&a));}
  REQUIRE(st==BTH_OK&&length==packet.size()&&bth_obex_assembler_complete(&a)&&!memcmp(storage,packet.data(),length));
  REQUIRE(bth_obex_assemble(&a,packet.data(),1,&length)==BTH_ERR_STATE); // Not consumed yet.
  bth_obex_assembler_reset(&a);REQUIRE(!bth_obex_assembler_complete(&a));
 }
 uint8_t storage[512];bth_obex_assembler_t a;bth_obex_assembler_init(&a,storage,sizeof storage);size_t length=0;
 // Declared length above capacity, below 3, and bytes beyond the packet.
 REQUIRE(bth_obex_assemble(&a,packet.data(),3,&length)==BTH_ERR_LENGTH&&a.used==0);
 const uint8_t two[]={0x82,0};REQUIRE(bth_obex_assemble(&a,two,2,&length)==BTH_MORE);const uint8_t low[]={2};REQUIRE(bth_obex_assemble(&a,low,1,&length)==BTH_ERR_LENGTH&&a.used==2);
 bth_obex_assembler_reset(&a);const uint8_t pipelined[]={0x82,0,3,0x81};REQUIRE(bth_obex_assemble(&a,pipelined,4,&length)==BTH_ERR_LENGTH&&a.used==0);
 REQUIRE(bth_obex_assemble(&a,pipelined,3,&length)==BTH_OK&&length==3);
 // Capacity overflow without a valid prefix.
 bth_obex_assembler_reset(&a);Bytes big(600,0);REQUIRE(bth_obex_assemble(&a,big.data(),big.size(),&length)==BTH_ERR_LENGTH&&a.used==0);
}
void fuzz(){
 // Deterministic random and mutated packets: only safe status results.
 uint32_t seed=0x1234567u;uint8_t p[300];char text[40];
 for(unsigned round=0;round<40000;++round){
  const size_t n=lcg(seed)%sizeof p;for(size_t i=0;i<n;++i)p[i]=uint8_t(lcg(seed));
  if(n>=3&&(round&1)){p[1]=uint8_t(n>>8);p[2]=uint8_t(n);} // Correct framing, random headers.
  bth_obex_request_t r;bth_obex_response_t s;const auto st=bth_obex_parse_request(p,n,&r);
  REQUIRE(st==BTH_OK||st==BTH_ERR_LENGTH);
  if(st==BTH_OK){bth_span_t rest=r.headers;bth_obex_header_t h;unsigned k=0;while(bth_obex_header_next(&rest,&h)==BTH_OK){++k;if(!(h.id>>6))bth_obex_text_to_utf8(h.data,text,sizeof text);}REQUIRE(!rest.len);(void)k;}
  const auto rs=bth_obex_parse_response(p,n,round&2,&s);REQUIRE(rs==BTH_OK||rs==BTH_ERR_LENGTH);
  bth_span_t tlv={p,n};uint8_t tag;bth_span_t value;for(unsigned k=0;k<300&&bth_obex_tlv_next(&tlv,&tag,&value)==BTH_OK;++k){}
  uint8_t storage[256];bth_obex_assembler_t a;bth_obex_assembler_init(&a,storage,sizeof storage);size_t length=0,at=0;
  while(at<n){const size_t k=1+lcg(seed)%40;const size_t take=k<n-at?k:n-at;const auto as=bth_obex_assemble(&a,p+at,take,&length);REQUIRE(as==BTH_OK||as==BTH_MORE||as==BTH_ERR_LENGTH||as==BTH_ERR_STATE);if(as==BTH_OK){REQUIRE(length<=sizeof storage);bth_obex_assembler_reset(&a);}at+=take;}
  bth_sdp_service_t service;const auto ps=bth_sdp_client_parse(p,n,&service);REQUIRE(ps==BTH_OK||ps==BTH_ERR_LENGTH);if(service.found)REQUIRE(service.rfcomm_channel>=1&&service.rfcomm_channel<=30);
  bth_sdp_client_t c;uint8_t lists[64];uint8_t req[64];size_t w=0;REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&w)==BTH_OK);
  if(n>=3){p[1]=uint8_t(c.transaction>>8);p[2]=uint8_t(c.transaction);}const auto cs=bth_sdp_client_response(&c,p,n);REQUIRE(cs==BTH_OK||cs==BTH_MORE||cs<0||cs==BTH_UNSUPPORTED);
 }
}
// PSE record shaped like common phone stacks; parsed through a real SDP server.
const uint8_t pse_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x09, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x2f,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x13,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02, 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x30,0x09,0x01,0x02,
 0x09,0x01,0x00,0x25,0x08,'P','B','A','P',' ','P','S','E', 0x09,0x02,0x00,0x09,0x10,0x25,
 0x09,0x03,0x14,0x08,0x03, 0x09,0x03,0x17,0x0a,0x00,0x00,0x03,0xff};
const uint8_t sink_record[]={0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x01,0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x0b};
void sdp_client(){
 bth_sdp_record_t records[]={{0x10001,{sink_record,sizeof sink_record}},{0x10009,{pse_record,sizeof pse_record}}};
 bth_sdp_t server{};REQUIRE(bth_sdp_init(&server,records,2,9));
 uint8_t lists[640],req[64],rsp[700];size_t n=0,m=0;bth_sdp_client_t c;REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,0x40));
 REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 const Bytes expected={0x06,0x00,0x41,0x00,0x19,0x35,0x03,0x19,0x11,0x2f,0x02,0x40,0x35,15,0x09,0x00,0x04,0x09,0x00,0x09,0x09,0x02,0x00,0x09,0x03,0x14,0x09,0x03,0x17,0x00};
 REQUIRE(Bytes(req,req+n)==expected);
 REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_ERR_STATE); // Awaiting the response.
 REQUIRE(bth_sdp_process(&server,req,expected.size(),rsp,sizeof rsp,&m)==BTH_OK&&rsp[0]==0x07);
 REQUIRE(bth_sdp_client_response(&c,rsp,m)==BTH_OK&&c.complete);
 bth_sdp_service_t s;REQUIRE(bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&s.found&&s.rfcomm_channel==19&&s.profile_version==0x0102&&s.goep_l2cap_psm==0x1025&&s.supported_repositories==3&&s.supported_features==0x3ff);
 // Continuation: a 40-byte response limit forces several rounds through the real server.
 bth_sdp_cursor_t cursor{};REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,7));unsigned rounds=0;bth_status_t st=BTH_MORE;
 while(st==BTH_MORE){REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);REQUIRE(bth_sdp_process_compact(&server,&cursor,req,n,rsp,40,&m)==BTH_OK);st=bth_sdp_client_response(&c,rsp,m);++rounds;REQUIRE(rounds<40);}
 REQUIRE(st==BTH_OK&&rounds>1&&bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&s.rfcomm_channel==19);
 // No matching record: empty attribute lists, not found.
 REQUIRE(bth_sdp_client_init(&c,0x1105,lists,sizeof lists,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 REQUIRE(bth_sdp_process(&server,req,n,rsp,sizeof rsp,&m)==BTH_OK&&bth_sdp_client_response(&c,rsp,m)==BTH_OK&&bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&!s.found);
 // Error response, wrong transaction, bad continuation length, storage exhaustion.
 REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 const uint8_t error[]={0x01,0x00,0x02,0x00,0x02,0x00,0x03};REQUIRE(bth_sdp_client_response(&c,error,sizeof error)==BTH_UNSUPPORTED);
 REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_ERR_STATE);
 REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 const uint8_t wrong_tid[]={0x07,0x00,0x09,0x00,0x05,0x00,0x02,0x35,0x00,0x00};REQUIRE(bth_sdp_client_response(&c,wrong_tid,sizeof wrong_tid)==BTH_ERR_LENGTH);
 REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 const uint8_t bad_cont[]={0x07,0x00,0x02,0x00,0x16,0x00,0x02,0x35,0x00,0x11,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};REQUIRE(bth_sdp_client_response(&c,bad_cont,sizeof bad_cont)==BTH_ERR_LENGTH);
 uint8_t tiny[16];REQUIRE(bth_sdp_client_init(&c,0x112f,tiny,sizeof tiny,1));REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 REQUIRE(bth_sdp_process(&server,req,n,rsp,sizeof rsp,&m)==BTH_OK&&bth_sdp_client_response(&c,rsp,m)==BTH_ERR_CAPACITY);
 // 128-bit RFCOMM UUID inside an alternative (DEA) protocol descriptor list.
 auto seq=[](uint8_t type,const Bytes& content){Bytes out={type,uint8_t(content.size())};out.insert(out.end(),content.begin(),content.end());return out;};
 auto uuid128=[](uint16_t v){Bytes u={0x1c,0,0,uint8_t(v>>8),uint8_t(v),0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};return u;};
 Bytes rfcomm=uuid128(0x0003);rfcomm.insert(rfcomm.end(),{0x08,0x07});
 Bytes pdl=seq(0x35,uuid128(0x0100));{const Bytes r=seq(0x35,rfcomm);pdl.insert(pdl.end(),r.begin(),r.end());}
 Bytes attr={0x09,0x00,0x04};{const Bytes d=seq(0x3d,seq(0x35,pdl));attr.insert(attr.end(),d.begin(),d.end());}
 const Bytes alt=seq(0x35,seq(0x35,attr));
 REQUIRE(bth_sdp_client_parse(alt.data(),alt.size(),&s)==BTH_OK&&s.found&&s.rfcomm_channel==7);
 // Channel 0 is not a server channel; a truncated element is malformed.
 const uint8_t channel_zero[]={0x35,0x13,0x35,0x11,0x09,0x00,0x04,0x35,0x0c,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x00};
 REQUIRE(bth_sdp_client_parse(channel_zero,sizeof channel_zero,&s)==BTH_OK&&!s.found);
 REQUIRE(bth_sdp_client_parse(channel_zero,sizeof channel_zero-1,&s)==BTH_ERR_LENGTH);
}
// MAS record (MAP 1.4) beside the PSE: the MCE request asks for MASInstanceID
// and SupportedMessageTypes too; the PBAP default list is unchanged.
const uint8_t mas_record[]={
 0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x0a, 0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x32,
 0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x11,0x35,0x03,0x19,0x00,0x08,
 0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x34,0x09,0x01,0x04, 0x09,0x02,0x00,0x09,0x10,0x29,
 0x09,0x03,0x15,0x08,0x02, 0x09,0x03,0x16,0x08,0x1f, 0x09,0x03,0x17,0x0a,0x00,0x00,0x00,0x00};
const uint8_t mas_attributes[]={0x35,18,0x09,0x00,0x04,0x09,0x00,0x09,0x09,0x02,0x00,0x09,0x03,0x15,0x09,0x03,0x16,0x09,0x03,0x17};
unsigned map_sdp_client(){
 const unsigned before=checks;
 bth_sdp_record_t records[]={{0x10009,{pse_record,sizeof pse_record}},{0x1000a,{mas_record,sizeof mas_record}}};
 bth_sdp_t server{};REQUIRE(bth_sdp_init(&server,records,2,9));
 uint8_t lists[640],req[64],rsp[700];size_t n=0,m=0;bth_sdp_client_t c;
 REQUIRE(bth_sdp_client_init(&c,0x1132,lists,sizeof lists,0x10)&&bth_sdp_client_set_attributes(&c,mas_attributes,sizeof mas_attributes));
 REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);
 const Bytes expected={0x06,0x00,0x11,0x00,0x1c,0x35,0x03,0x19,0x11,0x32,0x02,0x40,0x35,18,0x09,0x00,0x04,0x09,0x00,0x09,0x09,0x02,0x00,0x09,0x03,0x15,0x09,0x03,0x16,0x09,0x03,0x17,0x00};
 REQUIRE(Bytes(req,req+n)==expected);
 REQUIRE(!bth_sdp_client_set_attributes(&c,mas_attributes,sizeof mas_attributes)); // Only before the first request.
 REQUIRE(bth_sdp_process(&server,req,n,rsp,sizeof rsp,&m)==BTH_OK&&bth_sdp_client_response(&c,rsp,m)==BTH_OK);
 bth_sdp_service_t s;REQUIRE(bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&s.found&&s.rfcomm_channel==0x11&&s.profile_version==0x0104&&s.goep_l2cap_psm==0x1029);
 REQUIRE(s.has_mas_instance&&s.mas_instance_id==2&&s.supported_message_types==0x1f&&s.has_supported_features&&s.supported_features==0&&!s.supported_repositories);
 // Continuation across small responses gives the same result.
 bth_sdp_cursor_t cursor{};REQUIRE(bth_sdp_client_init(&c,0x1132,lists,sizeof lists,3)&&bth_sdp_client_set_attributes(&c,mas_attributes,sizeof mas_attributes));
 unsigned rounds=0;bth_status_t st=BTH_MORE;
 while(st==BTH_MORE){REQUIRE(bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK);REQUIRE(n>=33&&!memcmp(req+12,mas_attributes,sizeof mas_attributes));
  REQUIRE(bth_sdp_process_compact(&server,&cursor,req,n,rsp,24,&m)==BTH_OK);st=bth_sdp_client_response(&c,rsp,m);++rounds;REQUIRE(rounds<40);}
 REQUIRE(st==BTH_OK&&rounds>2&&bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&s.found&&s.mas_instance_id==2&&s.supported_message_types==0x1f);
 // The PSE lookup keeps the default list: no MAS attributes are returned.
 REQUIRE(bth_sdp_client_init(&c,0x112f,lists,sizeof lists,1)&&bth_sdp_client_request(&c,req,sizeof req,&n)==BTH_OK&&n==30);
 REQUIRE(bth_sdp_process(&server,req,n,rsp,sizeof rsp,&m)==BTH_OK&&bth_sdp_client_response(&c,rsp,m)==BTH_OK);
 REQUIRE(bth_sdp_client_parse(lists,c.used,&s)==BTH_OK&&s.found&&s.rfcomm_channel==0x13&&!s.has_mas_instance&&!s.supported_message_types&&s.has_supported_features&&s.supported_features==0x3ff);
 // Attribute list validation: a DES with an exact length, at most 32 bytes.
 const uint8_t bad_tag[]={0x36,0,3,0x09,0x00,0x04},bad_length[]={0x35,4,0x09,0x00,0x04};uint8_t big[34]={0x35,32};
 REQUIRE(bth_sdp_client_init(&c,0x1132,lists,sizeof lists,1));
 REQUIRE(!bth_sdp_client_set_attributes(&c,bad_tag,sizeof bad_tag)&&!bth_sdp_client_set_attributes(&c,bad_length,sizeof bad_length)&&!bth_sdp_client_set_attributes(&c,big,sizeof big));
 REQUIRE(!bth_sdp_client_set_attributes(&c,nullptr,0)&&!bth_sdp_client_set_attributes(nullptr,mas_attributes,sizeof mas_attributes));
 bth_sdp_client_t idle{};REQUIRE(!bth_sdp_client_set_attributes(&idle,mas_attributes,sizeof mas_attributes)); // Not initialized.
 // Wrong widths for the MAS attributes are ignored, not misread.
 const uint8_t odd[]={0x35,0x24,0x35,0x22,0x09,0x00,0x04,0x35,0x0c,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,0x05,
  0x09,0x03,0x15,0x09,0x00,0x01, 0x09,0x03,0x16,0x09,0x00,0x02, 0x09,0x03,0x17,0x08,0x01};
 REQUIRE(bth_sdp_client_parse(odd,sizeof odd,&s)==BTH_OK&&s.found&&s.rfcomm_channel==5&&!s.has_mas_instance&&!s.supported_message_types&&!s.has_supported_features);
 return checks-before;
}
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 writer_exact();utf16_names();parse_packets();fragmentation();sdp_client();fuzz();
 const unsigned map=map_sdp_client();std::printf("MAP_SDP_CLIENT_PASS checks=%u (MAS request/parse, attribute-list validation)\n",map);
 std::printf("OBEX_CODEC_SDP_CLIENT_PASS checks=%u (portable C core; no radio)\n",checks);return 0;
}
