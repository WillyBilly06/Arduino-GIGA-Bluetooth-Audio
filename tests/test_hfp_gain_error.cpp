#include "owned/HfpControl.h"
#include "owned/HspControl.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace giga_bredr::owned;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);std::abort();}}while(0)
static void receive(HfpControl& h,uint32_t generation,const char* p,uint32_t now) {
  CHECK(h.receive(generation,reinterpret_cast<const uint8_t*>(p),std::strlen(p),now)==BTH_OK);
}
static void exchange(HfpControl& h,uint32_t generation,const char* expected,const char* reply,uint32_t now) {
  char bytes[128]{};size_t written=99;
  CHECK(h.next(generation,now,bytes,sizeof(bytes),written)==BTH_OK);
  CHECK(written==std::strlen(expected)&&std::memcmp(bytes,expected,written)==0);
  // None of the automatically generated procedures starts/answers/ends a call,
  // emits DTMF, redials, clicks HSP, or requests voice recognition.
  const std::string command(bytes,written);
  CHECK(command.find("ATD")==std::string::npos && command!="ATA\r");
  for(const auto* forbidden:{"CHUP","BLDN","VTS","CKPD","BVRA"})CHECK(command.find(forbidden)==std::string::npos);
  receive(h,generation,reply,now+1);
}
static void passive(bool backendWideband,bool peerWideband) {
  const uint32_t g=backendWideband?23:24;HfpControl h;
  CHECK(h.begin(g,backendWideband,6,7));
  exchange(h,g,backendWideband?"AT+BRSF=144\r":"AT+BRSF=16\r",peerWideband?"\r\n+BRSF: 512\r\nOK\r\n":"\r\n+BRSF: 0\r\nOK\r\n",10);
  if(backendWideband&&peerWideband)exchange(h,g,"AT+BAC=1,2\r","\r\nOK\r\n",20);
  exchange(h,g,"AT+CIND=?\r","\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",30);
  exchange(h,g,"AT+CIND?\r","\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n",40);
  exchange(h,g,"AT+CMER=3,0,0,1\r","\r\nOK\r\n",50);
  exchange(h,g,"AT+VGS=6\r","\r\nOK\r\n",60);
  exchange(h,g,"AT+VGM=7\r","\r\nOK\r\n",70);
  CHECK(h.ready()&&h.status().slcEstablished&&h.status().completedCommands==0);
  receive(h,g,"\r\nRING\r\n+CIEV: 3,1\r\n+VGS: 8\r\n+VGM: 9\r\n",80);
  char bytes[128];size_t written=999;
  for(uint32_t now=100;now<2100;++now)CHECK(h.next(g,now,bytes,sizeof(bytes),written)==BTH_OK&&written==0);
  CHECK(h.status().rings==1&&h.status().speakerGain==8&&h.status().microphoneGain==9&&h.status().completedCommands==0);
  receive(h,g,"\r\n+BCS: 2\r\n",2200);
  if(backendWideband&&peerWideband){exchange(h,g,"AT+BCS=2\r","\r\nOK\r\n",2210);CHECK(h.status().codec==VoiceCodec::Msbc);}
  else CHECK(h.next(g,2210,bytes,sizeof(bytes),written)==BTH_OK&&written==0&&h.status().codec==VoiceCodec::Cvsd);
  CHECK(h.status().completedCommands==0);
  h.end();CHECK(h.next(g,2300,bytes,sizeof(bytes),written)==BTH_IGNORED&&written==0);
  CHECK(h.begin(g+1,false,6,7));CHECK(h.next(g,2310,bytes,sizeof(bytes),written)==BTH_IGNORED&&written==0);
  exchange(h,g+1,"AT+BRSF=16\r","\r\n+BRSF: 0\r\nOK\r\n",2320);
}
static void gain_rejection(const char* speakerReply,const char* micReply,bool sr,bool mr) {
  HfpControl h;const uint32_t g=99;CHECK(h.begin(g,false,6,7));
  exchange(h,g,"AT+BRSF=16\r","\r\n+BRSF: 0\r\nOK\r\n",10);
  exchange(h,g,"AT+CIND=?\r","\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",20);
  exchange(h,g,"AT+CIND?\r","\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n",30);
  exchange(h,g,"AT+CMER=3,0,0,1\r","\r\nOK\r\n",40);
  CHECK(h.status().slcEstablished&&!h.ready());
  exchange(h,g,"AT+VGS=6\r",speakerReply,50);
  CHECK(h.status().slcEstablished&&h.status().speakerGainRejected==sr);
  exchange(h,g,"AT+VGM=7\r",micReply,60);
  CHECK(h.ready()&&h.status().slcEstablished&&h.status().speakerGainRejected==sr&&h.status().microphoneGainRejected==mr);
}
static void required_failure_and_timeout() {
 HfpControl a;CHECK(a.begin(100,false,6,7));exchange(a,100,"AT+BRSF=16\r","\r\nERROR\r\n",0);CHECK(a.status().state==HfpState::Failed&&!a.status().slcEstablished);
 HfpControl h;const uint32_t g=101;CHECK(h.begin(g,false,6,7));
 exchange(h,g,"AT+BRSF=16\r","\r\n+BRSF: 0\r\nOK\r\n",10);
 exchange(h,g,"AT+CIND=?\r","\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",20);
 exchange(h,g,"AT+CIND?\r","\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n",30);
 exchange(h,g,"AT+CMER=3,0,0,1\r","\r\nOK\r\n",40);
 char bytes[64];size_t n=0;CHECK(h.next(g,50,bytes,sizeof bytes,n)==BTH_OK&&n>0);CHECK(h.expire(g,5050)==BTH_OK);CHECK(h.status().state==HfpState::Failed&&!h.status().slcEstablished&&!h.status().speakerGainRejected&&!h.status().microphoneGainRejected);
}

static void invalid_cme(const char* reply,bool microphone) {
 HfpControl h;const uint32_t g=113;CHECK(h.begin(g,false,6,7));
 exchange(h,g,"AT+BRSF=16\r","\r\n+BRSF: 0\r\nOK\r\n",10);
 exchange(h,g,"AT+CIND=?\r","\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",20);
 exchange(h,g,"AT+CIND?\r","\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n",30);
 exchange(h,g,"AT+CMER=3,0,0,1\r","\r\nOK\r\n",40);
 if(microphone) exchange(h,g,"AT+VGS=6\r","\r\nOK\r\n",50);
 char out[64];size_t n=0;CHECK(h.next(g,60,out,sizeof out,n)==BTH_OK&&n>0);
 const auto result=h.receive(g,reinterpret_cast<const uint8_t*>(reply),std::strlen(reply),61);
 CHECK(result==BTH_ERR_PARAM);CHECK(h.status().state==HfpState::Failed&&!h.status().slcEstablished);
 CHECK(!h.status().speakerGainRejected&&!h.status().microphoneGainRejected);
 CHECK(h.next(g,62,out,sizeof out,n)==BTH_IGNORED);
}
static void microphone_timeout_after_speaker_reject() {
 HfpControl h;const uint32_t g=114;CHECK(h.begin(g,false,6,7));
 exchange(h,g,"AT+BRSF=16\r","\r\n+BRSF: 0\r\nOK\r\n",10);
 exchange(h,g,"AT+CIND=?\r","\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n",20);
 exchange(h,g,"AT+CIND?\r","\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n",30);
 exchange(h,g,"AT+CMER=3,0,0,1\r","\r\nOK\r\n",40);
 exchange(h,g,"AT+VGS=6\r","\r\nERROR\r\n",50);
 char out[64];size_t n=0;CHECK(h.next(g,60,out,sizeof out,n)==BTH_OK&&n>0);
 CHECK(h.expire(g,5060)==BTH_OK);CHECK(h.status().state==HfpState::Failed&&!h.status().slcEstablished&&h.status().speakerGainRejected&&!h.status().microphoneGainRejected);
 const char late[]="\r\nOK\r\n";CHECK(h.receive(g,reinterpret_cast<const uint8_t*>(late),sizeof(late)-1,5061)==BTH_IGNORED);
 CHECK(h.begin(g+1,false,6,7));CHECK(!h.status().speakerGainRejected&&!h.status().microphoneGainRejected);
}
int main(){
  required_failure_and_timeout();
  microphone_timeout_after_speaker_reject();
  for(const auto* malformed:{"\r\n+CME ERROR:\r\n","\r\n+CME ERROR:  \r\n","\r\n+CME ERROR: @@@\r\n","\r\n+CME ERROR: operation not allowed\r\n","\r\n+CME ERROR: 3junk\r\n","\r\n+CME ERROR: -1\r\n","\r\n+CME ERROR: 4294967296\r\n"}) { invalid_cme(malformed,false);invalid_cme(malformed,true); }
  gain_rejection("\r\n+CME ERROR: 0\r\n","\r\n+CME ERROR: \t4294967295  \r\n",true,true);
  gain_rejection("\r\nERROR\r\n","\r\nOK\r\n",true,false);
  gain_rejection("\r\nOK\r\n","\r\n+CME ERROR: 3\r\n",false,true);
  gain_rejection("\r\nERROR\r\n","\r\nERROR\r\n",true,true);

  passive(false,false);passive(false,true);passive(true,false);passive(true,true);
  HspControl h;CHECK(h.begin(44,5,6));char bytes[128];size_t written=77;
  const char event[]="\r\nRING\r\n+VGS=9\r\n+VGM=8\r\n";
  CHECK(h.receive(44,reinterpret_cast<const uint8_t*>(event),sizeof(event)-1,10)==BTH_OK);
  for(uint32_t now=20;now<2020;++now)CHECK(h.next(44,now,bytes,sizeof(bytes),written)==BTH_OK&&written==0);
  CHECK(h.status().rings==1&&h.status().speakerGain==9&&h.status().microphoneGain==8&&h.status().completedCommands==0);
  h.end();CHECK(h.next(44,3000,bytes,sizeof(bytes),written)==BTH_IGNORED&&written==0);
  std::printf("PASS_PASSIVE_HFP_HSP checks=%u synthetic_only=1 no_call_intent_submitted=1\n",checks);return 0;
}
