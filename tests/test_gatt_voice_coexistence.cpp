// Synthetic controller and real host voice fixture copied from test_owned_voice_transport.cpp.
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
}
int main() {
 Transport t;t.boot();t.connect();t.request();
 t.waitCommand(0x0429);t.f.status(0x0429);
 t.f.event(0x2c,{0,0x33,0,1,2,3,4,5,6,2,6,2,30,0,30,0,2});
 CHECK(t.audio.active());
 // Retain one outstanding SCO TX packet. The inherited general actor
 // acknowledge helper handles HCI commands and ACL, not H4 SCO.
 bool sent=false;for(unsigned k=0;k<20&&!sent;++k){t.f.poll();while(!t.f.tx.empty()){
  auto p=t.f.pop();if(p[0]==3)sent=true;else t.f.acknowledge(p);
 }}CHECK(sent);
 t.f.leConnect();const auto generation=t.f.host.bleStats().generation;
 t.f.acl(0x26,4,{0x12,17,0,2,0});t.f.settle();t.f.output.clear();
 CHECK(t.f.host.bleSetVendorDescription(generation,false));t.f.settle();
 unsigned indications=0;
 for(const auto& p:t.f.output)if(p.cid==4&&p.bytes==Bytes({0x1d,16,0,7,0,13,0}))++indications;
 CHECK(indications==1&&t.audio.active()&&!t.f.host.radioStats().transport_errors);
 t.f.acl(0x26,4,{0x1e});t.f.settle();
 CHECK(t.audio.active()&&!t.f.host.radioStats().transport_errors);
 const uint8_t sco[]={0x33,0,4,1,2,3,4};
 const unsigned before=t.received;t.f.host.receive(3,sco,sizeof sco,t.f.now);t.f.poll();
 CHECK(t.received==before+1&&t.audio.active());
 t.f.event(5,{0,0x33,0,0x13});
 CHECK(!t.audio.active()&&t.f.host.bleStats().connected&&!t.f.host.radioStats().transport_errors);
 std::printf("GATT_VOICE_COEXISTENCE checks=%u synthetic_only=1\n",checks);
}
