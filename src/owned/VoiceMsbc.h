#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
// Dedicated mono16kHz/120-sample voice codec, independent of A2DP selection.
// One tracked context is allocated on begin; all analysis/decoder histories
// belong to that instance and are released on end. No radio or worker starts.
class VoiceMsbc {
public:
 static constexpr size_t PcmFrames=120,FrameBytes=57,PacketBytes=60;
 VoiceMsbc()=default;~VoiceMsbc(){end();}
 VoiceMsbc(const VoiceMsbc&)=delete;VoiceMsbc& operator=(const VoiceMsbc&)=delete;
 Status begin();void end();
 Status encode(const int16_t* pcm,size_t frames,uint8_t* frame,size_t capacity);
 DecodeResult decode(const uint8_t* frame,size_t length,int16_t* pcm,size_t capacity_frames);
 static Status packH2(uint8_t sequence,const uint8_t* frame,size_t length,uint8_t* packet,size_t capacity);
 static Status unpackH2(const uint8_t* packet,size_t length,uint8_t& sequence,const uint8_t*& frame);
private:
 struct State;State* state_=nullptr;
};
}}
