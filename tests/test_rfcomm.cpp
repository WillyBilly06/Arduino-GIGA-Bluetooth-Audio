// Portable behavior fixture. Compile-only is not a runtime result.
#if defined(__has_include)
# if __has_include("../src/owned/core/bth_rfcomm.h")
#  include "../src/owned/core/bth_rfcomm.h"
#  define HAVE_OWN_RFCOMM 1
# endif
#endif
#ifndef HAVE_OWN_RFCOMM
static_assert(false, "Missing bounded own RFCOMM transport");
#else
#include <cstring>
#include <cstdint>
namespace {
unsigned failures;
#define CHECK(x) do { if (!(x)) ++failures; } while (0)
bth_rfcomm_t a,b;
uint8_t ab[600],ba[600];size_t an,bn;
bth_rfcomm_event_t ev;
bth_status_t give(bth_rfcomm_t& to,const uint8_t* p,size_t n,uint8_t* out,size_t& w) {
 return bth_rfcomm_receive(&to,7,100,p,n,out,600,&w,&ev);
}
void session(bool credits) {
 CHECK(bth_rfcomm_init(&a,7,true,0,128,2)); CHECK(bth_rfcomm_init(&b,7,false,3,128,2));
 CHECK(bth_rfcomm_start(&a,7,0,ab,sizeof ab,&an)==BTH_OK);
 const uint8_t sabm[]={3,0x3f,1,0x1c}; CHECK(an==4&&!std::memcmp(ab,sabm,4));
 CHECK(give(b,ab,an,ba,bn)==BTH_OK&&b.mux==BTH_RFCOMM_OPEN);
 const uint8_t ua[]={3,0x73,1,0xd7}; CHECK(bn==4&&!std::memcmp(ba,ua,4));
 CHECK(give(a,ba,bn,ab,an)==BTH_OK&&a.mux==BTH_RFCOMM_OPEN);
 CHECK(bth_rfcomm_open(&a,7,3,credits,1,ab,sizeof ab,&an)==BTH_OK);
 size_t deferred=99;CHECK(bth_rfcomm_open(&a,7,4,credits,1,ba,sizeof ba,&deferred)==BTH_MORE&&!deferred); // first PN fixes session flow mode

 CHECK(give(b,ab,an,ba,bn)==BTH_OK); CHECK(give(a,ba,bn,ab,an)==BTH_OK);
 CHECK(bth_rfcomm_next(&a,7,2,ab,sizeof ab,&an)==BTH_OK&&an);
 CHECK(give(b,ab,an,ba,bn)==BTH_OK); CHECK(give(a,ba,bn,ab,an)==BTH_OK);
 CHECK(bth_rfcomm_next(&a,7,3,ab,sizeof ab,&an)==BTH_OK&&an);
 CHECK(give(b,ab,an,ba,bn)==BTH_OK); CHECK(give(a,ba,bn,ab,an)==BTH_OK);
 CHECK(!bth_rfcomm_ready(&a,7,6));
 CHECK(bth_rfcomm_expire(&a,7,4)==BTH_MORE); // our MSC ACK alone must not expire the peer half
 CHECK(bth_rfcomm_next(&b,7,4,ba,sizeof ba,&bn)==BTH_OK&&bn);
 CHECK(give(a,ba,bn,ab,an)==BTH_OK); CHECK(give(b,ab,an,ba,bn)==BTH_OK);
 CHECK(bth_rfcomm_ready(&a,7,6)&&bth_rfcomm_ready(&b,7,6));
}
}
unsigned rfcommSelftest() {
 failures=0; bth_rfcomm_frame_t v{};size_t n=0;uint8_t out[600]{};
 const uint8_t sabm[]={3,0x3f,1,0x1c}; CHECK(bth_rfcomm_decode(sabm,4,false,&v)==BTH_OK&&v.dlci==0&&v.pf);
 const uint8_t badFcs[]={3,0x3f,1,0x1d};CHECK(bth_rfcomm_decode(badFcs,4,false,&v)==BTH_ERR_STATE);
 const uint8_t badEa[]={2,0x3f,1,0x1c};CHECK(bth_rfcomm_decode(badEa,4,false,&v)==BTH_ERR_PARAM);
 const uint8_t tail[]={3,0x3f,1,0x1c,0};CHECK(bth_rfcomm_decode(tail,5,false,&v)==BTH_ERR_LENGTH);
 const uint8_t uih[]={3,0xef,1,0x70};CHECK(bth_rfcomm_decode(uih,4,false,&v)==BTH_OK);
 CHECK(bth_rfcomm_encode(0,true,BTH_RFCOMM_SABM,true,false,0,nullptr,0,out,3,&n)==BTH_ERR_CAPACITY&&!n);
 CHECK(bth_rfcomm_encode(0,true,BTH_RFCOMM_SABM,true,false,0,nullptr,0,out,sizeof out,&n)==BTH_OK&&n==4&&!std::memcmp(out,sabm,4));
 uint8_t longPayload[128]{};CHECK(bth_rfcomm_encode(6,true,BTH_RFCOMM_UIH,false,false,0,longPayload,128,out,sizeof out,&n)==BTH_OK&&out[2]==0&&out[3]==1);
 CHECK(bth_rfcomm_decode(out,n,false,&v)==BTH_OK&&v.payload.len==128);
 out[2]=2;out[3]=0;CHECK(bth_rfcomm_decode(out,n,false,&v)==BTH_ERR_LENGTH);
 session(true); const uint8_t payload[]={1,2,3};
 bth_rfcomm_t saved=a; CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,2,&an)==BTH_ERR_CAPACITY&&!an&&!std::memcmp(&a,&saved,sizeof a));
 CHECK(bth_rfcomm_send(&a,8,6,payload,3,ab,sizeof ab,&an)==BTH_IGNORED&&!an);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_OK);CHECK(give(b,ab,an,ba,bn)==BTH_OK&&ev.type==BTH_RFCOMM_DATA&&ev.payload.len==3);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_OK);CHECK(give(b,ab,an,ba,bn)==BTH_OK);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_MORE&&!an);
 CHECK(bth_rfcomm_release(&b,7,6,2)==BTH_OK);CHECK(bth_rfcomm_release(&b,7,6,1)==BTH_ERR_PARAM);
 CHECK(bth_rfcomm_next(&b,7,5,ba,sizeof ba,&bn)==BTH_OK&&bn);CHECK(give(a,ba,bn,ab,an)==BTH_OK);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_OK);
 CHECK(bth_rfcomm_disconnect(&a,7,6,6,ab,sizeof ab,&an)==BTH_OK);CHECK(give(b,ab,an,ba,bn)==BTH_OK);CHECK(give(a,ba,bn,ab,an)==BTH_OK);
 CHECK(!bth_rfcomm_ready(&a,7,6)&&!bth_rfcomm_ready(&b,7,6));
 bth_rfcomm_close(&a);CHECK(bth_rfcomm_release(&a,7,6,1)==BTH_IGNORED);
 session(false);
 CHECK(bth_rfcomm_set_flow(&b,7,6,true,ba,sizeof ba,&bn)==BTH_OK);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_OK);
 CHECK(give(b,ab,an,out,n)==BTH_OK&&ev.type==BTH_RFCOMM_DATA); // already in flight before FC reached peer
 CHECK(give(a,ba,bn,ab,an)==BTH_OK);CHECK(give(b,ab,an,ba,bn)==BTH_OK);
 CHECK(bth_rfcomm_send(&a,7,6,payload,3,ab,sizeof ab,&an)==BTH_MORE&&!an);
 session(true);
 const uint8_t pnTooSmall[]={3,0xef,0x15,0x83,0x11,6,0xf0,7,0,22,0,0,2,0x70};
 CHECK(give(b,pnTooSmall,sizeof pnTooSmall,ba,bn)==BTH_OK);CHECK(bth_rfcomm_decode(ba,bn,false,&v)==BTH_OK&&v.control==BTH_RFCOMM_DM&&!bth_rfcomm_ready(&b,7,6));
 CHECK(bth_rfcomm_encode(6,true,BTH_RFCOMM_SABM,true,false,0,nullptr,0,ab,sizeof ab,&an)==BTH_OK);CHECK(give(b,ab,an,ba,bn)==BTH_OK);CHECK(bth_rfcomm_decode(ba,bn,false,&v)==BTH_OK&&v.control==BTH_RFCOMM_DM);
 bth_rfcomm_close(&a);CHECK(bth_rfcomm_init(&a,7,true,0,128,2));a.mux=BTH_RFCOMM_OPEN;
 CHECK(bth_rfcomm_expire(&a,7,0xffffffffu)==BTH_MORE); // no transaction armed
 CHECK(bth_rfcomm_init(&a,9,true,0,128,2));CHECK(bth_rfcomm_start(&a,9,0xfffffff0u,ab,sizeof ab,&an)==BTH_OK);
 CHECK(bth_rfcomm_expire(&a,9,0x20u)==BTH_MORE);CHECK(bth_rfcomm_expire(&a,9,60000u)==BTH_OK&&a.mux==BTH_RFCOMM_CLOSED);
 return failures;
}
#ifdef GIGA_BREDR_PORTABLE_TEST_MAIN
int main(){return rfcommSelftest()?1:0;}
#endif
#endif
