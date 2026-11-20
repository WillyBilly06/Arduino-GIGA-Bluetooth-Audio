#include <cstddef>
#define private public
#include "owned/OwnedHost.h"
#undef private
#define main original_host_fixture_main
#include "test_owned_host.cpp"
#undef main
#include <stdexcept>
#include "platform/TxMailbox.h"
using Result=BondStorageResult;
static const uint8_t PeerHci[6]={1,2,3,4,5,6},LocalDisplay[6]={0x42,0xd3,0x50,0x12,0x1e,0x65},LocalHci[6]={0x65,0x1e,0x12,0x50,0xd3,0x42};
struct Store {
 ClassicBondRecord record;bool present=false;unsigned loads=0,saves=0,removes=0;Result loadResult=Result::Ok,saveResult=Result::Ok,removeResult=Result::Ok;unsigned corrupt=0,reenter=0;bool throws=false;Fixture* fixture=nullptr;
 ClassicBondStorage provider(){return {this,load,save,remove};}
 static Result load(void* p,const ClassicBondIdentity& id,ClassicBondRecord& out){auto& s=*static_cast<Store*>(p);++s.loads;CHECK(!memcmp(id.peer_address,PeerHci,6)&&!memcmp(id.local_address,LocalHci,6));
  CHECK(out.format==1);for(size_t i=sizeof out.format;i<sizeof out;++i)CHECK(reinterpret_cast<const uint8_t*>(&out)[i]==0);
  if(s.reenter){auto& f=*s.fixture;switch(s.reenter){case 1:CHECK(f.host.begin(f.config,f.hooks)==Status::Busy);break;case 2:f.host.end();break;case 3:f.host.poll(f.now);break;case 4:(void)f.host.radioStats();break;case 5:CHECK(f.host.setVolume(1)==Status::Busy);break;case 6:f.event(0x17,{1,2,3,4,5,6});break;case 7:CHECK(f.host.bleResumeAdvertising()==Status::Busy);break;}}
  if(s.throws)throw std::runtime_error("synthetic provider failure");
  if(s.loadResult!=Result::Ok)return s.loadResult;if(!s.present)return Result::NotFound;out=s.record;
  switch(s.corrupt){case 1:++out.format;break;case 2:out.identity.local_address[0]^=1;break;case 3:out.identity.peer_address[0]^=1;break;case 4:out.key_type=ClassicLinkKeyType(3);break;case 5:out.key_type=ClassicLinkKeyType(6);break;case 6:out.pin_length=17;break;}
  return Result::Ok;
 }
 static Result save(void* p,const ClassicBondRecord& record){auto& s=*static_cast<Store*>(p);++s.saves;CHECK(record.format==1&&!memcmp(record.identity.local_address,LocalHci,6)&&!memcmp(record.identity.peer_address,PeerHci,6));
  const auto* bytes=reinterpret_cast<const uint8_t*>(&record);for(size_t i=offsetof(ClassicBondRecord,pin_length)+1;i<sizeof record;++i)CHECK(bytes[i]==0);
  if(s.saveResult!=Result::Ok)return s.saveResult;s.record=record;s.present=true;return Result::Ok;
 }
 static Result remove(void* p,const ClassicBondIdentity& id){auto& s=*static_cast<Store*>(p);++s.removes;CHECK(!memcmp(id.local_address,LocalHci,6)&&!memcmp(id.peer_address,PeerHci,6));if(s.removeResult==Result::Ok)s.present=false;return s.removeResult;}
};
static void configure(Fixture& f,Store* store){f.host.end();memcpy(f.config.address,LocalDisplay,6);f.config.classic_bonds=store?store->provider():ClassicBondStorage{};if(store)store->fixture=&f;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);}
static void link(Fixture& f){f.event(4,{1,2,3,4,5,6,0,0,0,1});f.settle();f.event(3,{0,0x25,0,1,2,3,4,5,6,1,0});f.settle();CHECK(f.host.radioStats().connected&&!bth_links_secure(&f.host.links_,0x25));}
static bool sent(const Fixture& f,uint16_t op){for(const auto& p:f.tx)if(p.size()>=4&&p[0]==1&&bth_read_le16(p.data()+1)==op)return true;return false;}
static void failStopped(Fixture& f){CHECK(!f.host.active_&&f.host.radioStats().transport_errors==1&&!f.host.radioStats().streaming);CHECK(!f.host.application_bonds_.enabled()&&!f.host.config_.classic_bonds.configured()&&!f.host.config_.callbacks.classic_bonds.configured());CHECK(!f.host.commands_.used);}
static Store persisted(){Store s;s.present=true;s.record.identity={};memcpy(s.record.identity.local_address,LocalHci,6);memcpy(s.record.identity.peer_address,PeerHci,6);for(unsigned i=0;i<16;++i)s.record.link_key[i]=uint8_t(0x90+i);s.record.key_type=ClassicLinkKeyType::UnauthenticatedP192;return s;}
static void persistence_and_security(){Store s;Fixture f(false);configure(f,&s);f.boot();f.classicConnect();CHECK(s.loads==1&&s.saves==1&&s.present&&s.record.pin_length==0&&uint8_t(s.record.key_type)==4);CHECK(bth_links_secure(&f.host.links_,0x25));CHECK(!f.host.bonds_[0].used);
 const unsigned saves=s.saves,loads=s.loads;f.host.end();CHECK(s.present&&s.saves==saves&&s.loads==loads&&!f.host.application_bonds_.enabled());configure(f,&s);f.tx.clear();f.baud_changes=0;f.boot();link(f);f.tx.clear();f.event(0x17,{1,2,3,4,5,6});f.poll();CHECK(sent(f,0x040b)&&!bth_links_secure(&f.host.links_,0x25));f.settle();CHECK(!bth_links_secure(&f.host.links_,0x25));f.event(8,{0,0x25,0,1});f.settle();CHECK(bth_links_secure(&f.host.links_,0x25)&&s.loads==loads+1&&s.saves==saves);f.host.end();
 CHECK(s.present&&!s.removes);for(size_t i=0;i+16<=sizeof f.host;++i)CHECK(memcmp(reinterpret_cast<const uint8_t*>(&f.host)+i,s.record.link_key,16)!=0);
}
static void default_ram_only(){Fixture f(false);f.boot();f.classicConnect();CHECK(f.host.bonds_[0].used);f.host.end();CHECK(!f.host.bonds_[0].used);CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.baud_changes=0;f.boot();link(f);f.tx.clear();f.event(0x17,{1,2,3,4,5,6});f.poll();CHECK(sent(f,0x040c)&&!sent(f,0x040b));f.host.end();}
static void misses_errors_and_malformed(){
 {Store s;Fixture f(false);configure(f,&s);f.boot();link(f);f.tx.clear();f.event(0x17,{1,2,3,4,5,6});f.poll();CHECK(sent(f,0x040c)&&f.host.active_&&!f.host.application_bonds_.failed());f.host.end();}
 for(unsigned kind=0;kind<9;++kind){Store s=persisted();Fixture f(false);configure(f,&s);f.boot();link(f);f.tx.clear();if(kind==0)s.loadResult=Result::Error;else if(kind==1)s.loadResult=Result(99);else if(kind==8)s.throws=true;else s.corrupt=kind-1;f.event(0x17,{1,2,3,4,5,6});failStopped(f);CHECK(s.loads==1&&!sent(f,0x040b)&&!sent(f,0x040c));}
}
static void save_remove_failure(){
 for(Result status:{Result::Error,Result::NotFound,Result(99)}){Store s;Fixture f(false);configure(f,&s);f.boot();link(f);Bytes key={1,2,3,4,5,6};key.insert(key.end(),16,0xaa);key.push_back(4);f.event(0x18,key);CHECK(!s.saves);s.saveResult=status;f.event(6,{0,0x25,0});CHECK(s.saves==1);failStopped(f);}
 for(Result status:{Result::Ok,Result::NotFound,Result::Error,Result(99)}){Store s=persisted();Fixture f(false);configure(f,&s);f.boot();link(f);f.event(0x17,{1,2,3,4,5,6});f.settle();s.removeResult=status;f.event(6,{5,0x25,0});CHECK(s.removes==1&&!bth_links_secure(&f.host.links_,0x25));if(status==Result::Error||status==Result(99))failStopped(f);else CHECK(f.host.active_&&f.host.links_.classic.disconnecting);f.host.end();}
}
static void reentry_and_configuration(){
 for(unsigned action=1;action<=7;++action){Store s=persisted();Fixture f(false);configure(f,&s);f.boot();link(f);s.reenter=action;f.tx.clear();f.event(0x17,{1,2,3,4,5,6});failStopped(f);CHECK(s.loads==1&&!sent(f,0x040b));}
 Fixture f(false);f.host.end();Store s;auto provider=s.provider();provider.remove=nullptr;f.config.classic_bonds=provider;CHECK(f.host.begin(f.config,f.hooks)==Status::InvalidArgument);f.config.classic_bonds={};f.config.classic_bonds.context=&s;CHECK(f.host.begin(f.config,f.hooks)==Status::InvalidArgument);f.config.classic_bonds=s.provider();f.config.callbacks.enable_a2dp=false;f.config.ble.enabled=true;CHECK(f.host.begin(f.config,f.hooks)==Status::Unsupported);
}
static void key_size_and_bad_notification(){
 {Store s=persisted();Fixture f(false);configure(f,&s);f.boot();link(f);f.event(0x17,{1,2,3,4,5,6});f.settle();CHECK(!bth_links_secure(&f.host.links_,0x25));f.event(8,{0,0x25,0,1});f.poll();while(!f.tx.empty()){auto packet=f.pop();if(Fixture::op(packet)==0x1408)f.complete(0x1408,{0,0x25,0,8});else f.acknowledge(packet);}CHECK(!bth_links_secure(&f.host.links_,0x25)&&f.host.links_.classic.disconnecting);f.host.end();}
 for(unsigned type:{1u,2u,3u,6u,9u}){Store s;Fixture f(false);configure(f,&s);f.boot();link(f);Bytes key={1,2,3,4,5,6};key.insert(key.end(),16,0xaa);key.push_back(uint8_t(type));f.event(0x18,key);CHECK(!s.saves&&!bth_links_secure(&f.host.links_,0x25));CHECK(f.host.links_.classic.disconnecting);f.host.end();}
}
static void changed_key_and_saved_callback_copy(){Store s;Fixture f(false);configure(f,&s);f.config.classic_bonds={};f.boot();f.classicConnect();CHECK(s.present&&s.saves==1&&bth_links_secure(&f.host.links_,0x25));Bytes change={1,2,3,4,5,6};change.insert(change.end(),16,0xbb);change.push_back(6);f.event(0x18,change);CHECK(s.loads==2&&s.saves==2&&s.record.link_key[0]==0xbb&&uint8_t(s.record.key_type)==4&&bth_links_secure(&f.host.links_,0x25));f.host.end();}
static TxMailbox* capturedMailbox=nullptr;
static void copied_uart_teardown(){Store s=persisted();Fixture f(false);configure(f,&s);f.boot();link(f);TxMailbox mailbox;capturedMailbox=&mailbox;
 f.host.hooks_.sendH4=[](const uint8_t* p,size_t n,void* user){auto& f=*static_cast<Fixture*>(user);f.tx.emplace_back(p,p+n);return capturedMailbox->submit(p,n);};
 f.tx.clear();f.event(0x17,{1,2,3,4,5,6});f.poll();CHECK(sent(f,0x040b)&&!mailbox.empty());const Bytes copied=f.tx.back();f.host.end();CHECK(f.tx.back()==copied);for(auto byte:f.host.output_)CHECK(byte==0);Bytes wire;uint8_t byte=0;while(mailbox.peek(byte)){wire.push_back(byte);CHECK(mailbox.advance());}CHECK(wire==copied&&mailbox.drained()&&mailbox.finish()&&mailbox.empty());capturedMailbox=nullptr;
}
int main(){persistence_and_security();default_ram_only();misses_errors_and_malformed();save_remove_failure();reentry_and_configuration();key_size_and_bad_notification();changed_key_and_saved_callback_copy();copied_uart_teardown();printf("APPLICATION_BOND_HOST_PASS checks=%u synthetic_keys_only=1 durable_backend_not_tested=1\n",checks);return 0;}
