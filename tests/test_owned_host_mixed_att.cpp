// Real public H4 actor, with an honest controller-to-host credit ledger.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_mixed_fixture_main
#include "test_owned_host.cpp"
#undef main
#include <map>
template<class T>static auto mixedPending(const T&t,int)->decltype(t.pendingAtt()){return t.pendingAtt();}
template<class T>static bool mixedPending(const T&,long){return false;}
static Bytes mixedValue(unsigned n){Bytes v(512);for(unsigned i=0;i<v.size();++i)v[i]=uint8_t(37*i+n);return v;}
static Bytes mixedWrite(uint8_t op,const Bytes&v){Bytes p={op,9,0};p.insert(p.end(),v.begin(),v.end());return p;}
static uint16_t mixedStream(Fixture&f){f.boot();f.classicConnect();auto s=f.channel(0x19,0x81);f.acl(0x25,s,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,s,{0x20,6,4});f.settle();auto m=f.channel(0x19,0x82);f.acl(0x25,s,{0x30,7,4});f.settle();CHECK(f.playing&&f.host.radioStats().streaming);f.leConnect();f.acl(0x26,4,{2,5,2});f.settle();CHECK(f.host.bleStats().att_mtu==517&&!f.host.rx_.outstanding);f.output.clear();return m;}
struct MixedController {
 Fixture&f;unsigned credit=4,received=0,completed=0;std::map<unsigned,unsigned> outstanding;std::vector<Bytes> withheld;std::vector<uint16_t> disconnects;
 explicit MixedController(Fixture&v):f(v){CHECK(!f.host.rx_.outstanding&&f.tx.empty());}
 void drain(bool hold=false){for(unsigned k=0;k<8;++k){f.poll(0);while(!f.tx.empty()){auto p=f.pop();if(p[0]==1&&Fixture::op(p)==0x0c35){CHECK(p.size()==5u+4u*p[4]);for(unsigned i=0;i<p[4];++i){auto h=bth_read_le16(p.data()+5+4*i),n=bth_read_le16(p.data()+7+4*i);CHECK(n&&outstanding[h]>=n);outstanding[h]-=n;credit+=n;completed+=n;CHECK(credit<=4);}continue;}if(p[0]==1&&Fixture::op(p)==0x0406)disconnects.push_back(bth_read_le16(p.data()+4));if(hold&&p[0]==2&&(bth_read_le16(p.data()+1)&0xfff)==0x26)withheld.push_back(p);else f.acknowledge(p);}}}
 bool raw(uint16_t handle,uint8_t pb,const Bytes&p,bool hold=false){if(!credit)return false;--credit;++received;++outstanding[handle];f.rawAcl(handle,pb,p);drain(hold);CHECK(!f.host.radioStats().transport_errors);return true;}
 bool att(const Bytes&p,unsigned chunk,bool hold=false){Bytes s={uint8_t(p.size()),uint8_t(p.size()>>8),4,0};s.insert(s.end(),p.begin(),p.end());for(size_t pos=0;pos<s.size();pos+=chunk){auto n=std::min<size_t>(chunk,s.size()-pos);if(!raw(0x26,pos?1:2,Bytes(s.begin()+pos,s.begin()+pos+n),hold))return false;}return true;}
 unsigned responses(uint8_t op)const{unsigned n=0;for(auto&p:f.output)if(p.cid==4&&p.bytes==Bytes{op})++n;return n;}
 void retired(){drain();CHECK(credit==4&&received==completed&&!f.host.rx_.outstanding);}
 void media(uint16_t cid,unsigned seq){auto before=f.media;CHECK(raw(0x25,2,{14,0,uint8_t(cid),uint8_t(cid>>8),0x80,0x60,uint8_t(seq>>8),uint8_t(seq),1,2,3,4,5,6,7,8,1,0xaa}));CHECK(f.media==before+1&&f.host.radioStats().streaming);}
};
static void mixed(unsigned chunk,bool prepared){Fixture f;auto cid=mixedStream(f);MixedController c(f);auto a=mixedValue(1),b=mixedValue(2),expected=b;CHECK(c.att(mixedWrite(0x12,a),chunk));CHECK(c.responses(0x13)==1&&f.host.bleStats().writes==1);
 if(prepared){Bytes p={0x16,9,0,0,0};p.insert(p.end(),b.begin(),b.end());CHECK(p.size()==517&&c.att(p,chunk));CHECK(f.host.le_.att_.prepared_length==512);CHECK(c.att({0x18,1},chunk));CHECK(c.att({0x16,9,0,0,0,0xee},chunk));CHECK(c.att({0x18,0},chunk));CHECK(f.host.le_.att_.prepared_length==512&&!memcmp(f.host.le_.att_.prepared,expected.data(),512));}else CHECK(c.att(mixedWrite(0x12,b),chunk));
 b.assign(512,0);c.retired();CHECK(!f.host.peers_[1].has_pending&&mixedPending(f.host.le_,0)&&c.responses(prepared?0x19:0x13)==(prepared?0u:1u));
 for(unsigned i=0;i<65;++i){CHECK(c.att(mixedWrite(0x52,mixedValue(i+8)),chunk));c.media(cid,i);c.retired();CHECK(f.host.bleStats().writes==1&&mixedPending(f.host.le_,0));}
 CHECK(f.host.bleStats().write_command_drops==65);BleWrite out;CHECK(f.host.bleReadWrite(out)&&out.length==512&&!memcmp(out.data,a.data(),512));c.drain();c.retired();CHECK(!mixedPending(f.host.le_,0)&&f.host.bleStats().writes==2);CHECK(c.responses(prepared?0x19:0x13)==(prepared?1u:2u));CHECK(f.host.bleReadWrite(out)&&out.length==512&&!memcmp(out.data,expected.data(),512));CHECK(!f.host.bleReadWrite(out));if(prepared)CHECK(!f.host.le_.att_.execute_pending&&!f.host.le_.att_.prepared_length);
 printf("mixed ATT chunk%u prepared%u:65 commands+Classic with real credits\n",chunk,unsigned(prepared));}
static void blockReplies(MixedController&c){for(unsigned i=0;i<7;++i)CHECK(c.att({0x0a,3,0},1021,true));CHECK(c.withheld.size()==4&&c.f.host.control_.peers[1].owned==3);}
static void unblockReplies(MixedController&c){for(auto&p:c.withheld)c.f.acknowledge(p);c.withheld.clear();c.drain();c.retired();}
static void replyPressure(){Fixture f;auto cid=mixedStream(f);MixedController c(f);blockReplies(c);auto b=mixedValue(42);CHECK(c.att(mixedWrite(0x12,b),251,true));CHECK(f.host.le_.pendingAtt()&&!f.host.bleStats().writes&&!f.host.le_.write_pending_);
 const auto original=Bytes(f.host.le_.pending_request_,f.host.le_.pending_request_+f.host.le_.pending_request_length_);
 for(unsigned i=0;i<65;++i){CHECK(c.att(mixedWrite(0x52,mixedValue(i)),251,true));c.media(cid,i);CHECK(f.host.le_.pendingAtt()&&!f.host.bleStats().writes);}
 for(auto p:std::vector<Bytes>{{1,0x12,0,0,4},{0x0b,1},{0x13},{0x1b,11,0,1},{0x1d,11,0,1},{0x1e},{0x40},{0xd2}})CHECK(c.att(p,251,true));
 CHECK(c.att({0x52,12,0,1,0},251,true)&&f.host.bleStats().notify_enabled);CHECK(c.att({0x52,12,0,0,0},251,true)&&!f.host.bleStats().notify_enabled);
 CHECK(f.host.bleStats().write_command_drops==65&&original==Bytes(f.host.le_.pending_request_,f.host.le_.pending_request_+f.host.le_.pending_request_length_));CHECK(c.responses(0x13)==0);unblockReplies(c);CHECK(!f.host.le_.pendingAtt()&&c.responses(0x13)==1&&f.host.bleStats().writes==1);BleWrite out;CHECK(f.host.bleReadWrite(out)&&!memcmp(out.data,b.data(),512));}
static void oversized(){for(unsigned length:{518u,4092u}){Fixture f;mixedStream(f);MixedController c(f);blockReplies(c);Bytes p(length,0xcc);p[0]=0x12;p[1]=9;p[2]=0;CHECK(c.att(p,251,true));CHECK(f.host.le_.pendingAtt()&&f.host.le_.pending_response_&&f.host.le_.pending_request_length_==5);CHECK(f.host.peers_[1].assembly.capacity==4096&&!f.host.peers_[1].has_pending&&!f.host.rx_.outstanding);unblockReplies(c);unsigned errors=0;for(auto&v:f.output)if(v.cid==4&&v.bytes==Bytes({1,0x12,0,0,4}))++errors;CHECK(errors==1&&!f.host.bleStats().writes&&!f.host.le_.pendingAtt()&&!f.host.radioStats().transport_errors);}}
static void deadline(uint32_t at,bool pressure){Fixture f;auto cid=mixedStream(f);MixedController c(f);CHECK(c.att(mixedWrite(0x12,mixedValue(1)),251));f.now=at;CHECK(c.att(mixedWrite(0x12,mixedValue(2)),251));CHECK(f.host.le_.pending_request_started_==at);f.now=at+4999;CHECK(c.att(mixedWrite(0x52,mixedValue(3)),251));CHECK(f.host.le_.pendingAtt()&&c.disconnects.empty());
 if(pressure){f.ready=false;for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push_owned(&f.host.commands_,0x0c16,nullptr,0,false,f.host.peers_[0].owner)==BTH_OK);}
 BleWrite out;CHECK(f.host.bleReadWrite(out));f.now=at+5000;c.drain();CHECK(f.host.le_.pendingAtt()&&f.host.bleStats().writes==1&&c.responses(0x13)==1);if(pressure){CHECK(c.disconnects.empty());f.ready=true;c.drain();}
 CHECK(!c.disconnects.empty()&&c.disconnects.back()==0x26&&f.host.radioStats().streaming);f.event(5,{0,0x26,0,0x13});c.drain();CHECK(!f.host.le_.pendingAtt()&&!f.host.bleStats().connected&&f.host.radioStats().streaming&&!f.host.radioStats().transport_errors);c.media(cid,1);}
static void deferredAndReuse(){Fixture f;auto cid=mixedStream(f);MixedController c(f);CHECK(c.att(mixedWrite(0x12,mixedValue(1)),27));CHECK(c.att(mixedWrite(0x12,mixedValue(2)),27));BleWrite out;CHECK(f.host.bleReadWrite(out));auto old=f.host.bleStats().generation;f.ready=false;for(unsigned i=0;i<7;++i)CHECK(bth_cmdq_push_owned(&f.host.commands_,0x0c16,nullptr,0,false,f.host.peers_[0].owner)==BTH_OK);f.event(5,{0,0x26,0,0x13});CHECK(f.host.deferred_count_==1);c.drain();CHECK(f.host.le_.pendingAtt()&&f.host.bleStats().writes==1&&c.responses(0x13)==1);f.ready=true;c.drain();CHECK(!f.host.le_.pendingAtt()&&!f.host.bleStats().connected&&f.host.radioStats().streaming);f.leConnect();f.acl(0x26,4,{2,5,2});f.settle();CHECK(f.host.bleStats().generation!=old);f.output.clear();CHECK(c.att(mixedWrite(0x12,mixedValue(3)),27));CHECK(f.host.bleReadWrite(out)&&out.generation!=old&&out.data[0]==3);c.media(cid,1);}
int main(){mixed(1021,false);mixed(251,false);mixed(27,false);mixed(251,true);replyPressure();oversized();deadline(0,false);deadline(uint32_t(0)-5000,false);deadline(20000,true);deferredAndReuse();printf("Mixed ATT %u checks PASS\n",checks);}
