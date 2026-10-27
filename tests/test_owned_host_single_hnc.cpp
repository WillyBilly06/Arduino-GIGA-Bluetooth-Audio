// Reuse the maintained actual actor fixture, driving real Classic/LE receive
// ownership and the credit-exempt HNC boundary under transport pressure.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_fixture_main
#include "test_owned_host.cpp"
#undef main
#undef CHECK
#define CHECK(x) do { ++checks;if(!(x)){fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);exit(1);} } while(0)

static void pending_both(Fixture& f) {
 f.boot();f.classicConnect();f.leConnect();f.ready=false;
 f.acl(0x25,0x7fff,{0xaa});f.acl(0x26,4,{0x0a,3,0});f.poll();
 CHECK(f.host.rx_.outstanding==2 && f.host.rx_.links[0].pending==1 && f.host.rx_.links[1].pending==1);
}
static void wire(const Bytes& p,unsigned handle) {CHECK(p.size()==9 && p[0]==1 && Fixture::op(p)==0x0c35 && p[4]==1 && bth_read_le16(p.data()+5)==handle && bth_read_le16(p.data()+7)==1);}
int main() {
 {Fixture f;pending_both(f);CHECK(f.tx.empty());f.host.flow_.command_credits=0;f.ready=true;
  f.host.serviceReceiveCredits(++f.now);wire(f.pop(),0x25);CHECK(f.host.rx_.outstanding==1 && f.host.rx_.links[1].pending==1 && f.host.flow_.command_credits==0);
  f.host.serviceReceiveCredits(++f.now);wire(f.pop(),0x26);CHECK(f.host.rx_.outstanding==0 && f.host.flow_.command_credits==0);f.settle();CHECK(!f.host.radioStats().transport_errors);
 }
 {Fixture f;pending_both(f);const auto held=f.host.rx_;for(unsigned i=0;i<10;++i)f.host.serviceReceiveCredits(++f.now);CHECK(f.tx.empty() && !memcmp(&held,&f.host.rx_,sizeof held));
  f.event(5,{0,0x26,0,0x13});f.poll();CHECK(f.host.rx_.outstanding==1 && !f.host.rx_.links[1].active);f.ready=true;
  f.host.serviceReceiveCredits(++f.now);wire(f.pop(),0x25);f.host.serviceReceiveCredits(++f.now);CHECK(f.tx.empty() && !f.host.rx_.outstanding);f.settle();f.leConnect();CHECK(f.host.bleStats().connected && !f.host.radioStats().transport_errors);
 }
 printf("Actual actor single-pair HNC pending/TXpressure/credit/disconnect %u checks PASS\n",checks);
}
