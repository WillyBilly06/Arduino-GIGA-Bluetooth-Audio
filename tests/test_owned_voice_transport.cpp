#include <vector>
#include <string>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main original_owned_actor_fixture_main
#include "test_owned_host.cpp"
#undef main
namespace {
struct Transport {
 Fixture f;VoiceProfiles profiles;VoiceSdp sdp;OwnedVoiceAudio audio;uint8_t storage[256]{},unit[60]{};
 uint32_t prepared=0,ticket=1;unsigned requests=0,received=0,ended=0;size_t cursor=0;bool route=true,queued=false,synchronous_mask=false;
 Transport(bool hci=true):route(hci){f.host.end();VoiceAudioHostHooks h;
  h.user=this;h.prepare=[](uint32_t g,owned::VoiceCodec,void* p){static_cast<Transport*>(p)->prepared=g;return Status::Ok;};
  h.ended=[](uint32_t,void* p){++static_cast<Transport*>(p)->ended;};
  h.request_tx=[](uint32_t,unsigned n,void* p){auto& t=*static_cast<Transport*>(p);t.requests+=n;t.queued=true;t.cursor=0;++t.ticket;};
  h.copy_tx=[](uint32_t,uint8_t* out,size_t cap,size_t& n,uint32_t& serial,void* p){auto& t=*static_cast<Transport*>(p);if(!t.queued||cap<60-t.cursor)return false;n=60-t.cursor;memcpy(out,t.unit+t.cursor,n);serial=t.ticket;return true;};
  h.consume_tx=[](uint32_t,uint32_t serial,size_t n,void* p){auto& t=*static_cast<Transport*>(p);if(serial!=t.ticket||n>60-t.cursor)return false;t.cursor+=n;if(t.cursor==60)t.queued=false;return true;};
  h.receive=[](uint32_t,uint8_t,const uint8_t*,size_t,void* p){++static_cast<Transport*>(p)->received;return true;};
  CHECK(audio.begin({},h,storage,sizeof storage)==Status::Ok);f.config.voice_control=&profiles;f.config.voice_config.headset=true;f.config.voice_audio=&audio;f.config.voice_sdp=&sdp;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);
 }
 void boot(){unsigned loops=0;while(!f.host.radioStats().controller_ready&&loops++<1500){f.poll();if(!f.tx.empty()){const auto p=f.pop();auto op=Fixture::op(p);auto reply=f.response(op);if(op==0x1005)reply={0,0xfd,3,64,8,0,2,0};if(op==0x1002){reply.assign(65,0);reply[11]=0x10;}if(op==0xfc1d)reply={0,uint8_t(route),0,0,0,0};if(op==0x0c2e)reply={0,1};if(op==0x0c01)synchronous_mask=p.size()==12&&(p[9]&0x18)==0x18;CHECK(op!=0xfc1c);f.complete(op,reply);}}
  CHECK(f.host.radioStats().controller_ready&&!f.host.radioStats().transport_errors&&synchronous_mask);f.settle();
 }
 void connect(){f.classicConnect();f.channel(3,0x90);CHECK(profiles.hs_.begin(77,15,15));}
 void request(){f.event(4,{1,2,3,4,5,6,0,0,0,2});}
 Bytes waitCommand(uint16_t op){for(unsigned k=0;k<20;++k){f.poll();while(!f.tx.empty()){auto p=f.pop();if(p[0]==1&&Fixture::op(p)==op)return p;f.acknowledge(p);}}CHECK(false);return {};}
};
void actual_dispatch(){Transport t;t.boot();CHECK(t.audio.ready()&&t.sdp.count()==1&&t.f.host.gap_.config.uuid_count==2&&t.f.host.uuids_[1]==0x1108);t.connect();
 const auto sdp=t.f.channel(1,0x91);t.f.output.clear();t.f.acl(0x25,sdp,{2,0,1,0,8,0x35,3,0x19,0x11,8,0,4,0});t.f.settle();bool advertised=false;for(const auto& p:t.f.output)if(p.cid==0x91&&p.bytes.size()==14&&p.bytes[6]==1&&p.bytes[12]==5)advertised=true;CHECK(advertised);
 t.request();auto accept=t.waitCommand(0x0429);CHECK(accept.size()==25&&bth_read_le16(accept.data()+20)==0x60&&t.prepared);t.f.status(0x0429);
 t.f.event(0x2c,{0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2});CHECK(t.audio.active()&&t.requests==1);
 bool sent=false;for(unsigned k=0;k<20&&!sent;++k){t.f.poll();while(!t.f.tx.empty()){auto p=t.f.pop();if(p[0]==3){CHECK(p.size()==64&&bth_read_le16(p.data()+1)==0x33);sent=true;}else t.f.acknowledge(p);}}CHECK(sent);
 const auto aclAvailable=t.f.host.flow_.classic.available;t.f.event(0x13,{1,0x33,0,1,0});CHECK(t.requests==2&&t.f.host.flow_.classic.available==aclAvailable);
 const uint8_t sco[]={0x33,0,4,1,2,3,4};t.f.host.receive(3,sco,sizeof sco,t.f.now);t.f.poll();CHECK(t.received==1);
 t.f.event(5,{0,0x33,0,0x13});CHECK(!t.audio.active()&&t.ended==1&&t.f.host.radioStats().connected&&!t.f.host.radioStats().transport_errors);
 t.f.host.end();CHECK(!t.audio.active());t.f.config.voice_audio=nullptr;t.f.config.voice_control=nullptr;t.f.config.voice_sdp=nullptr;t.f.config.callbacks.volume=[](uint8_t,void*){};CHECK(t.f.host.begin(t.f.config,t.f.hooks)==Status::Ok);CHECK(t.f.host.uuids_[1]==0x110c&&t.f.host.uuids_[2]==0x110e&&!t.f.host.gap_.synchronous_events);t.f.host.end();
}
void route_gate(){Transport t(false);t.boot();CHECK(!t.audio.ready()&&!t.profiles.enabled());t.f.classicConnect();t.request();auto reject=t.waitCommand(0x042a);CHECK(reject.size()==11&&!t.prepared&&!t.audio.active());t.f.status(0x042a);t.f.event(0x2c,{0x0d,0,0,1,2,3,4,5,6,2,0,0,0,0,0,0,0});CHECK(t.f.host.radioStats().connected&&!t.f.host.radioStats().transport_errors);}
void profile_gate(){Transport t;t.boot();t.f.classicConnect();t.request();t.waitCommand(0x042a);CHECK(!t.prepared);}
}
int main(){actual_dispatch();route_gate();profile_gate();printf("OWNED_VOICE_TRANSPORT checks=%u\n",checks);return 0;}
