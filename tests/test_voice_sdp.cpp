#include "../src/owned/VoiceSdp.h"
#include <cstring>
using namespace giga_bredr;using namespace giga_bredr::detail;
namespace {unsigned checks;
#define CHECK(x) do{++checks;if(!(x))return __LINE__;}while(0)
}
extern "C" unsigned voiceSdpTest(){checks=0;VoiceSdp voice;VoiceControlConfig c;c.hands_free=c.headset=true;CHECK(voice.begin(c,true)&&voice.count()==2);bth_sdp_t server{};CHECK(bth_sdp_init(&server,voice.records(),voice.count(),19));
 const uint8_t query[]={2,0,1,0,8,0x35,3,0x19,0x11,0x1e,0,4,0};uint8_t out[112];size_t n=0;CHECK(bth_sdp_process(&server,query,sizeof query,out,sizeof out,&n)==BTH_OK&&n==14&&out[0]==3&&out[5]==0&&out[6]==1&&out[7]==0&&out[8]==1&&out[9]==0&&out[10]==1&&out[11]==0&&out[12]==4);
 uint8_t hs[sizeof query];std::memcpy(hs,query,sizeof hs);hs[9]=8;CHECK(bth_sdp_process(&server,hs,sizeof hs,out,sizeof out,&n)==BTH_OK&&n==14&&out[12]==5);
 const auto records=voice.records();const uint8_t rfcomm[]={0x35,5,0x19,0,3,8,1};bool channel=false,features=false;for(size_t i=0;i+sizeof rfcomm<=records[0].attributes.len;++i)if(!std::memcmp(records[0].attributes.data+i,rfcomm,sizeof rfcomm))channel=true;
 const uint8_t caps[]={0x09,3,0x11,0x09,0,0x30};for(size_t i=0;i+sizeof caps<=records[0].attributes.len;++i)if(!std::memcmp(records[0].attributes.data+i,caps,sizeof caps))features=true;CHECK(channel&&features);
 CHECK(voice.begin(c,false));bool disabled=false;const uint8_t narrow[]={0x09,3,0x11,0x09,0,0x10};for(size_t i=0;i+sizeof narrow<=voice.records()[0].attributes.len;++i)if(!std::memcmp(voice.records()[0].attributes.data+i,narrow,sizeof narrow))disabled=true;CHECK(disabled);c.headset_channel=c.hands_free_channel;CHECK(!voice.begin(c,true));return 0;}
extern "C" unsigned voiceSdpChecks(){return checks;}
