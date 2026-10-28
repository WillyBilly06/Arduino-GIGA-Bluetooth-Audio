#include "VoiceSdp.h"
namespace giga_bredr { namespace detail {
namespace {
size_t record(uint8_t* p,uint16_t uuid,uint8_t channel,uint16_t version,uint32_t handle,bool hf,bool wide){
 const uint8_t values[]={
  0x09,0,0,0x0a,uint8_t(handle>>24),uint8_t(handle>>16),uint8_t(handle>>8),uint8_t(handle),
  0x09,0,1,0x35,6,0x19,uint8_t(uuid>>8),uint8_t(uuid),0x19,0x12,3,
  0x09,0,4,0x35,12,0x35,3,0x19,1,0,0x35,5,0x19,0,3,8,channel,
  0x09,0,5,0x35,3,0x19,0x10,2,
  0x09,0,9,0x35,8,0x35,6,0x19,uint8_t(uuid>>8),uint8_t(uuid),0x09,uint8_t(version>>8),uint8_t(version),
  0x09,1,0,0x25,7,'G','I','G','A',' ','H',uint8_t(hf?'F':'S')};
 size_t n=0;for(auto b:values)p[n++]=b;
 if(hf){const uint8_t features[]={0x09,3,0x11,0x09,0,uint8_t(wide?0x30:0x10)};for(auto b:features)p[n++]=b;}
 else {const uint8_t gain[]={0x09,3,2,0x28,1};for(auto b:gain)p[n++]=b;}
 return n;
}
}
bool VoiceSdp::begin(const VoiceControlConfig& c,bool wide){
 count_=0;if((!c.hands_free&&!c.headset)||(c.hands_free&&(!c.hands_free_channel||c.hands_free_channel>30))||(c.headset&&(!c.headset_channel||c.headset_channel>30))||(c.hands_free&&c.headset&&c.hands_free_channel==c.headset_channel))return false;
 if(c.hands_free){const size_t n=record(bytes_[count_],0x111e,c.hands_free_channel,0x0106,0x10004,true,wide);records_[count_]={0x10004,{bytes_[count_],n}};++count_;}
 if(c.headset){const size_t n=record(bytes_[count_],0x1108,c.headset_channel,0x0102,0x10005,false,false);records_[count_]={0x10005,{bytes_[count_],n}};++count_;}
 bth_sdp_t checked{};return bth_sdp_init(&checked,records_,count_,1);
}
}}
