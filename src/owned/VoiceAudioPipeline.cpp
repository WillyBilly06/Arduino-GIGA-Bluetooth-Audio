#include "VoiceAudioPipeline.h"
#include <cstring>
namespace giga_bredr { namespace detail {
namespace {
bool overlaps(const void* a,size_t an,const void* b,size_t bn){
 if(!a||!an||!b||!bn)return false;const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 if(an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return true;return x<y+bn&&y<x+an;
}
bool sequence(uint8_t value){return value==0x08||value==0x38||value==0xc8||value==0xf8;}
}
Status VoiceAudioPipeline::begin(uint32_t generation,uint32_t epoch,const VoicePipelineConfig& c,const VoicePipelineBuffers& b){
 if(active_)return Status::Busy;
 if(!generation||!epoch||!owned::validVoicePcmFormat(c.format)||!c.max_rx_bytes||c.max_rx_bytes>255||!c.sink.write||!b.rx_pcm||!b.packet||b.packet_bytes<c.max_rx_bytes)return Status::InvalidArgument;
 const bool wide=c.format.codec==owned::VoiceCodec::Msbc;const size_t native_rx=wide?120:(c.max_rx_bytes/2>c.format.framesPerUnit?c.max_rx_bytes/2:c.format.framesPerUnit);
 if(b.rx_frames<native_rx||(wide&&(!b.tx_pcm||b.tx_frames<120||!b.h2||b.h2_bytes<60)))return Status::InvalidArgument;
 const void* ranges[]={b.rx_pcm,b.packet,wide?b.tx_pcm:nullptr,wide?b.h2:nullptr};const size_t sizes[]={native_rx*2,c.max_rx_bytes,wide?240u:0u,wide?60u:0u};
 for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)if(overlaps(ranges[i],sizes[i],ranges[j],sizes[j]))return Status::InvalidArgument;
 if(wide){const auto result=codec_.begin();if(result!=Status::Ok)return result;}
 if(!input_.configure(generation,epoch,c.format,c.input_mode,c.input)){codec_.end();return Status::InvalidArgument;}
 config_=c;buffers_=b;generation_=generation;input_epoch_=epoch;stats_={};packet_size_=packet_cursor_=pcm_size_=pcm_cursor_=h2_used_=0;packet_status_=tx_sequence_=expected_sequence_=0;packet_pending_=have_sequence_=false;active_=true;return Status::Ok;
}
void VoiceAudioPipeline::end(){active_=false;input_.detach();codec_.end();generation_=input_epoch_=0;packet_pending_=false;packet_size_=packet_cursor_=pcm_size_=pcm_cursor_=h2_used_=0;config_={};buffers_={};}
Status VoiceAudioPipeline::setInput(uint32_t generation,uint32_t epoch,owned::MicrophoneMode mode,owned::VoicePcmProvider provider){
 if(!active_||generation!=generation_)return Status::Unsupported;if(!input_.configure(generation,epoch,config_.format,mode,provider))return Status::InvalidArgument;
 input_epoch_=epoch;return Status::Ok;
}
Status VoiceAudioPipeline::renderTx(uint32_t generation,uint8_t* out,size_t capacity,size_t& written){
 written=0;if(!active_||generation!=generation_)return Status::Unsupported;if(!out)return Status::InvalidArgument;
 const size_t needed=msbc()?60:size_t(config_.format.framesPerUnit)*2;if(capacity<needed)return Status::OutputFull;
 if(overlaps(out,needed,buffers_.packet,config_.max_rx_bytes)||overlaps(out,needed,buffers_.rx_pcm,buffers_.rx_frames*2)||
    (msbc()&&(overlaps(out,needed,buffers_.tx_pcm,240)||overlaps(out,needed,buffers_.h2,60))))return Status::InvalidArgument;
 uint8_t* pcm=msbc()?reinterpret_cast<uint8_t*>(buffers_.tx_pcm):out;
 const auto read=input_.readPadded(generation_,input_epoch_,pcm,config_.format.framesPerUnit);
 if(read.result!=owned::VoiceIoResult::Ready||read.frames!=config_.format.framesPerUnit)return Status::InvalidArgument;
 if(microphone_gain_!=15)for(size_t i=0;i<config_.format.framesPerUnit;++i){const int16_t sample=int16_t(uint16_t(pcm[2*i])|(uint16_t(pcm[2*i+1])<<8));const int16_t scaled=int16_t(int32_t(sample)*microphone_gain_/15);pcm[2*i]=uint8_t(scaled);pcm[2*i+1]=uint8_t(uint16_t(scaled)>>8);}
 if(msbc()){
  uint8_t frame[57];const auto encoded=codec_.encode(buffers_.tx_pcm,120,frame,sizeof frame);if(encoded!=Status::Ok)return encoded;
  const auto packet=VoiceMsbc::packH2(tx_sequence_,frame,sizeof frame,out,capacity);if(packet!=Status::Ok)return packet;tx_sequence_=uint8_t((tx_sequence_+1)&3);
 }
 written=needed;++stats_.tx_units;return Status::Ok;
}
Status VoiceAudioPipeline::receive(uint32_t generation,uint8_t status,const uint8_t* data,size_t bytes){
 if(!active_||generation!=generation_)return Status::Unsupported;if(packet_pending_)return Status::OutputFull;
 if(status>3||bytes>config_.max_rx_bytes||(bytes&&!data)||(!msbc()&&!status&&(bytes&1)))return Status::InvalidArgument;
 if(bytes)std::memmove(buffers_.packet,data,bytes);packet_size_=bytes;packet_cursor_=0;packet_status_=status;packet_pending_=true;++stats_.rx_packets;return Status::Ok;
}
Status VoiceAudioPipeline::flush(){
 if(pcm_cursor_==pcm_size_)return Status::NeedMoreData;
 const owned::VoicePcmRequest request{generation_,0,stats_.delivered_frames,config_.format};const size_t remaining=pcm_size_-pcm_cursor_;
 const auto result=config_.sink.write(config_.sink.context,request,reinterpret_cast<const uint8_t*>(buffers_.rx_pcm+pcm_cursor_),remaining);
 if(result.frames>remaining||(result.result!=owned::VoiceIoResult::Ready&&result.result!=owned::VoiceIoResult::WouldBlock&&result.result!=owned::VoiceIoResult::End&&result.result!=owned::VoiceIoResult::Unavailable))return Status::InvalidArgument;
 pcm_cursor_+=result.frames;stats_.delivered_frames+=result.frames;if(pcm_cursor_==pcm_size_)pcm_cursor_=pcm_size_=0;
 return result.frames?Status::Ok:Status::Busy;
}
Status VoiceAudioPipeline::prepareRx(){
 if(!packet_pending_)return Status::NeedMoreData;
 if(packet_status_){
  ++stats_.packet_errors;packet_pending_=false;h2_used_=0;
  if(msbc())return Status::NeedMoreData; // Next validated H2 sequence reports whole-unit gaps.
  pcm_size_=packet_size_?packet_size_/2:config_.format.framesPerUnit;pcm_cursor_=0;for(size_t i=0;i<pcm_size_;++i)buffers_.rx_pcm[i]=0;
  ++stats_.rx_units;stats_.concealed_frames+=pcm_size_;return Status::Ok;
 }
 if(!msbc()){
  pcm_size_=packet_size_/2;pcm_cursor_=0;for(size_t i=0;i<pcm_size_;++i)buffers_.rx_pcm[i]=int16_t(uint16_t(buffers_.packet[2*i])|(uint16_t(buffers_.packet[2*i+1])<<8));
  packet_pending_=false;++stats_.rx_units;stats_.decoded_frames+=pcm_size_;return pcm_size_?Status::Ok:Status::NeedMoreData;
 }
 while(packet_cursor_<packet_size_){
  const uint8_t byte=buffers_.packet[packet_cursor_++];
  if(!h2_used_&&byte!=1){++stats_.h2_discarded_bytes;continue;}
  if(h2_used_==1&&!sequence(byte)){++stats_.h2_discarded_bytes;h2_used_=byte==1?1:0;continue;}
  if(h2_used_==2&&byte!=0xad){stats_.h2_discarded_bytes+=2;h2_used_=byte==1?1:0;continue;}
  buffers_.h2[h2_used_++]=byte;if(h2_used_!=60)continue;
  uint8_t sequence_number=0;const uint8_t* frame=nullptr;const auto header=VoiceMsbc::unpackH2(buffers_.h2,60,sequence_number,frame);h2_used_=0;
  if(header!=Status::Ok){stats_.h2_discarded_bytes+=60;continue;}
  if(have_sequence_)stats_.h2_gaps+=uint8_t((sequence_number-expected_sequence_)&3);have_sequence_=true;expected_sequence_=uint8_t((sequence_number+1)&3);
  const auto decoded=codec_.decode(frame,57,buffers_.rx_pcm,buffers_.rx_frames);pcm_size_=120;pcm_cursor_=0;++stats_.rx_units;
  if(decoded.status!=Status::Ok){++stats_.decode_errors;stats_.concealed_frames+=120;for(size_t i=0;i<120;++i)buffers_.rx_pcm[i]=0;}else stats_.decoded_frames+=120;
  if(packet_cursor_==packet_size_)packet_pending_=false;return Status::Ok;
 }
 packet_pending_=false;return Status::NeedMoreData;
}
Status VoiceAudioPipeline::poll(uint32_t generation){
 if(!active_||generation!=generation_)return Status::Unsupported;if(pcm_size_)return flush();
 const auto result=prepareRx();if(result!=Status::Ok)return result;return flush();
}
}}
