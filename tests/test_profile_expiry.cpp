// Breaks caught: a delayed untagged response reviving a timed-out session,
// or reusing a prior microphone epoch to make a stale request current again.
#include "owned/HfpControl.h"
#include "owned/HspControl.h"
#include "owned/core/bth_rfcomm.h"
#include <cstdio>
#include <cstring>
using namespace giga_bredr::owned;
namespace {
unsigned failures;
#define CHECK(x) do { if(!(x)){ ++failures;std::printf("FAIL line %u: %s\n",unsigned(__LINE__),#x); } } while(0)
void hfReady(HfpControl& h) {
 CHECK(h.begin(19,false,8,9));
 const char* replies[]={"+BRSF: 0\r\nOK\r\n",
 "+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",
 "+CIND: 1,0,0,0,5,0,5\r\nOK\r\n","OK\r\n","OK\r\n","OK\r\n"};
 char tx[64];size_t n;
 for(const char* reply:replies){CHECK(h.next(19,1,tx,sizeof tx,n)==BTH_OK&&n);CHECK(h.receive(19,reinterpret_cast<const uint8_t*>(reply),std::strlen(reply),2)==BTH_OK);}
 CHECK(h.ready());
}
void hfTimeoutRetiresSession(bool pollFirst) {
 HfpControl h;hfReady(h);char tx[64];size_t n;
 CHECK(h.command(19,HfpCommand::Answer)==BTH_OK);
 CHECK(h.next(19,10,tx,sizeof tx,n)==BTH_OK&&n==4);
 if(pollFirst)CHECK(h.expire(19,5010)==BTH_OK);
 CHECK(h.receive(19,reinterpret_cast<const uint8_t*>("OK\r\n"),4,5010)==BTH_IGNORED);
 CHECK(h.status().state==HfpState::Failed&&!h.status().slcEstablished&&!h.status().lastCommandOk);
 CHECK(h.command(19,HfpCommand::Hangup)==BTH_IGNORED);
 CHECK(h.next(19,5011,tx,sizeof tx,n)==BTH_IGNORED&&n==0);
}
void hsTimeoutRetiresSession(bool pollFirst) {
 HspControl h;CHECK(h.begin(23,8,9));CHECK(h.button(23)==BTH_OK);char tx[32];size_t n;
 CHECK(h.next(23,10,tx,sizeof tx,n)==BTH_OK&&n==12);
 if(pollFirst)CHECK(h.expire(23,5010)==BTH_OK);
 CHECK(h.receive(23,reinterpret_cast<const uint8_t*>("OK\r\n"),4,5010)==BTH_IGNORED);
 CHECK(!h.ready()&&h.status().failed&&!h.status().lastCommandOk);
 CHECK(h.button(23)==BTH_IGNORED);
}
void rfcommTimeoutRetiresSession() {
 bth_rfcomm_t r{};uint8_t tx[32];size_t n;
 CHECK(bth_rfcomm_init(&r,31,true,0,128,2));CHECK(bth_rfcomm_start(&r,31,1,tx,sizeof tx,&n)==BTH_OK);
 CHECK(bth_rfcomm_expire(&r,31,60001)==BTH_OK);
 CHECK(bth_rfcomm_start(&r,31,60002,tx,sizeof tx,&n)==BTH_IGNORED&&n==0);
 const uint8_t ua[]={3,0x73,1,0xd7};bth_rfcomm_event_t e{};
 CHECK(bth_rfcomm_receive(&r,31,60002,ua,sizeof ua,tx,sizeof tx,&n,&e)==BTH_IGNORED);
 CHECK(!bth_rfcomm_ready(&r,31,6));
}
void rfcommLateUaExpiresBeforeReceive() {
 bth_rfcomm_t r{};uint8_t tx[32];size_t n;bth_rfcomm_event_t e{};
 CHECK(bth_rfcomm_init(&r,41,true,0,128,2));CHECK(bth_rfcomm_start(&r,41,1,tx,sizeof tx,&n)==BTH_OK);
 const uint8_t ua[]={3,0x73,1,0xd7};
 CHECK(bth_rfcomm_receive(&r,41,60001,ua,sizeof ua,tx,sizeof tx,&n,&e)==BTH_IGNORED&&n==0);
 CHECK(bth_rfcomm_start(&r,41,60002,tx,sizeof tx,&n)==BTH_IGNORED&&n==0);
}
void microphoneEpochCannotRollBack() {
 HfpPcmInput input;VoicePcmFormat f{VoiceCodec::Msbc,16000,120,1};uint8_t pcm[240];
 CHECK(input.configure(37,1,f,MicrophoneMode::None));CHECK(input.configure(37,3,f,MicrophoneMode::None));input.detach();
 CHECK(!input.configure(37,1,f,MicrophoneMode::None));
 CHECK(input.read(37,1,pcm,120).result==VoiceIoResult::Stale);
 CHECK(input.configure(37,4,f,MicrophoneMode::None));
 CHECK(input.read(37,4,pcm,120).frames==120);
}
}
int main(){hfTimeoutRetiresSession(true);hfTimeoutRetiresSession(false);hsTimeoutRetiresSession(true);hsTimeoutRetiresSession(false);rfcommTimeoutRetiresSession();rfcommLateUaExpiresBeforeReceive();microphoneEpochCannotRollBack();return failures?1:0;}
