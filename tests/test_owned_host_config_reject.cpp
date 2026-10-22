#define private public
#include "owned/OwnedHost.h"
#undef private
#define main fixture_main
#include "test_owned_host.cpp"
#undef main
static uint16_t start(Fixture&f){f.boot();f.classicConnect();auto s=f.channel(0x19,0x81);f.acl(0x25,s,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,s,{0x20,6,4});f.settle();auto m=f.channel(0x19,0x82);f.acl(0x25,s,{0x30,7,4});f.settle();CHECK(f.playing&&f.host.radioStats().streaming);return m;}
static Bytes qos(){
 // Defined L2CAP QoS option3,22-byte flow spec: Flags0, Guaranteed service2,
 // TokenRate1000, Bucket512, Peak1000, Latency/DelayVariation1000000us.
 return{3,22,0,2,0xe8,3,0,0,0,2,0,0,0xe8,3,0,0,0x40,0x42,0x0f,0,0x40,0x42,0x0f,0};
}
static Bytes config(uint16_t local,const Bytes& options,uint8_t label=0x55,uint8_t flags=0){Bytes p={4,label,uint8_t(options.size()+4),0,uint8_t(local),uint8_t(local>>8),flags,0};p.insert(p.end(),options.begin(),options.end());return p;}
static const bth_l2cap_channel_t&ch(const Fixture&f,uint16_t local){auto*p=bth_l2cap_sig_find(&f.host.signaling_,local);CHECK(p);return*p;}
static const Bytes* reply(const Fixture&f,uint8_t id){for(const auto&p:f.output)if(p.cid==1&&p.bytes.size()>=10&&p.bytes[0]==5&&p.bytes[1]==id)return&p.bytes;return nullptr;}
static bool rejected_open(bool with_mtu,unsigned role,uint8_t flags){Fixture f(false);const auto media=start(f);const auto signal=f.host.classic_.signalCid();uint16_t target=role==1?media:signal;if(role==2)target=f.channel(0x17,0x83);const auto avrcp=f.host.classic_.avrcpCid();const auto before=ch(f,target);const auto stops=f.stops,prepared=f.configured;
 Bytes options=qos();if(with_mtu)options.insert(options.begin(),{1,2,48,0});bool preserved=true;
 for(unsigned repeat=0;repeat<3;++repeat){const uint8_t id=uint8_t(0x55+repeat);f.output.clear();f.acl(0x25,1,config(target,options,id,flags));f.settle();
 const auto*r=reply(f,id);CHECK(r&&bth_read_le16(r->data()+8)==3);CHECK(r->size()==34&&(*r)[6]==flags&&Bytes(r->begin()+10,r->end())==qos());
 const auto&after=ch(f,target);preserved&=memcmp(&after,&before,sizeof before)==0&&f.playing&&f.host.classic_.configured()&&f.host.classic_.mediaCid()==media&&f.host.classic_.signalCid()==signal&&f.host.classic_.avrcpCid()==avrcp&&f.configured==prepared&&f.stops==stops;
 if(preserved){Bytes p={0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa};auto n=f.media;f.acl(0x25,media,p);f.settle();CHECK(f.media==n+1&&f.host.radioStats().streaming);}
 }
 printf("REJECTED_OPEN role%u mtuOption%u firstC%u:3 exact rejects preserved=%u stream=%u stops=%u(old%u)\n",role,with_mtu,flags,preserved,f.playing,f.stops,stops);
 CHECK(f.host.radioStats().transport_errors==0);return preserved;
}
static void full_reply_pool(){Fixture f(false);start(f);auto signal=f.host.classic_.signalCid();const auto before=ch(f,signal);const auto stops=f.stops;f.ready=false;
 for(unsigned i=0;i<3;++i){f.acl(0x25,1,{8,uint8_t(0x60+i),2,0,uint8_t(i),0});f.poll();}
 CHECK(f.host.control_.peers[0].owned==3);f.acl(0x25,1,config(signal,qos()));f.poll();CHECK(f.host.peers_[0].has_pending&&f.host.rx_.outstanding==4);
 CHECK(!memcmp(&ch(f,signal),&before,sizeof before)&&f.playing&&f.stops==stops);f.ready=true;f.settle();CHECK(!f.host.peers_[0].has_pending&&!f.host.rx_.outstanding&&f.playing&&f.stops==stops);
 CHECK(!memcmp(&ch(f,signal),&before,sizeof before));auto*r=reply(f,0x55);CHECK(r&&bth_read_le16(r->data()+8)==3);
}
static void malformed_wrong_cid(){Fixture f(false);start(f);auto signal=f.host.classic_.signalCid();const auto before=ch(f,signal);const auto stops=f.stops;
 auto p=config(signal,qos());p.pop_back();--p[2];f.output.clear();f.acl(0x25,1,p);f.settle();CHECK(!memcmp(&ch(f,signal),&before,sizeof before)&&f.playing&&f.stops==stops&&!reply(f,0x55));
 f.output.clear();f.acl(0x25,1,config(0x70,qos()));f.settle();bool rejected=false;for(const auto&o:f.output)if(o.cid==1&&o.bytes.size()>=6&&o.bytes[0]==1&&bth_read_le16(o.bytes.data()+4)==2)rejected=true;
 CHECK(rejected&&!memcmp(&ch(f,signal),&before,sizeof before)&&f.playing&&f.stops==stops&&f.host.radioStats().transport_errors==0);
}
static void accepted_mtu_observer(){Fixture f(false);start(f);auto sdp=f.channel(1,0x83);CHECK(ch(f,sdp).remote_mtu==672);f.output.clear();f.acl(0x25,1,config(sdp,{1,2,48,0},0x56));f.settle();auto*r=reply(f,0x56);CHECK(r&&bth_read_le16(r->data()+8)==0);uint16_t cached=0;for(const auto&c:f.host.classic_.channels_)if(c.local==sdp)cached=c.mtu;CHECK(cached);unsigned own_config_requests=0;for(const auto&p:f.output)if(p.cid==1&&!p.bytes.empty()&&p.bytes[0]==4)++own_config_requests;
 printf("ACCEPTED_MTU observation only: kernelRemote=%u cachedProfile=%u state=%u reverseConfigRequests=%u; no post-reconfig SDP data submitted\n",ch(f,sdp).remote_mtu,cached,unsigned(ch(f,sdp).state),own_config_requests);
}
int main(){bool ok=true;for(unsigned role=0;role<3;++role)for(uint8_t flags:{uint8_t(0),uint8_t(1)})for(bool mtu:{false,true})ok&=rejected_open(mtu,role,flags);full_reply_pool();malformed_wrong_cid();accepted_mtu_observer();printf("Classic configuration public-path checks=%u preserve_rejected=%u\n",checks,ok);return ok?0:2;}
