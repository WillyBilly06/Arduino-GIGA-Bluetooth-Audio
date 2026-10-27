#ifndef GIGA_BREDR_HFP_AUDIO_H
#define GIGA_BREDR_HFP_AUDIO_H
#include <cstddef>
#include <cstdint>
namespace giga_bredr { namespace owned {
// Voice codecs are independent of the A2DP Codec enum and its decoder arenas.
enum class VoiceCodec:uint8_t { Cvsd=1,Msbc=2 };
struct VoicePcmFormat { VoiceCodec codec;uint32_t sampleRateHz;uint16_t framesPerUnit;uint8_t channels; };
constexpr bool validVoicePcmFormat(VoicePcmFormat f){return f.channels==1&&((f.codec==VoiceCodec::Cvsd&&f.sampleRateHz==8000&&f.framesPerUnit>0&&f.framesPerUnit<=240)||(f.codec==VoiceCodec::Msbc&&f.sampleRateHz==16000&&f.framesPerUnit==120));}
constexpr int16_t voiceSilenceSample(){return 0;}
constexpr int32_t voiceMonoToStereoSlot(int16_t sample){return static_cast<int32_t>(sample)*65536;}
enum class VoiceIoResult:uint8_t { Ready,WouldBlock,End,Unavailable,Stale,Invalid };
enum class MicrophoneMode:uint8_t { None,Provider };
struct VoicePcmRequest { uint32_t audioGeneration,inputEpoch;uint64_t firstFrame;VoicePcmFormat format; };
struct VoicePcmRead { size_t frames;VoiceIoResult result; };
// Signed16 little-endian mono bytes; provider never writes beyond capacityFrames*2.
// Both callbacks are nonblocking, allocation/logging-free, invoked only by voice worker.
struct VoicePcmProvider {
 void* context=nullptr;
 size_t (*available)(void*,const VoicePcmRequest&)=nullptr;
 VoicePcmRead (*read)(void*,const VoicePcmRequest&,uint8_t*,size_t)=nullptr;
};
struct VoicePcmSink { void* context=nullptr;VoicePcmRead (*write)(void*,const VoicePcmRequest&,const uint8_t*,size_t)=nullptr; };
struct VoiceInputCounters { uint32_t noMicSilenceFrames=0,micStarvedFrames=0; };
// Standalone pull adapter; DOES NOT create a worker, microphone, SCO route or codec.
// Each same-session reconfiguration requires a strictly increasing epoch and retains
// fault counters. On epoch exhaustion, the outer owner must start a new audio generation.
// Configure/detach/read must run serially on one voice owner. detach return fences
// callbacks only under this owner contract; final counters remain readable.
// Counter history resets only for a new audio generation; it is not a cross-thread atomic API.
class HfpPcmInput {
public:
 bool configure(uint32_t generation,uint32_t epoch,VoicePcmFormat f,MicrophoneMode mode,VoicePcmProvider provider={}){
  if(!generation||!epoch||(generation==counterGeneration_&&epoch<=lastEpoch_)||!validVoicePcmFormat(f)||(mode!=MicrophoneMode::None&&mode!=MicrophoneMode::Provider)||(mode==MicrophoneMode::Provider&&(!provider.available||!provider.read)))return false;
  const VoiceInputCounters previous=generation==counterGeneration_?counters_:VoiceInputCounters{};
  detach();counters_=previous;counterGeneration_=generation;lastEpoch_=epoch;generation_=generation;epoch_=epoch;format_=f;mode_=mode;provider_=mode==MicrophoneMode::Provider?provider:VoicePcmProvider{};return true;
 }
 void detach(){provider_={};generation_=epoch_=0;first_=0;}
 VoicePcmRead read(uint32_t generation,uint32_t epoch,uint8_t* out,size_t frames){
  if(!generation_||generation!=generation_||epoch!=epoch_)return {0,VoiceIoResult::Stale};
  if(!out||!frames||frames>format_.framesPerUnit)return {0,VoiceIoResult::Invalid};
  VoicePcmRequest request{generation_,epoch_,first_,format_};
  if(mode_==MicrophoneMode::None){for(size_t i=0;i<frames*2;++i)out[i]=0;add(counters_.noMicSilenceFrames,frames);first_+=frames;return {frames,VoiceIoResult::Ready};}
  const size_t available=provider_.available(provider_.context,request);
  const size_t ask=available<frames?available:frames;
  VoicePcmRead result=ask?provider_.read(provider_.context,request,out,ask):VoicePcmRead{0,VoiceIoResult::WouldBlock};
  if(result.frames>ask)return {0,VoiceIoResult::Invalid};
  if(result.result!=VoiceIoResult::Ready&&result.result!=VoiceIoResult::WouldBlock&&result.result!=VoiceIoResult::End&&result.result!=VoiceIoResult::Unavailable)return {0,VoiceIoResult::Invalid};
  // Short active reads remain explicitly starved; caller chooses concealment policy.
  add(counters_.micStarvedFrames,frames-result.frames);first_+=result.frames;return result;
 }
 const VoiceInputCounters& counters()const{return counters_;}
private:
 static void add(uint32_t& n,size_t d){n=d>UINT32_MAX-n?UINT32_MAX:n+static_cast<uint32_t>(d);}
 VoicePcmProvider provider_{};VoicePcmFormat format_{VoiceCodec::Cvsd,8000,1,1};
 uint32_t generation_=0,epoch_=0,counterGeneration_=0,lastEpoch_=0;uint64_t first_=0;MicrophoneMode mode_=MicrophoneMode::None;VoiceInputCounters counters_{};
};
}}
#endif
