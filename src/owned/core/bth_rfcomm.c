#include "bth_rfcomm.h"
#include <string.h>
// Independently written from RFCOMM1.2 and its dated ETSI07.10 reference.
enum { DLC_PN_WAIT=1,DLC_SABM_PENDING,DLC_SABM_WAIT,DLC_OPEN,DLC_DISC_WAIT,DLC_REFUSED,
 MODE_LEGACY=1,MODE_CREDIT=2,PN=0x80,MSC=0xe0,RPN=0x90,RLS=0x50,TEST=0x20,FCON=0xa0,FCOFF=0x60,NSC=0x10 };
static bool valid_dlci(uint8_t d){return d==0||(d>=2&&d<=61);}
static uint8_t fcs(const uint8_t* p,size_t n){uint8_t c=0xff;size_t i;unsigned b;for(i=0;i<n;++i){c^=p[i];for(b=0;b<8;++b)c=(uint8_t)((c>>1)^((c&1)?0xe0:0));}return (uint8_t)(0xff-c);}
static size_t length_size(size_t n){return n<128?1:2;}
static void put_length(uint8_t* p,size_t n){p[0]=(uint8_t)((n<<1)|(n<128?1:0));if(n>=128)p[1]=(uint8_t)(n>>7);}
static bth_status_t get_length(const uint8_t* p,size_t n,size_t* value,size_t* used){
 if(!n)return BTH_ERR_LENGTH;*used=1;*value=p[0]>>1;
 if(!(p[0]&1)){if(n<2)return BTH_ERR_LENGTH;*used=2;*value|=(size_t)p[1]<<7;if(*value<128)return BTH_ERR_LENGTH;}return BTH_OK;
}
bth_status_t bth_rfcomm_decode(const uint8_t* p,size_t n,bool credits,bth_rfcomm_frame_t* v){
 size_t len,ls,header;uint8_t control;bool has;
 if(!p||!v)return BTH_ERR_PARAM;if(n<4)return BTH_ERR_LENGTH;
 if(!(p[0]&1)||!valid_dlci(p[0]>>2))return BTH_ERR_PARAM;
 control=p[1]&0xef;if(control!=BTH_RFCOMM_UIH&&control!=BTH_RFCOMM_SABM&&control!=BTH_RFCOMM_UA&&control!=BTH_RFCOMM_DM&&control!=BTH_RFCOMM_DISC)return BTH_UNSUPPORTED;
 if(get_length(p+2,n-2,&len,&ls)!=BTH_OK)return BTH_ERR_LENGTH;
 header=2+ls;has=control==BTH_RFCOMM_UIH&&credits&&(p[0]>>2)!=0&&(p[1]&0x10);
 if(len>32767||n!=header+len+(has?1:0)+1)return BTH_ERR_LENGTH;
 if(control!=BTH_RFCOMM_UIH&&(len||(!(p[1]&0x10)&&control!=BTH_RFCOMM_DM)))return BTH_ERR_PARAM;
 if(control==BTH_RFCOMM_UIH&&(p[1]&0x10)&&!has)return BTH_ERR_PARAM;
 if(fcs(p,control==BTH_RFCOMM_UIH?2:header)!=p[n-1])return BTH_ERR_STATE;
 v->dlci=p[0]>>2;v->control=control;v->cr=(p[0]&2)!=0;v->pf=(p[1]&0x10)!=0;v->has_credit=has;v->credit=has?p[header]:0;
 v->payload.data=p+header+(has?1:0);v->payload.len=len;return BTH_OK;
}
bth_status_t bth_rfcomm_encode(uint8_t dlci,bool cr,uint8_t ctl,bool pf,bool has,uint8_t credit,const uint8_t* p,size_t n,uint8_t* out,size_t cap,size_t* written){
 size_t header=2+length_size(n),total=header+n+(has?1:0)+1;
 if(!written)return BTH_ERR_PARAM;*written=0;
 if(!out||(n&&!p)||!valid_dlci(dlci)||n>32767||(ctl!=BTH_RFCOMM_UIH&&ctl!=BTH_RFCOMM_SABM&&ctl!=BTH_RFCOMM_UA&&ctl!=BTH_RFCOMM_DM&&ctl!=BTH_RFCOMM_DISC))return BTH_ERR_PARAM;
 if((has&&(!pf||!dlci||ctl!=BTH_RFCOMM_UIH))||(ctl==BTH_RFCOMM_UIH&&pf&&!has)||(ctl!=BTH_RFCOMM_UIH&&(has||n||(!pf&&ctl!=BTH_RFCOMM_DM))))return BTH_ERR_PARAM;
 if(cap<total)return BTH_ERR_CAPACITY;
 out[0]=(uint8_t)((dlci<<2)|(cr?2:0)|1);out[1]=ctl|(pf?0x10:0);put_length(out+2,n);
 if(has)out[header]=credit;if(n)memcpy(out+header+(has?1:0),p,n);out[total-1]=fcs(out,ctl==BTH_RFCOMM_UIH?2:header);*written=total;return BTH_OK;
}
static bool live(const bth_rfcomm_t* s,uint32_t g){return s&&s->initialized&&g&&s->generation==g;}
static bth_rfcomm_dlc_t* find(bth_rfcomm_t* s,uint8_t d){unsigned i;for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i)if(s->dlc[i].state&&s->dlc[i].dlci==d)return &s->dlc[i];return 0;}
static bth_rfcomm_dlc_t* allocate(bth_rfcomm_t* s,uint8_t d){unsigned i;bth_rfcomm_dlc_t* c=find(s,d);if(c)return c;for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i)if(!s->dlc[i].state){c=&s->dlc[i];memset(c,0,sizeof *c);c->dlci=d;return c;}return 0;}
static bool incoming(const bth_rfcomm_t* s,uint8_t d){return d>=2&&((d&1)==(s->initiator?1:0))&&(s->listener_mask&(1u<<(d>>1)));}
static bth_status_t frame(bth_rfcomm_t* s,uint8_t d,uint8_t ctl,bool command,uint8_t* out,size_t cap,size_t* n){return bth_rfcomm_encode(d,command?s->initiator:!s->initiator,ctl,true,false,0,0,0,out,cap,n);}
static bth_status_t mcc(bth_rfcomm_t* s,uint8_t type,bool command,const uint8_t* p,size_t len,uint8_t* out,size_t cap,size_t* n){
 uint8_t data[BTH_RFCOMM_MTU_LIMIT];size_t header=1+length_size(len);if(len>sizeof data-header)return BTH_ERR_CAPACITY;
 data[0]=type|1|(command?2:0);put_length(data+1,len);if(len)memcpy(data+header,p,len);
 return bth_rfcomm_encode(0,s->initiator,BTH_RFCOMM_UIH,false,false,0,data,header+len,out,cap,n);
}
bool bth_rfcomm_init(bth_rfcomm_t* s,uint32_t g,bool initiator,uint8_t scn,uint16_t mtu,uint8_t window){
 if(!s||!g||scn>30||mtu<127||mtu>BTH_RFCOMM_MTU_LIMIT||!window||window>7)return false;
 memset(s,0,sizeof *s);s->generation=g;s->initiator=initiator;s->server_channel=scn;s->listener_mask=scn?1u<<scn:0;s->mtu=mtu;s->window=window;s->initialized=true;return true;
}
bth_status_t bth_rfcomm_add_listener(bth_rfcomm_t* s,uint32_t g,uint8_t scn){
 unsigned count=0,i;if(!live(s,g))return BTH_IGNORED;if(!scn||scn>30)return BTH_ERR_PARAM;
 if(s->mux!=BTH_RFCOMM_CLOSED)return BTH_ERR_STATE;if(s->listener_mask&(1u<<scn))return BTH_OK;
 for(i=1;i<=30;++i)if(s->listener_mask&(1u<<i))++count;if(count>=BTH_RFCOMM_DLC_LIMIT)return BTH_ERR_CAPACITY;
 s->listener_mask|=1u<<scn;return BTH_OK;
}
void bth_rfcomm_close(bth_rfcomm_t* s){if(s)memset(s,0,sizeof *s);}
bth_status_t bth_rfcomm_start(bth_rfcomm_t* s,uint32_t g,uint32_t now,uint8_t* out,size_t cap,size_t* n){
 bth_status_t st;if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;if(!s->initiator||s->mux!=BTH_RFCOMM_CLOSED)return BTH_ERR_STATE;
 st=frame(s,0,BTH_RFCOMM_SABM,true,out,cap,n);if(st==BTH_OK){s->mux=BTH_RFCOMM_CONNECTING;s->deadline=now+60000;}return st;
}
bth_status_t bth_rfcomm_open(bth_rfcomm_t* s,uint32_t g,uint8_t scn,bool credits,uint32_t now,uint8_t* out,size_t cap,size_t* n){
 bth_rfcomm_t copy;bth_rfcomm_dlc_t* c;uint8_t p[8]={0};bth_status_t st;
 if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;if(bth_rfcomm_expire(s,g,now)==BTH_OK)return BTH_IGNORED;if(s->mux!=BTH_RFCOMM_OPEN||!scn||scn>30)return BTH_ERR_PARAM;
 p[0]=(uint8_t)(scn*2+(s->initiator?0:1));if(find(s,p[0]))return BTH_ERR_STATE;if(s->mode==MODE_CREDIT&&!credits)return BTH_ERR_STATE;
 if(!s->mode){unsigned i;for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i)if(s->dlc[i].state)return BTH_MORE;}
 copy=*s;c=allocate(&copy,p[0]);if(!c)return BTH_ERR_CAPACITY;c->state=DLC_PN_WAIT;c->mtu=s->mtu;c->credit=credits&&s->mode!=MODE_LEGACY;c->rx_credits=c->credit?s->window:0;c->deadline=now+60000;
 p[1]=c->credit?0xf0:0;p[2]=7;p[4]=(uint8_t)c->mtu;p[5]=(uint8_t)(c->mtu>>8);p[7]=c->rx_credits;
 st=mcc(&copy,PN,true,p,8,out,cap,n);if(st==BTH_OK)*s=copy;return st;
}
bool bth_rfcomm_ready(const bth_rfcomm_t* s,uint32_t g,uint8_t dlci){unsigned i;if(!live(s,g)||s->mux!=BTH_RFCOMM_OPEN)return false;for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i){const bth_rfcomm_dlc_t* c=&s->dlc[i];if(c->dlci==dlci&&c->state==DLC_OPEN)return c->msc_received&&c->msc_acked;}return false;}
static void event(bth_rfcomm_t* s,bth_rfcomm_event_t* e,bth_rfcomm_event_type_t t,uint8_t d){e->type=t;e->generation=s->generation;e->dlci=d;}
static bth_status_t reject_dlc(bth_rfcomm_t* s,uint8_t d,uint8_t* out,size_t cap,size_t* n,bth_rfcomm_event_t* e){
 bth_status_t st;if(!d||!valid_dlci(d))return BTH_ERR_PARAM;st=frame(s,d,BTH_RFCOMM_DM,false,out,cap,n);if(st==BTH_OK&&incoming(s,d)){bth_rfcomm_dlc_t* c=allocate(s,d);if(c){bool active=c->state==DLC_OPEN;memset(c,0,sizeof *c);c->dlci=d;c->state=DLC_REFUSED;if(active)event(s,e,BTH_RFCOMM_DLC_CLOSED,d);}}return st;
}
static bth_status_t receive_mcc(bth_rfcomm_t* s,uint32_t now,bth_span_t m,uint8_t* out,size_t cap,size_t* n,bth_rfcomm_event_t* e){
 size_t len,ls;uint8_t type;bool command;const uint8_t* p;bth_rfcomm_dlc_t* c;bth_status_t st;uint8_t reply[8];
 if(m.len<2||!(m.data[0]&1))return BTH_ERR_LENGTH;
 if(get_length(m.data+1,m.len-1,&len,&ls)!=BTH_OK||1+ls+len!=m.len)return BTH_ERR_LENGTH;
 type=m.data[0]&0xfc;command=(m.data[0]&2)!=0;p=m.data+1+ls;
 if(type==PN){
  uint8_t d;uint16_t mtu;bool credit;if(len!=8)return BTH_ERR_LENGTH;d=p[0];mtu=bth_read_le16(p+4);if(!valid_dlci(d)||!d)return BTH_ERR_PARAM;c=find(s,d);if(mtu<23)return command?reject_dlc(s,d,out,cap,n,e):BTH_ERR_PARAM;
  if(!command){if(!c||c->state!=DLC_PN_WAIT)return BTH_IGNORED;if(mtu>c->mtu)return BTH_ERR_PARAM;credit=(p[1]>>4)==14&&c->credit;
   if(s->mode&&credit!=(s->mode==MODE_CREDIT))return BTH_ERR_STATE;c->credit=credit;c->mtu=mtu;c->tx_credits=credit?(p[7]&7):0;c->rx_credits=credit?s->window:0;c->state=DLC_SABM_PENDING;c->deadline=now+60000;return BTH_OK;}
  if(!incoming(s,d)||(!c&&s->mode==MODE_CREDIT&&(p[1]>>4)!=15)||(c&&c->state!=DLC_OPEN))return frame(s,d,BTH_RFCOMM_DM,false,out,cap,n);
  if(!c){if(!s->mode){unsigned i;for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i)if(s->dlc[i].state)return frame(s,d,BTH_RFCOMM_DM,false,out,cap,n);}
   c=allocate(s,d);if(!c)return frame(s,d,BTH_RFCOMM_DM,false,out,cap,n);c->state=DLC_SABM_PENDING;c->mtu=mtu<s->mtu?mtu:s->mtu;c->credit=(p[1]>>4)==15&&s->mode!=MODE_LEGACY;c->tx_credits=c->credit?(p[7]&7):0;c->rx_credits=c->credit?s->window:0;c->deadline=now+60000;}
  // On an open DLC, report current values; never reapply initial credits.
  memset(reply,0,8);reply[0]=d;reply[1]=c->state==DLC_OPEN?0:(c->credit?0xe0:0);reply[2]=p[2]&63;reply[4]=(uint8_t)c->mtu;reply[5]=(uint8_t)(c->mtu>>8);reply[7]=c->state==DLC_OPEN?0:c->rx_credits;
  if(c->mtu>mtu)return reject_dlc(s,d,out,cap,n,e);
  return mcc(s,PN,false,reply,8,out,cap,n);
 }
 if(type==MSC){uint8_t d;if((len!=2&&len!=3)||!(p[0]&1))return BTH_ERR_LENGTH;d=p[0]>>2;c=find(s,d);if(!c||c->state!=DLC_OPEN)return command?reject_dlc(s,d,out,cap,n,e):BTH_IGNORED;
  if(command){c->msc_received=true;if(c->msc_acked)c->deadline=0;if(!c->credit)c->peer_flow_off=(p[1]&2)!=0;return mcc(s,MSC,false,p,len,out,cap,n);}
  if(!c->msc_sent)return BTH_IGNORED;c->msc_acked=true;if(c->msc_received)c->deadline=0;return BTH_OK;
 }
 if(type==TEST){if(!command)return BTH_IGNORED;return mcc(s,TEST,false,p,len,out,cap,n);}
 if(type==FCON||type==FCOFF){if(len)return BTH_ERR_LENGTH;if(!command)return BTH_IGNORED;if(s->mode==MODE_CREDIT){uint8_t unsupported=m.data[0];return mcc(s,NSC,false,&unsupported,1,out,cap,n);}s->peer_flow_off=type==FCOFF;return mcc(s,type,false,0,0,out,cap,n);}
 if(type==RLS){if(len!=2||!(p[0]&1))return BTH_ERR_LENGTH;if(!find(s,p[0]>>2))return command?reject_dlc(s,p[0]>>2,out,cap,n,e):BTH_IGNORED;if(!command)return BTH_IGNORED;event(s,e,BTH_RFCOMM_LINE_STATUS,p[0]>>2);e->status=p[1];return mcc(s,RLS,false,p,2,out,cap,n);}
 if(type==RPN){if(len!=1&&len!=8)return BTH_ERR_LENGTH;if(!command)return BTH_IGNORED;if(!(p[0]&1))return BTH_ERR_PARAM;if(!find(s,p[0]>>2))return reject_dlc(s,p[0]>>2,out,cap,n,e);
  // Type1 virtual port, fixed9600/8N1; proposals rejected explicitly (mask0).
  reply[0]=p[0];reply[1]=3;reply[2]=3;reply[3]=0;reply[4]=0x11;reply[5]=0x13;reply[6]=reply[7]=0;return mcc(s,RPN,false,reply,8,out,cap,n);
 }
 if(type==NSC)return command?BTH_ERR_PARAM:BTH_IGNORED;
 if(!command)return BTH_IGNORED;{uint8_t unsupported=m.data[0];st=mcc(s,NSC,false,&unsupported,1,out,cap,n);return st;}
}
static bth_status_t receive_impl(bth_rfcomm_t* s,uint32_t now,const bth_rfcomm_frame_t* v,uint8_t* out,size_t cap,size_t* n,bth_rfcomm_event_t* e){
 bth_rfcomm_dlc_t* c;bool remote_command_cr=!s->initiator;bth_status_t st;
 if(v->control==BTH_RFCOMM_UIH){if(v->cr!=remote_command_cr||s->mux!=BTH_RFCOMM_OPEN)return BTH_IGNORED;if(!v->dlci)return receive_mcc(s,now,v->payload,out,cap,n,e);
  c=find(s,v->dlci);if(!c||c->state!=DLC_OPEN)return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);
  if(!bth_rfcomm_ready(s,s->generation,v->dlci))return BTH_ERR_STATE;
  if(v->payload.len>c->mtu-(v->has_credit?1u:0u))return BTH_ERR_LENGTH;
  if(v->has_credit){if(!c->credit||c->tx_credits>65535u-v->credit)return BTH_ERR_STATE;c->tx_credits=(uint16_t)(c->tx_credits+v->credit);}
  if(v->payload.len){if(c->credit){if(!c->rx_credits)return BTH_ERR_STATE;--c->rx_credits;}event(s,e,BTH_RFCOMM_DATA,v->dlci);e->payload=v->payload;}return BTH_OK;
 }
 if(v->control==BTH_RFCOMM_SABM||v->control==BTH_RFCOMM_DISC){if(v->cr!=remote_command_cr)return BTH_IGNORED;
  if(!v->dlci){if(v->control==BTH_RFCOMM_SABM){if(s->initiator||s->mux!=BTH_RFCOMM_CLOSED)return BTH_ERR_STATE;st=frame(s,0,BTH_RFCOMM_UA,false,out,cap,n);if(st==BTH_OK)s->mux=BTH_RFCOMM_OPEN;return st;}
   st=frame(s,0,s->mux==BTH_RFCOMM_OPEN?BTH_RFCOMM_UA:BTH_RFCOMM_DM,false,out,cap,n);if(st==BTH_OK){s->mux=BTH_RFCOMM_CLOSED;memset(s->dlc,0,sizeof s->dlc);s->mode=0;s->peer_flow_off=false;event(s,e,BTH_RFCOMM_MUX_CLOSED,0);}return st;}
  if(s->mux!=BTH_RFCOMM_OPEN)return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);c=find(s,v->dlci);
  if(v->control==BTH_RFCOMM_DISC){st=frame(s,v->dlci,c&&c->state==DLC_OPEN?BTH_RFCOMM_UA:BTH_RFCOMM_DM,false,out,cap,n);if(st==BTH_OK&&c){memset(c,0,sizeof *c);event(s,e,BTH_RFCOMM_DLC_CLOSED,v->dlci);}return st;}
  if(!incoming(s,v->dlci))return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);
  if(!c){if(s->mode==MODE_CREDIT)return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);c=allocate(s,v->dlci);if(!c)return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);c->mtu=127;c->credit=false;}
  if(c->state==DLC_REFUSED)return frame(s,v->dlci,BTH_RFCOMM_DM,false,out,cap,n);
  if(c->state==DLC_OPEN)return frame(s,v->dlci,BTH_RFCOMM_UA,false,out,cap,n);
  st=frame(s,v->dlci,BTH_RFCOMM_UA,false,out,cap,n);if(st==BTH_OK){c->state=DLC_OPEN;c->deadline=now+60000;if(!s->mode)s->mode=c->credit?MODE_CREDIT:MODE_LEGACY;event(s,e,BTH_RFCOMM_DLC_OPENED,v->dlci);}return st;
 }
 if(v->cr==remote_command_cr)return BTH_IGNORED;
 if(!v->dlci){if(v->control==BTH_RFCOMM_DM||(v->control==BTH_RFCOMM_UA&&s->mux==BTH_RFCOMM_DISCONNECTING)){s->mux=BTH_RFCOMM_CLOSED;memset(s->dlc,0,sizeof s->dlc);s->mode=0;event(s,e,BTH_RFCOMM_MUX_CLOSED,0);return BTH_OK;}
  if(v->control==BTH_RFCOMM_UA&&s->mux==BTH_RFCOMM_CONNECTING){s->mux=BTH_RFCOMM_OPEN;s->deadline=0;return BTH_OK;}return BTH_IGNORED;}
 c=find(s,v->dlci);if(!c)return BTH_IGNORED;
 if(v->control==BTH_RFCOMM_DM||(v->control==BTH_RFCOMM_UA&&c->state==DLC_DISC_WAIT)){memset(c,0,sizeof *c);event(s,e,BTH_RFCOMM_DLC_CLOSED,v->dlci);return BTH_OK;}
 if(v->control==BTH_RFCOMM_UA&&c->state==DLC_SABM_WAIT){c->state=DLC_OPEN;c->deadline=now+60000;if(!s->mode)s->mode=c->credit?MODE_CREDIT:MODE_LEGACY;event(s,e,BTH_RFCOMM_DLC_OPENED,v->dlci);return BTH_OK;}return BTH_IGNORED;
}
bth_status_t bth_rfcomm_receive(bth_rfcomm_t* s,uint32_t g,uint32_t now,const uint8_t* p,size_t len,uint8_t* out,size_t cap,size_t* n,bth_rfcomm_event_t* e){
 bth_rfcomm_t copy;bth_rfcomm_frame_t v;bth_rfcomm_event_t result;bth_status_t st;
 if(!n||!e)return BTH_ERR_PARAM;*n=0;memset(e,0,sizeof *e);if(!live(s,g))return BTH_IGNORED;if(bth_rfcomm_expire(s,g,now)==BTH_OK)return BTH_IGNORED;
 st=bth_rfcomm_decode(p,len,s->mode==MODE_CREDIT,&v);if(st!=BTH_OK)return st;copy=*s;memset(&result,0,sizeof result);st=receive_impl(&copy,now,&v,out,cap,n,&result);
 if(st>=BTH_OK){*s=copy;*e=result;}else *n=0;return st;
}
bth_status_t bth_rfcomm_next(bth_rfcomm_t* s,uint32_t g,uint32_t now,uint8_t* out,size_t cap,size_t* n){
 unsigned i;bth_status_t st;uint8_t p[2];if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;if(bth_rfcomm_expire(s,g,now)==BTH_OK)return BTH_IGNORED;if(s->mux!=BTH_RFCOMM_OPEN)return BTH_MORE;
 for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i){bth_rfcomm_dlc_t* c=&s->dlc[i];
  // A remote PN responder waits for SABM, never initiates it itself.
  if(c->state==DLC_SABM_PENDING&&!incoming(s,c->dlci)){st=frame(s,c->dlci,BTH_RFCOMM_SABM,true,out,cap,n);if(st==BTH_OK){c->state=DLC_SABM_WAIT;c->deadline=now+60000;}return st;}
  if(c->state==DLC_OPEN&&!c->msc_sent){p[0]=(uint8_t)((c->dlci<<2)|3);p[1]=0x8d|(c->local_flow_off?2:0);st=mcc(s,MSC,true,p,2,out,cap,n);if(st==BTH_OK){c->msc_sent=true;c->deadline=now+60000;}return st;}
  if(c->state==DLC_OPEN&&c->credit&&c->grant_pending&&bth_rfcomm_ready(s,g,c->dlci)){st=bth_rfcomm_encode(c->dlci,s->initiator,BTH_RFCOMM_UIH,true,true,c->grant_pending,0,0,out,cap,n);if(st==BTH_OK){c->rx_credits=(uint8_t)(c->rx_credits+c->grant_pending);c->grant_pending=0;}return st;}
 }return BTH_OK;
}
bth_status_t bth_rfcomm_send(bth_rfcomm_t* s,uint32_t g,uint8_t d,const uint8_t* p,size_t len,uint8_t* out,size_t cap,size_t* n){
 bth_rfcomm_dlc_t* c;bth_status_t st;if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;if(!bth_rfcomm_ready(s,g,d))return BTH_ERR_STATE;c=find(s,d);if(len>c->mtu)return BTH_ERR_LENGTH;
 if(len&&((c->credit&&!c->tx_credits)||(!c->credit&&(s->peer_flow_off||c->peer_flow_off))))return BTH_MORE;
 st=bth_rfcomm_encode(d,s->initiator,BTH_RFCOMM_UIH,false,false,0,p,len,out,cap,n);if(st==BTH_OK&&len&&c->credit)--c->tx_credits;return st;
}
bth_status_t bth_rfcomm_release(bth_rfcomm_t* s,uint32_t g,uint8_t d,uint8_t count){bth_rfcomm_dlc_t* c;if(!live(s,g))return BTH_IGNORED;c=find(s,d);if(!c||c->state!=DLC_OPEN||!c->credit)return BTH_ERR_STATE;
 if(!count||count>s->window-c->rx_credits-c->grant_pending)return BTH_ERR_PARAM;c->grant_pending=(uint8_t)(c->grant_pending+count);return BTH_OK;}
bth_status_t bth_rfcomm_set_flow(bth_rfcomm_t* s,uint32_t g,uint8_t d,bool off,uint8_t* out,size_t cap,size_t* n){bth_rfcomm_dlc_t* c;bth_status_t st;uint8_t p[2];if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;c=find(s,d);if(!c||c->state!=DLC_OPEN)return BTH_ERR_STATE;if(c->credit)return BTH_UNSUPPORTED;
 p[0]=(uint8_t)((d<<2)|3);p[1]=0x8d|(off?2:0);st=mcc(s,MSC,true,p,2,out,cap,n);if(st==BTH_OK)c->local_flow_off=off;return st;}
bth_status_t bth_rfcomm_disconnect(bth_rfcomm_t* s,uint32_t g,uint8_t d,uint32_t now,uint8_t* out,size_t cap,size_t* n){bth_rfcomm_dlc_t* c;bth_status_t st;if(!n)return BTH_ERR_PARAM;*n=0;if(!live(s,g))return BTH_IGNORED;if(bth_rfcomm_expire(s,g,now)==BTH_OK)return BTH_IGNORED;if(s->mux!=BTH_RFCOMM_OPEN)return BTH_ERR_STATE;c=d?find(s,d):0;if(d&&(!c||c->state!=DLC_OPEN))return BTH_ERR_STATE;
 st=frame(s,d,BTH_RFCOMM_DISC,true,out,cap,n);if(st==BTH_OK){if(d){c->state=DLC_DISC_WAIT;c->deadline=now+60000;}else{s->mux=BTH_RFCOMM_DISCONNECTING;s->deadline=now+60000;}}return st;}
bth_status_t bth_rfcomm_expire(bth_rfcomm_t* s,uint32_t g,uint32_t now){unsigned i;if(!live(s,g))return BTH_IGNORED;
 // RFCOMM replies do not carry a session token. A timed-out PN/SABM/MSC/DISC
 // cannot be distinguished from a late reply to a replacement DLC transaction.
 // Retire the entire mux; the outer owner must close L2CAP and use a fresh generation.
 if((s->mux==BTH_RFCOMM_CONNECTING||s->mux==BTH_RFCOMM_DISCONNECTING)&&(int32_t)(now-s->deadline)>=0){bth_rfcomm_close(s);return BTH_OK;}
 for(i=0;i<BTH_RFCOMM_DLC_LIMIT;++i){bth_rfcomm_dlc_t* c=&s->dlc[i];if(c->state&&c->state!=DLC_REFUSED&&(c->state!=DLC_OPEN||!c->msc_received||!c->msc_acked)&&(int32_t)(now-c->deadline)>=0){bth_rfcomm_close(s);return BTH_OK;}}
 return BTH_MORE;
}
