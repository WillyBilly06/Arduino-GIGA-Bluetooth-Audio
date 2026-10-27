#include "bth_rfcomm_link.h"
static void put16(uint8_t* p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
bth_status_t bth_rfcomm_link_receive(bth_l2cap_sig_t* s,const uint8_t* in,size_t length,bool secure,bool recovering,uint8_t* out,size_t capacity,size_t* written,bool* collision){
 bth_l2cap_sig_t next;size_t at=0,used=0;bool collided=false;
 if(written)*written=0;if(collision)*collision=false;
 if(!s||!s->initialized||(!in&&length)||!out||!written||!collision)return BTH_ERR_PARAM;
 if(length>BTH_SIGNAL_MTU)return BTH_ERR_LENGTH;next=*s;
 while(at<length){
  const uint8_t* p=in+at;size_t n,bytes=0;bool rejected=false,one_collision=false;
  if(length-at<4||!p[1])return BTH_ERR_LENGTH;n=bth_read_le16(p+2);if(n>length-at-4)return BTH_ERR_LENGTH;
  if(p[0]==2&&n==4&&bth_read_le16(p+4)==3&&bth_read_le16(p+6)>=0x40){
   unsigned i;bool active=false,source_in_use=false;const uint16_t source=bth_read_le16(p+6);
   for(i=0;i<BTH_CHANNEL_LIMIT;++i)if(next.channels[i].state!=BTH_CHANNEL_CLOSED){active|=next.channels[i].psm==3;source_in_use|=next.channels[i].remote_cid==source;}
   if(active&&!source_in_use){
    if(capacity-used<12)return BTH_ERR_CAPACITY;out[used]=3;out[used+1]=p[1];put16(out+used+2,8);put16(out+used+4,0);put16(out+used+6,source);put16(out+used+8,4);put16(out+used+10,0);used+=12;rejected=true;
   }
  }
  if(!rejected){
   bth_status_t status=bth_avrcp_link_receive(&next,p,n+4,secure,recovering,out+used,capacity-used,&bytes,&one_collision);
   if(status!=BTH_OK)return status;used+=bytes;collided|=one_collision;
  }
  at+=n+4;
 }
 *s=next;*written=used;*collision=collided;return BTH_OK;
}
