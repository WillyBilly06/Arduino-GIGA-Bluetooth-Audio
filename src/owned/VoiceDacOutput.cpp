#include "VoiceDacOutput.h"
#include "../detail/Memory.h"
#include <cstring>
namespace giga_bredr { namespace detail {
namespace {const int32_t coefficients[32]={
#include "VoiceHalfband.inc"
};}
Status VoiceDacOutput::begin(owned::VoiceCodec codec,size_t(*writer)(const int32_t*,size_t,void*),void* user){
 if(storage_)return Status::Busy;if(!writer||(codec!=owned::VoiceCodec::Cvsd&&codec!=owned::VoiceCodec::Msbc))return Status::InvalidArgument;
 const size_t pcm_bytes=codec==owned::VoiceCodec::Cvsd?512:960;storage_=allocate(pcm_bytes+(codec==owned::VoiceCodec::Cvsd?64:0),MemoryKind::Fast);if(!storage_)return Status::OutOfMemory;
 std::memset(storage_,0,pcm_bytes+(codec==owned::VoiceCodec::Cvsd?64:0));pcm_=static_cast<int32_t*>(storage_);history_=codec==owned::VoiceCodec::Cvsd?reinterpret_cast<int16_t*>(static_cast<uint8_t*>(storage_)+pcm_bytes):nullptr;write_=writer;user_=user;codec_=codec;return Status::Ok;
}
void VoiceDacOutput::end(){release(storage_);storage_=nullptr;pcm_=nullptr;history_=nullptr;write_=nullptr;user_=nullptr;generation_=0;first_=0;input_frames_=output_frames_=cursor_=0;}
int16_t VoiceDacOutput::even(int16_t sample){
 for(unsigned n=31;n;--n)history_[n]=history_[n-1];history_[0]=sample;int64_t sum=0;for(unsigned n=0;n<32;++n)sum+=int64_t(history_[n])*coefficients[n];
 const int64_t rounded=sum>=0?(sum+(int64_t(1)<<29))/(int64_t(1)<<30):-((-sum+(int64_t(1)<<29))/(int64_t(1)<<30));return int16_t(rounded>32767?32767:rounded<-32768?-32768:rounded);
}
int32_t VoiceDacOutput::slot(int16_t sample,uint8_t gain)const{return int32_t(int64_t(sample)*65536*gain/15);}
owned::VoicePcmRead VoiceDacOutput::write(const owned::VoicePcmRequest& r,const uint8_t* data,size_t frames,uint8_t gain){
 using owned::VoiceIoResult;if(!storage_||!data||!frames||gain>15||!r.audioGeneration||r.format.codec!=codec_||!owned::validVoicePcmFormat(r.format))return {0,VoiceIoResult::Invalid};
 const size_t maximum=codec_==owned::VoiceCodec::Cvsd?32:120;
 if(input_frames_){if(generation_!=r.audioGeneration||first_!=r.firstFrame||frames<input_frames_)return {0,VoiceIoResult::Invalid};}
 else {
  generation_=r.audioGeneration;first_=r.firstFrame;input_frames_=frames<maximum?frames:maximum;output_frames_=0;cursor_=0;
  for(size_t n=0;n<input_frames_;++n){const int16_t sample=int16_t(uint16_t(data[2*n])|(uint16_t(data[2*n+1])<<8));
   const int16_t a=codec_==owned::VoiceCodec::Cvsd?even(sample):sample;const int32_t first=slot(a,gain);pcm_[2*output_frames_]=pcm_[2*output_frames_+1]=first;++output_frames_;
   if(codec_==owned::VoiceCodec::Cvsd){const int32_t second=slot(history_[15],gain);pcm_[2*output_frames_]=pcm_[2*output_frames_+1]=second;++output_frames_;}
  }
 }
 const size_t remaining=output_frames_-cursor_;const size_t sent=write_(pcm_+cursor_*2,remaining,user_);if(sent>remaining)return {0,VoiceIoResult::Invalid};cursor_+=sent;if(cursor_!=output_frames_)return {0,VoiceIoResult::WouldBlock};
 const size_t accepted=input_frames_;input_frames_=output_frames_=cursor_=0;return {accepted,VoiceIoResult::Ready};
}
}}
