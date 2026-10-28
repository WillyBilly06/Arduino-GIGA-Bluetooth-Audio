#pragma once
#include "Types.h"
#include "owned/HfpAudio.h"
#include "owned/HfpControl.h"
#include "owned/HspControl.h"
namespace giga_bredr {
using VoiceCodec=owned::VoiceCodec;using VoicePcmFormat=owned::VoicePcmFormat;
using VoicePcmRequest=owned::VoicePcmRequest;using VoicePcmRead=owned::VoicePcmRead;
using VoiceIoResult=owned::VoiceIoResult;using VoicePcmProvider=owned::VoicePcmProvider;
using HfpCommand=owned::HfpCommand;
struct VoiceInputToken {uint64_t lifetime=0;uint32_t epoch=0;};
struct VoiceConfig {
 bool hands_free=false,headset=false,wideband=true;
 // Default never writes a vendor route. Explicit opt-in permits only a known
 // readback-bound Broadcom PCM route change, preserving its other parameters.
 bool allow_controller_route_configuration=false,allow_rx_pacing=false;
 uint8_t hands_free_channel=1,headset_channel=2,speaker_gain=15,microphone_gain=15;
 uint16_t pcm_buffer_ms=40,startup_buffer_ms=12;
 bool enabled()const{return hands_free||headset;}
};
struct VoiceStatus {
 bool enabled=false,controller_ready=false,backend_available=false,wideband_available=false,audio_connected=false;
 uint32_t generation=0;VoiceCodec codec=VoiceCodec::Cvsd;uint32_t sample_rate_hz=8000,dac_sample_rate_hz=16000;uint8_t bits_per_sample=16;
 Status worker_status=Status::Busy;uint8_t setup_reason=0,controller_status=0;
 owned::HfpStatus hands_free;owned::HspStatus headset;owned::VoiceInputCounters input;
 uint32_t received_packets=0,submitted_packets=0,packet_errors=0,decode_errors=0,h2_gaps=0;
 uint64_t delivered_pcm_frames=0,concealed_pcm_frames=0,preempted_a2dp_packets=0;
};
}
