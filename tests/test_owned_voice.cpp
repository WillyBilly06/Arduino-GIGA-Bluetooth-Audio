#include <vector>
#include <string>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main original_owned_actor_fixture_main
#include "test_owned_host.cpp"
#undef main
namespace {
struct VoiceFixture {
 VoiceProfiles voice;Fixture f;uint16_t cid=0;
 VoiceFixture(){f.host.end();f.config.voice_control=&voice;f.config.voice_config.hands_free=true;f.config.voice_config.headset=true;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();cid=f.channel(3,0x90);CHECK(voice.cid()==cid);}
 void sabm(){f.output.clear();f.acl(0x25,cid,{3,0x3f,1,0x1c});f.settle();bool ua=false;for(const auto& p:f.output)if(p.cid==0x90&&p.bytes==Bytes({3,0x73,1,0xd7}))ua=true;CHECK(ua);}
};
void default_refusal(){Fixture f;f.boot();f.classicConnect();f.output.clear();f.acl(0x25,1,{2,12,4,0,3,0,0x90,0});f.settle();bool refused=false;
 for(const auto& p:f.output)if(p.cid==1&&p.bytes.size()==12&&p.bytes[0]==3&&p.bytes[1]==12&&bth_read_le16(p.bytes.data()+8)==2)refused=true;CHECK(refused);CHECK(!f.host.config_.voice_control);}
void admission_and_teardown(){VoiceFixture v;v.sabm();auto& f=v.f;
 // A second L2CAP RFCOMM channel cannot create a crossed mux.
 f.output.clear();f.acl(0x25,1,{2,13,4,0,3,0,0x91,0});f.settle();bool rejected=false;
 for(const auto& p:f.output)if(p.cid==1&&p.bytes.size()==12&&p.bytes[0]==3&&p.bytes[1]==13&&bth_read_le16(p.bytes.data()+8)==4)rejected=true;CHECK(rejected);CHECK(v.voice.cid()==v.cid);
 // No actual pending disconnect: a forged/mismatched response cannot retire it.
 f.acl(0x25,1,{7,99,4,0,0x90,0,uint8_t(v.cid),uint8_t(v.cid>>8)});f.settle();CHECK(v.voice.cid()==v.cid);
 f.acl(0x25,1,{6,14,4,0,uint8_t(v.cid),uint8_t(v.cid>>8),0x90,0});f.settle();CHECK(!v.voice.cid());
 v.cid=f.channel(3,0x90);v.sabm();CHECK(f.host.radioStats().connected&&!f.host.radioStats().transport_errors);
 f.event(5,{0,0x25,0,0x13});f.settle();CHECK(!v.voice.cid()&&!v.voice.hfpStatus().generation);
 f.host.end();CHECK(!v.voice.enabled());
}
void no_profile_advertisement(){VoiceFixture v;auto& f=v.f;const auto cid=f.channel(1,0x91);f.output.clear();f.acl(0x25,cid,{2,0,1,0,8,0x35,3,0x19,0x11,0x1e,0,4,0});f.settle();bool empty=false;
 for(const auto& p:f.output)if(p.cid==0x91&&p.bytes.size()>=10&&p.bytes[0]==3&&p.bytes[5]==0&&p.bytes[6]==0&&p.bytes[7]==0&&p.bytes[8]==0)empty=true;CHECK(empty);
}
void copied_close_preserves_media(){VoiceFixture v;auto& f=v.f;
 const auto signal=f.channel(0x19,0x91);f.acl(0x25,signal,{0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53});f.settle();f.acl(0x25,signal,{0x20,6,4});f.settle();const auto media=f.channel(0x19,0x92);f.acl(0x25,signal,{0x30,7,4});f.settle();CHECK(f.playing);
 bth_control_reservation_t pending{};CHECK(bth_control_tx_reserve(&f.host.control_,0,&pending)==BTH_OK);pending.data[0]=0x77;CHECK(bth_control_tx_commit(&f.host.control_,pending.token,0x90,1,672)==BTH_OK);
 f.ready=false;f.acl(0x25,1,{6,19,4,0,uint8_t(v.cid),uint8_t(v.cid>>8),0x90,0});f.poll();CHECK(f.host.voice_retiring_&&v.voice.cid()==v.cid);
 const unsigned before=f.media,configured=f.configured;f.acl(0x25,media,{0x80,0x60,0,1,1,2,3,4,5,6,7,8,1,0xaa});f.poll();CHECK(f.media==before+1&&f.playing&&f.configured==configured);
 f.ready=true;f.settle();CHECK(!v.voice.cid()&&!f.host.voice_retiring_&&f.playing&&!f.host.radioStats().transport_errors);
}
void local_close_cannot_overtake_wire(){VoiceFixture v;auto& f=v.f;
 bth_control_reservation_t pending{};CHECK(bth_control_tx_reserve(&f.host.control_,0,&pending)==BTH_OK);pending.data[0]=0x77;CHECK(bth_control_tx_commit(&f.host.control_,pending.token,0x90,1,672)==BTH_OK);
 f.ready=false;CHECK(f.host.disconnectChannel(v.cid));const auto* old=bth_l2cap_sig_find(&f.host.signaling_,v.cid);CHECK(old&&old->state==BTH_CHANNEL_DISCONNECTING);const auto id=old->disconnect_id;
 f.host.synchronizeChannels();CHECK(!v.voice.cid());
 // The peer answers before the ordered mailbox has transmitted our DISC,
 // then asks for a new mux in the same signaling SDU. The old queued data
 // remains real transport work and must fence replacement even now.
 f.acl(0x25,1,{7,id,4,0,0x90,0,uint8_t(v.cid),uint8_t(v.cid>>8),2,32,4,0,3,0,0x91,0});f.poll();
 old=bth_l2cap_sig_find(&f.host.signaling_,v.cid);CHECK(f.host.voice_retiring_&&old&&old->state==BTH_CHANNEL_DISCONNECTING);
 f.ready=true;f.settle();CHECK(!f.host.voice_retiring_&&!f.host.radioStats().transport_errors);
}
void le_isolation(){VoiceFixture v;auto& f=v.f;f.leConnect();CHECK(f.host.bleStats().connected);
 f.acl(0x26,4,{0x12,9,0,0x11,0x22,0x33});f.settle();BleWrite w;CHECK(f.host.bleReadWrite(w)&&w.length==3&&w.data[0]==0x11&&w.data[2]==0x33);
 v.sabm();f.event(5,{0,0x26,0,0x13});f.settle();CHECK(!f.host.bleStats().connected&&f.host.radioStats().connected&&v.voice.cid()==v.cid);
 f.leConnect();f.acl(0x26,4,{0x12,9,0,0x44});f.settle();CHECK(f.host.bleReadWrite(w)&&w.length==1&&w.data[0]==0x44);CHECK(!f.host.radioStats().transport_errors);
}
}
int main(int argc,char** argv){const std::string s=argc>1?argv[1]:"all";if(s=="all"||s=="default")default_refusal();if(s=="all"||s=="owner")admission_and_teardown();if(s=="all"||s=="sdp")no_profile_advertisement();if(s=="all"||s=="media")copied_close_preserves_media();if(s=="all"||s=="local_close")local_close_cannot_overtake_wire();if(s=="all"||s=="le")le_isolation();std::printf("OWNED_VOICE_ACTOR checks=%u\n",checks);return 0;}
