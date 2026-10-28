#include "VoiceMsbc.h"
#include "../CodecBuildConfig.h"
#include "../detail/Memory.h"
#include <cstring>
#include <new>
#if GBR_ENABLE_SBC
extern "C" {
#include "../vendor/codecs/sbc/include/oi_codec_sbc.h"
#include "../vendor/voice/sbc_encoder/include/sbc_encoder.h"
}
#endif
namespace giga_bredr { namespace detail {
namespace {
#if GBR_ENABLE_SBC
constexpr size_t DecoderBytes=sizeof(int32_t)*8*16+sizeof(SBC_BUFFER_T)*8*SBC_CODEC_FAST_FILTER_BUFFERS;
static_assert(sizeof(SINT16)==2&&sizeof(SINT32)==4&&sizeof(SINT64)==8,"Exact-width mSBC arithmetic required");
static_assert(DecoderBytes%4==0,"Aligned mono decoder storage");
#endif
constexpr uint8_t H2Sequence[]={0x08,0x38,0xc8,0xf8};
}
#if GBR_ENABLE_SBC
struct VoiceMsbc::State {
 SBC_ENC_PARAMS encoder{};OI_CODEC_SBC_DECODER_CONTEXT decoder{};
 alignas(4) OI_UINT32 work[DecoderBytes/4]{};
};
Status VoiceMsbc::begin(){
 if(state_)return Status::Busy;void* raw=allocate(sizeof(State),MemoryKind::Fast,alignof(State));if(!raw)return Status::OutOfMemory;
 State* next=new(raw)State();next->encoder.sbc_mode=SBC_MODE_MSBC;SBC_Encoder_Init(&next->encoder);
 if(OI_CODEC_SBC_DecoderResetGeometry(&next->decoder,next->work,DecoderBytes,1,1,0,1,8,16)!=OI_OK){next->~State();release(raw);return Status::Unsupported;}
 state_=next;return Status::Ok;
}
void VoiceMsbc::end(){if(state_){state_->~State();release(state_);state_=nullptr;}}
Status VoiceMsbc::encode(const int16_t* pcm,size_t frames,uint8_t* frame,size_t capacity){
 if(!state_||!pcm||!frame)return Status::InvalidArgument;
 if(frames<PcmFrames)return Status::NeedMoreData;if(frames!=PcmFrames)return Status::InvalidArgument;if(capacity<FrameBytes)return Status::OutputFull;
 auto& e=state_->encoder;e.ps16PcmBuffer=const_cast<SINT16*>(pcm);e.pu8Packet=frame;e.u16PacketLength=0;SBC_Encoder(&e);
 return e.u16PacketLength==FrameBytes&&frame[0]==0xad?Status::Ok:Status::CorruptFrame;
}
DecodeResult VoiceMsbc::decode(const uint8_t* frame,size_t length,int16_t* pcm,size_t capacity){
 if(!state_||!frame||!pcm)return {Status::InvalidArgument,0,0};
 if(length<FrameBytes)return {Status::NeedMoreData,0,0};if(length!=FrameBytes)return {Status::InvalidArgument,0,0};if(capacity<PcmFrames)return {Status::OutputFull,0,0};
 if(frame[0]!=0xad)return {Status::CorruptFrame,FrameBytes,0};
 const OI_BYTE* source=frame;OI_UINT32 bytes=FrameBytes,output=PcmFrames*sizeof(int16_t);
 const auto result=OI_CODEC_SBC_DecodeFrame(&state_->decoder,&source,&bytes,pcm,&output);
 if(result!=OI_OK||bytes||output!=PcmFrames*sizeof(int16_t))return {Status::CorruptFrame,FrameBytes,0};
 return {Status::Ok,FrameBytes,PcmFrames};
}
#else
Status VoiceMsbc::begin(){return Status::Unsupported;}
void VoiceMsbc::end(){state_=nullptr;}
Status VoiceMsbc::encode(const int16_t*,size_t,uint8_t*,size_t){return Status::Unsupported;}
DecodeResult VoiceMsbc::decode(const uint8_t*,size_t,int16_t*,size_t){return {Status::Unsupported,0,0};}
#endif
Status VoiceMsbc::packH2(uint8_t sequence,const uint8_t* frame,size_t length,uint8_t* packet,size_t capacity){
 if(sequence>=4||!frame||!packet||length!=FrameBytes||frame[0]!=0xad)return Status::InvalidArgument;if(capacity<PacketBytes)return Status::OutputFull;
 // Disjoint buffers are part of the codec/transport owner contract.
 packet[0]=1;packet[1]=H2Sequence[sequence];std::memcpy(packet+2,frame,FrameBytes);packet[59]=0;return Status::Ok;
}
Status VoiceMsbc::unpackH2(const uint8_t* packet,size_t length,uint8_t& sequence,const uint8_t*& frame){
 frame=nullptr;if(!packet)return Status::InvalidArgument;if(length<PacketBytes)return Status::NeedMoreData;if(length!=PacketBytes)return Status::InvalidArgument;
 if(packet[0]!=1||packet[2]!=0xad)return Status::CorruptFrame;
 for(uint8_t i=0;i<4;++i)if(packet[1]==H2Sequence[i]){sequence=i;frame=packet+2;return Status::Ok;}
 return Status::CorruptFrame;
}
}}
