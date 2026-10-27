// Pure protocol fixture: Root must execute hfpControlSelftest(), not merely compile it.
#if defined(__has_include)
# if __has_include("../src/owned/HfpControl.h") && __has_include("../src/owned/HspControl.h") && __has_include("../src/owned/HfpAudio.h")
#  include "../src/owned/HfpControl.h"
#  include "../src/owned/HspControl.h"
#  include "../src/owned/HfpAudio.h"
#  define HAVE_OWN_HF_CONTROL 1
# endif
#endif
#ifndef HAVE_OWN_HF_CONTROL
static_assert(false,"Missing bounded own HF and HS control modules");
#else
#include <cstring>
using namespace giga_bredr::owned;
namespace { unsigned failures;char tx[128];size_t used;
#define CHECK(x) do { if(!(x))++failures; } while(0)
void feed(HfpControl& h,const char* s,uint32_t now=1){CHECK(h.receive(11,reinterpret_cast<const uint8_t*>(s),std::strlen(s),now)==BTH_OK);}
void next(HfpControl& h,const char* expected,uint32_t now=1){CHECK(h.next(11,now,tx,sizeof tx,used)==BTH_OK&&used==std::strlen(expected)&&!std::memcmp(tx,expected,used));}
size_t micCalls=0;
size_t micAvailable(void*,const VoicePcmRequest&){++micCalls;return 0;}
VoicePcmRead micRead(void*,const VoicePcmRequest&,uint8_t*,size_t){++micCalls;return {0,VoiceIoResult::WouldBlock};}
const char* defs="\r\n+CIND: (\"signal\",(0-5)),(\"callsetup\",(0-3)),(\"service\",(0,1)),(\"call\",(0,1)),(\"callheld\",(0-2)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n";
const char* extraDefs="\r\n+CIND: (\"signal\",(0,1,2,3,4,5)),(\"callsetup\",(0-3)),(\"service\",(0,1)),(\"call\",(0,1)),(\"callheld\",(0-2)),(\"roam\",(0,1)),(\"battchg\",(0-5)),(\"extra\",(2,4))\r\nOK\r\n";
void slc(HfpControl& h,bool wb=false,const char* definitions=defs,const char* initialValues="\r\n+CIND: 4,0,1,0,0,0,5\r\nOK\r\n"){CHECK(h.begin(11,wb,8,9));next(h,wb?"AT+BRSF=144\r":"AT+BRSF=16\r");feed(h,"\r\n+BRSF: 512\r\nOK\r\n");if(wb){next(h,"AT+BAC=1,2\r");feed(h,"\r\nOK\r\n");}next(h,"AT+CIND=?\r");feed(h,definitions);next(h,"AT+CIND?\r");feed(h,initialValues);next(h,"AT+CMER=3,0,0,1\r");feed(h,"\r\nOK\r\n");next(h,"AT+VGS=8\r");feed(h,"\r\nOK\r\n");next(h,"AT+VGM=9\r");feed(h,"\r\nOK\r\n");CHECK(h.ready()&&h.status().indicators[2]==1);}
}
unsigned hfpControlSelftest(){
 failures=0;HfpControl h;slc(h); CHECK(h.status().codec==VoiceCodec::Cvsd&&h.status().generation==11);
 feed(h,"\r\n+CIEV: 2,1\r\nRING\r\n+VGS: 15\r\n+BSIR: 1\r\n");CHECK(h.status().indicators[1]==1&&h.status().rings==1&&h.status().speakerGain==15&&h.status().inBandRing);
 uint8_t indication=99;CHECK(h.indicator(HfpIndicator::Service,indication)&&indication==1);CHECK(h.indicator(HfpIndicator::CallSetup,indication)&&indication==1);CHECK(!h.indicator(static_cast<HfpIndicator>(255),indication));
 CHECK(h.receive(12,reinterpret_cast<const uint8_t*>("RING\r\n"),6,2)==BTH_IGNORED&&h.status().rings==1);
 CHECK(h.command(11,HfpCommand::Dial,"123\rAT+CHUP")==BTH_ERR_PARAM);
 CHECK(h.command(11,HfpCommand::Dial,"+123#")==BTH_OK);CHECK(h.command(11,HfpCommand::Answer,nullptr)==BTH_MORE);
 CHECK(h.next(11,2,tx,2,used)==BTH_ERR_CAPACITY&&!used);next(h,"ATD+123#;\r",2);feed(h,"\r\nOK\r\n",3);CHECK(h.status().lastCommandOk);
 CHECK(h.command(11,HfpCommand::Answer,nullptr)==BTH_OK);next(h,"ATA\r",4);feed(h,"\r\nERROR\r\n",5);CHECK(h.ready()&&!h.status().lastCommandOk);
 CHECK(h.command(11,HfpCommand::Dtmf,"A")==BTH_OK);next(h,"AT+VTS=A\r",6);CHECK(h.expire(11,5007)==BTH_OK&&!h.status().lastCommandOk);
 slc(h,false,extraDefs,"\r\n+CIND: 4,0,1,0,0,0,5,2\r\nOK\r\n");feed(h,"\r\n+CIEV: 8,4\r\n");
 const char invalidExtra[]="\r\n+CIEV: 8,3\r\n";CHECK(h.receive(11,reinterpret_cast<const uint8_t*>(invalidExtra),sizeof invalidExtra-1,7)==BTH_ERR_PARAM&&h.status().state==HfpState::Failed);
 slc(h,true);feed(h,"\r\n+BCS: 2\r\n");next(h,"AT+BCS=2\r");feed(h,"\r\nOK\r\n");CHECK(h.status().codec==VoiceCodec::Msbc);
 feed(h,"\r\n+BCS: 3\r\n");next(h,"AT+BAC=1,2\r");feed(h,"\r\nOK\r\n");CHECK(h.status().codec==VoiceCodec::Msbc);
 h.end();CHECK(!h.ready());CHECK(h.begin(11,false,8,9));next(h,"AT+BRSF=16\r");feed(h,"\r\nOK\r\n");CHECK(h.status().state==HfpState::Failed);
 CHECK(h.begin(11,false,8,9));next(h,"AT+BRSF=16\r",0xfffffff0u);CHECK(h.expire(11,0x20)==BTH_MORE);CHECK(h.expire(11,5000)==BTH_OK&&h.status().state==HfpState::Failed);
 CHECK(h.begin(11,false,8,9));next(h,"AT+BRSF=16\r");feed(h,"\r\n+BR");feed(h,"SF: 0\r");feed(h,"\nOK\r\n");next(h,"AT+CIND=?\r");
 char oversized[400];std::memset(oversized,'x',sizeof oversized);CHECK(h.receive(11,reinterpret_cast<uint8_t*>(oversized),sizeof oversized,3)==BTH_ERR_LENGTH);CHECK(h.status().state==HfpState::Failed);
 HspControl hs;CHECK(hs.begin(5,8,9));CHECK(hs.ready());CHECK(hs.button(5)==BTH_OK);CHECK(hs.next(5,10,tx,sizeof tx,used)==BTH_OK&&used==12&&!std::memcmp(tx,"AT+CKPD=200\r",12));
 CHECK(hs.receive(5,reinterpret_cast<const uint8_t*>("\r\nOK\r\nRING\r\n+VGM=3\r\n"),20,11)==BTH_OK);
 CHECK(hs.status().rings==1&&hs.status().microphoneGain==3);
 CHECK(hs.setGain(5,true,16)==BTH_ERR_PARAM);CHECK(hs.setGain(5,true,7)==BTH_OK);CHECK(hs.next(5,12,tx,sizeof tx,used)==BTH_OK&&used==9&&!std::memcmp(tx,"AT+VGS=7\r",9));hs.end();CHECK(!hs.ready());
 VoicePcmFormat f{VoiceCodec::Msbc,16000,120,1};CHECK(validVoicePcmFormat(f));f.sampleRateHz=48000;CHECK(!validVoicePcmFormat(f));
 CHECK(voiceSilenceSample()==0&&voiceMonoToStereoSlot(-32768)==static_cast<int32_t>(0x80000000u)&&voiceMonoToStereoSlot(32767)==2147418112);
 HfpPcmInput input;uint8_t pcm[240];std::memset(pcm,0x5a,sizeof pcm);VoicePcmFormat mf{VoiceCodec::Msbc,16000,120,1};
 CHECK(input.configure(7,2,mf,MicrophoneMode::None));auto pr=input.read(7,2,pcm,120);CHECK(pr.frames==120&&pr.result==VoiceIoResult::Ready&&input.counters().noMicSilenceFrames==120&&input.counters().micStarvedFrames==0);
 for(unsigned i=0;i<sizeof pcm;++i)CHECK(pcm[i]==0);CHECK(input.read(7,3,pcm,120).result==VoiceIoResult::Stale);CHECK(input.read(7,2,pcm,121).result==VoiceIoResult::Invalid);input.detach();CHECK(input.read(7,2,pcm,120).result==VoiceIoResult::Stale);
 CHECK(!input.configure(7,2,mf,MicrophoneMode::Provider));
 VoicePcmProvider provider{nullptr,micAvailable,micRead};CHECK(input.configure(8,1,mf,MicrophoneMode::Provider,provider));pr=input.read(8,1,pcm,120);CHECK(pr.frames==0&&pr.result==VoiceIoResult::WouldBlock&&input.counters().micStarvedFrames==120);
 CHECK(input.configure(8,2,mf,MicrophoneMode::None,provider));pr=input.read(8,2,pcm,120);CHECK(pr.frames==120&&input.counters().micStarvedFrames==120&&input.counters().noMicSilenceFrames==120&&micCalls==1);
 CHECK(!input.configure(8,2,mf,MicrophoneMode::None));
 input.detach();CHECK(input.counters().micStarvedFrames==120);CHECK(input.configure(8,3,mf,MicrophoneMode::None));pr=input.read(8,3,pcm,120);CHECK(input.counters().micStarvedFrames==120&&input.counters().noMicSilenceFrames==240);
 return failures;
}
#ifdef GIGA_BREDR_PORTABLE_TEST_MAIN
int main(){return hfpControlSelftest()?1:0;}
#endif
#endif
