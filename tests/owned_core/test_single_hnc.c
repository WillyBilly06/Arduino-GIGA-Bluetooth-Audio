#include "bth_rx_flow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void begin(bth_rx_flow_t* r) {CHECK(bth_rx_flow_init(r,8,4));CHECK(bth_rx_flow_attach(r,11,100));CHECK(bth_rx_flow_attach(r,64,200));}
static bth_rx_lease_t receive(bth_rx_flow_t* r,unsigned slot,unsigned h,unsigned owner) {bth_rx_lease_t l;CHECK(bth_rx_flow_received(r,(uint8_t)slot,(uint16_t)h,owner,&l)==BTH_OK);return l;}
static void done(bth_rx_flow_t* r,unsigned slot,unsigned h,unsigned owner) {bth_rx_lease_t l=receive(r,slot,h,owner);CHECK(bth_rx_flow_release(r,l)==BTH_OK);CHECK(bth_rx_flow_release(r,l)==BTH_ERR_STATE);}
static unsigned ack(bth_rx_flow_t* r,unsigned* count) {uint8_t out[16];size_t n=0;CHECK(bth_rx_flow_next_completed(r,out,sizeof out,&n)==BTH_OK);CHECK(n==9 && out[0]==1 && out[1]==0x35 && out[2]==0x0c && out[3]==5 && out[4]==1);*count=bth_read_le16(out+7);return bth_read_le16(out+5);}
static void one_pair_and_rollback(void) {
 bth_rx_flow_t r;begin(&r);done(&r,0,11,100);done(&r,1,64,200);
 const bth_rx_flow_t before=r;uint8_t out[16];size_t n;
 for(size_t cap=0;cap<9;++cap) {memset(out,0xa5,sizeof out);n=77;CHECK(bth_rx_flow_next_completed(&r,out,cap,&n)==BTH_ERR_CAPACITY && !n);CHECK(!memcmp(&r,&before,sizeof r));for(unsigned i=0;i<sizeof out;++i)CHECK(out[i]==0xa5);}
 CHECK(bth_rx_flow_next_completed(&r,NULL,sizeof out,&n)==BTH_ERR_PARAM && !n && !memcmp(&r,&before,sizeof r));
 CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,NULL)==BTH_ERR_PARAM && !memcmp(&r,&before,sizeof r));
 unsigned count;CHECK(ack(&r,&count)==11 && count==1 && r.outstanding==1);CHECK(r.links[0].pending==0 && r.links[1].pending==1);
 CHECK(ack(&r,&count)==64 && count==1 && r.outstanding==0);CHECK(r.links[1].pending==0);
 const bth_rx_flow_t empty=r;CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,&n)==BTH_MORE && !n && !memcmp(&r,&empty,sizeof r));
}
static void finite_fairness(void) {
 bth_rx_flow_t r;begin(&r);unsigned total[2]={0},received[2]={2,2},count;
 done(&r,0,11,100);done(&r,1,11,100);done(&r,2,64,200);done(&r,3,64,200);
 for(unsigned i=0;i<1000;++i) {
  unsigned h=ack(&r,&count),side=h==11?0:1;CHECK(h==(i%2?64u:11u) && count==2);total[side]+=count;CHECK(total[side]<=received[side]);
  CHECK(r.outstanding==2 && r.links[1-side].pending==2);
  // Keep the chosen peer continuously eligible too. Its traffic must not
  // starve the other peer, whose real pending credits remain reserved.
  done(&r,0,h,side?200:100);done(&r,1,h,side?200:100);received[side]+=2;
  CHECK(r.outstanding==4);bth_rx_lease_t blocked;CHECK(bth_rx_flow_received(&r,4,11,100,&blocked)==BTH_ERR_CAPACITY);
 }
 for(unsigned i=0;i<2;++i) {unsigned h=ack(&r,&count),side=h==11?0:1;CHECK(count==2);total[side]+=count;}
 CHECK(total[0]==received[0] && total[1]==received[1] && !r.outstanding);
 // A sole pending link progresses despite a cursor pointing at the other.
 done(&r,0,11,100);CHECK(ack(&r,&count)==11 && count==1);
 done(&r,0,11,100);done(&r,1,64,200);CHECK(ack(&r,&count)==64 && count==1);CHECK(ack(&r,&count)==11 && count==1);
}
static void retained_disconnect_and_reuse(void) {
 bth_rx_flow_t r; bth_rx_admission_t incoming; bth_rx_lease_t old[4],fresh[4];unsigned count;size_t n;uint8_t out[16];
 CHECK(bth_rx_flow_init(&r,8,4));CHECK(bth_rx_flow_attach(&r,11,100));CHECK(bth_rx_flow_reserve(&r,0,&incoming));
 for(unsigned i=0;i<4;++i)old[i]=receive(&r,i,11,100);
 CHECK(bth_rx_flow_detach(&r,11,100) && !r.outstanding);CHECK(bth_rx_flow_attach_reserved(&r,incoming,11,101));
 for(unsigned i=0;i<4;++i)fresh[i]=receive(&r,i+4,11,101);
 for(unsigned i=0;i<4;++i)CHECK(bth_rx_flow_release(&r,old[i])==BTH_IGNORED);
 CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,&n)==BTH_MORE && !n && r.outstanding==4);
 CHECK(bth_rx_flow_release(&r,fresh[0])==BTH_OK);CHECK(bth_rx_flow_release(&r,fresh[1])==BTH_OK);
 CHECK(ack(&r,&count)==11 && count==2 && r.outstanding==2);
 CHECK(bth_rx_flow_release(&r,old[0])==BTH_ERR_STATE);CHECK(bth_rx_flow_detach(&r,11,101));
 CHECK(bth_rx_flow_release(&r,fresh[2])==BTH_IGNORED);CHECK(bth_rx_flow_release(&r,fresh[3])==BTH_IGNORED);
 CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,&n)==BTH_MORE && !n && !r.outstanding);
 CHECK(bth_rx_flow_attach(&r,11,102));done(&r,0,11,102);CHECK(ack(&r,&count)==11 && count==1);
}
static void invalid_state_no_commit(void) {
 bth_rx_flow_t r;begin(&r);done(&r,0,11,100);done(&r,1,64,200);uint8_t out[16];size_t n;
 r.links[1].pending=2;const bth_rx_flow_t bad=r;CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,&n)==BTH_ERR_STATE && !n && !memcmp(&r,&bad,sizeof r));
 begin(&r);done(&r,0,11,100);done(&r,1,64,200);r.outstanding=1;const bth_rx_flow_t global=r;
 CHECK(bth_rx_flow_next_completed(&r,out,sizeof out,&n)==BTH_ERR_STATE && !n && !memcmp(&r,&global,sizeof r));
}
int main(void) {one_pair_and_rollback();finite_fairness();retained_disconnect_and_reuse();invalid_state_no_commit();printf("Single-pair HNC transactional/fairness/window/generation %u checks PASS; rx=%zu\n",checks,sizeof(bth_rx_flow_t));}
