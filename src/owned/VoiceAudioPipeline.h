#pragma once
#include "HfpAudio.h"
#include "VoiceMsbc.h"
namespace giga_bredr { namespace detail {
struct VoicePipelineConfig {
 owned::VoicePcmFormat format{owned::VoiceCodec::Cvsd,8000,30,1};
 uint16_t max_rx_bytes=64;owned::MicrophoneMode input_mode=owned::MicrophoneMode::None;
 owned::VoicePcmProvider input{};owned::VoicePcmSink sink{};
};
// Selected-format storage belongs to the caller and remains alive through end.
// CVSD needs no TX scratch or H2 buffer. mSBC requires120 mono TX/RX samples.
struct VoicePipelineBuffers {
 int16_t* rx_pcm=nullptr;size_t rx_frames=0;int16_t* tx_pcm=nullptr;size_t tx_frames=0;
 uint8_t* packet=nullptr;size_t packet_bytes=0;uint8_t* h2=nullptr;size_t h2_bytes=0;
};
struct VoicePipelineStats {
 uint32_t rx_packets=0,rx_units=0,packet_errors=0,decode_errors=0,h2_discarded_bytes=0,h2_gaps=0,tx_units=0;
 uint64_t decoded_frames=0,delivered_frames=0,concealed_frames=0;
};
// Single serialized audio-worker component. Never call from the radio owner,
// an IRQ or concurrently with provider/sink callbacks. No thread, I2S clock,
// controller configuration, service advertisement or public registration starts.
class VoiceAudioPipeline {
public:
 const owned::VoicePcmFormat& format()const{return config_.format;}
 bool microphoneGain(uint8_t gain){if(gain>15)return false;microphone_gain_=gain;return true;}
 Status begin(uint32_t audio_generation,uint32_t input_epoch,const VoicePipelineConfig&,const VoicePipelineBuffers&);
 void end();
 Status setInput(uint32_t audio_generation,uint32_t new_epoch,owned::MicrophoneMode,owned::VoicePcmProvider={});
 Status renderTx(uint32_t audio_generation,uint8_t* unit,size_t capacity,size_t& written);
 Status receive(uint32_t audio_generation,uint8_t packet_status,const uint8_t*,size_t);
 Status poll(uint32_t audio_generation);
 bool canReceive()const{return active_&&!packet_pending_;}
 VoicePipelineStats stats()const{return stats_;}
 owned::VoiceInputCounters inputCounters()const{return input_.counters();}
private:
 Status flush();Status prepareRx();bool msbc()const{return config_.format.codec==owned::VoiceCodec::Msbc;}
 VoicePipelineConfig config_{};VoicePipelineBuffers buffers_{};VoicePipelineStats stats_{};
 owned::HfpPcmInput input_{};VoiceMsbc codec_{};
 uint32_t generation_=0,input_epoch_=0;size_t packet_size_=0,packet_cursor_=0,pcm_size_=0,pcm_cursor_=0,h2_used_=0;
 uint8_t packet_status_=0,tx_sequence_=0,expected_sequence_=0,microphone_gain_=15;bool active_=false,packet_pending_=false,have_sequence_=false;
};
}}
