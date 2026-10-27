#define private public
#include "owned/OwnedHost.h"
#undef private
#define main fixture_main
#include "test_owned_host.cpp"
#undef main
static const uint8_t old_value[]={0x71,0x72};
static void start(Fixture& f){f.boot();f.leConnect();f.acl(0x26,4,{0x12,12,0,1,0});f.settle();CHECK(f.host.bleStats().notify_enabled);f.output.clear();}
static unsigned notifications(const Fixture&f){unsigned n=0;for(const auto&p:f.output)if(p.cid==4&&!p.bytes.empty()&&p.bytes[0]==0x1b)++n;return n;}
static void readable(Fixture&f){f.output.clear();f.acl(0x26,4,{0x0a,11,0});f.settle();CHECK(!f.output.empty()&&f.output.back().bytes==Bytes({0x0b,0x71,0x72}));}
static void cancellation(){for(uint8_t op:{uint8_t(0x12),uint8_t(0x52)}){
 Fixture f;start(f);CHECK(f.host.bleNotify(old_value,2)==Status::Ok);CHECK(f.host.le_.notify_pending_);
 // Both commands can be queued without response. Request path uses an ordinary
 // completed disable transaction followed by a re-enable transaction.
 f.acl(0x26,4,{op,12,0,0,0});if(op==0x52)f.acl(0x26,4,{op,12,0,1,0});f.settle();
 CHECK(!f.host.le_.notify_pending_&&notifications(f)==0);
 if(op==0x12){CHECK(!f.host.bleStats().notify_enabled);f.acl(0x26,4,{op,12,0,1,0});f.settle();}
 CHECK(f.host.bleStats().notify_enabled&&notifications(f)==0);readable(f);
 const uint8_t fresh[]={8};CHECK(f.host.bleNotify(fresh,1)==Status::Ok);f.output.clear();f.settle();CHECK(notifications(f)==1);
 CHECK(f.host.radioStats().transport_errors==0);
}}
static void capacity_retry(){for(uint8_t op:{uint8_t(0x12),uint8_t(0x52)}){
 Fixture f;start(f);f.ready=false;
 for(unsigned i=0;i<3;++i){f.acl(0x26,4,{0x0a,3,0});f.poll();}
 CHECK(f.host.control_.peers[1].owned==3);CHECK(f.host.bleNotify(old_value,2)==Status::Ok);
 auto old_mtu=f.host.le_.att_.mtu;auto generation=f.host.bleStats().generation;
 f.acl(0x26,4,{op,12,0,0,0});f.poll();
 if(op==0x52){
  CHECK(!f.host.peers_[1].has_pending&&!f.host.le_.notify_pending_&&!f.host.bleStats().notify_enabled);
  CHECK(f.host.le_.att_.cccd[0]==0&&f.host.le_.att_.mtu==old_mtu&&f.host.bleStats().generation==generation);
  CHECK(f.host.le_.tx_length_==2&&!memcmp(f.host.le_.tx_value_,old_value,2));
  CHECK(f.host.control_.peers[1].owned==3&&f.host.bleStats().capacity_retries==0);
  f.ready=true;f.settle();CHECK(notifications(f)==0&&f.host.radioStats().transport_errors==0);readable(f);continue;
 }
 CHECK(!f.host.peers_[1].has_pending&&f.host.le_.pendingAtt()&&f.host.le_.notify_pending_&&f.host.bleStats().notify_enabled);
 CHECK(f.host.le_.att_.cccd[0]==1&&f.host.le_.att_.mtu==old_mtu&&f.host.bleStats().generation==generation);
 CHECK(f.host.le_.tx_length_==2&&!memcmp(f.host.le_.tx_value_,old_value,2));
 const auto retries=f.host.bleStats().capacity_retries;f.poll();CHECK(f.host.bleStats().capacity_retries>retries);
 CHECK(f.host.le_.notify_pending_&&f.host.le_.att_.cccd[0]==1);
 f.ready=true;f.settle();CHECK(!f.host.peers_[1].has_pending&&!f.host.le_.pendingAtt()&&!f.host.le_.notify_pending_&&!f.host.bleStats().notify_enabled);
 CHECK(notifications(f)==0&&f.host.radioStats().transport_errors==0);readable(f);
}}
static void unrelated_and_invalid(){for(auto p:{Bytes{0x12,13,0,0,0},Bytes{0x52,13,0,0,0},Bytes{0x12,12,0,2,0},Bytes{0x52,12,0,2,0},Bytes{0x12,12,0,1,0}}){
 Fixture f;start(f);CHECK(f.host.bleNotify(old_value,2)==Status::Ok);f.acl(0x26,4,p);f.settle();
 CHECK(f.host.bleStats().notify_enabled&&notifications(f)==1);CHECK(f.host.radioStats().transport_errors==0);
}}
static void committed_boundary(){Fixture f;start(f);CHECK(f.host.bleNotify(old_value,2)==Status::Ok);
 f.ready=false;f.poll();CHECK(!f.host.le_.notify_pending_&&f.host.bleStats().notifications_queued==1);
 CHECK(f.host.control_.peers[1].owned==1);f.acl(0x26,4,{0x52,12,0,0,0});f.poll();
 CHECK(!f.host.bleStats().notify_enabled&&f.host.control_.peers[1].owned==1);
 f.ready=true;f.settle();CHECK(notifications(f)==1);CHECK(f.output.back().bytes==Bytes({0x1b,11,0,0x71,0x72}));}
static void detach_generation(){Fixture f;start(f);const auto generation=f.host.bleStats().generation;
 CHECK(f.host.bleNotify(old_value,2)==Status::Ok);f.event(5,{0,0x26,0,0x13});f.settle();CHECK(!f.host.le_.notify_pending_&&!f.host.bleStats().connected);
 f.leConnect();CHECK(f.host.bleStats().generation!=generation&&!f.host.bleStats().notify_enabled);f.output.clear();
 f.acl(0x26,4,{0x12,12,0,1,0});f.settle();CHECK(notifications(f)==0);readable(f);}
int main(){cancellation();capacity_retry();unrelated_and_invalid();committed_boundary();detach_generation();printf("Pending application notify cancellation: %u actual actor checks passed\n",checks);}
