#include <GigaBredr.h>
#include "Console.h"
using namespace giga_bredr;

GigaBredr receiver;
ReceiverConsole console;

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name="GIGA Hands-Free";
  config.ble.enabled=true;
  config.ble.maximum_mtu=517;
  config.i2s.bclk=70; config.i2s.data=72; config.i2s.lrck=74; config.i2s.mclk=-1;
  config.voice.hands_free=true;
  config.voice.headset=true;
  config.voice.wideband=true;
  // Explicit example opt-in: only the known controller's readback-bound PCM
  // route may change. Failed/unknown setup leaves the voice backend unavailable.
  config.voice.allow_controller_route_configuration=true;
  const auto result=receiver.begin(config);
  console.print("BEGIN %s; a=answer h=hangup; input=None (silence)\n",statusName(result));
}

void loop() {
  receiver.poll();
  for(unsigned budget=0;budget<8 && Serial.available();++budget) {
    const int key=Serial.read();
    if(key!='a' && key!='h')continue;
    VoiceStatus voice;
    const auto snapshot=receiver.voiceStatus(voice);
    if(snapshot!=Status::Ok || !voice.hands_free.slcEstablished) {
      console.print("HFP not ready status=%s\n",statusName(snapshot));
      continue;
    }
    const auto result=receiver.voiceCommand(key=='a'?HfpCommand::Answer:HfpCommand::Hangup,
                                            voice.hands_free.generation);
    console.print("HFP action=%c generation=%lu status=%s\n",key,
                  (unsigned long)voice.hands_free.generation,statusName(result));
  }
  static uint32_t previous=0;
  const uint32_t now=millis();
  if(uint32_t(now-previous)>=1000) {
    previous=now;
    VoiceStatus voice;
    const auto result=receiver.voiceStatus(voice);
    if(result==Status::Ok) {
      console.print("VOICE enabled=%u controller=%u backend=%u wideband=%u audio=%u generation=%lu rate=%lu dac_rate=%lu setup=%u/%u worker=%s rx=%lu tx=%lu errors=%lu/%lu\n",
                    unsigned(voice.enabled),unsigned(voice.controller_ready),unsigned(voice.backend_available),
                    unsigned(voice.wideband_available),unsigned(voice.audio_connected),
                    (unsigned long)voice.generation,(unsigned long)voice.sample_rate_hz,
                    (unsigned long)voice.dac_sample_rate_hz,unsigned(voice.setup_reason),
                    unsigned(voice.controller_status),statusName(voice.worker_status),
                    (unsigned long)voice.received_packets,(unsigned long)voice.submitted_packets,
                    (unsigned long)voice.packet_errors,(unsigned long)voice.decode_errors);
      console.print("HFP generation=%lu state=%u slc=%u rings=%lu command_ok=%u console_drops=%u\n",
                    (unsigned long)voice.hands_free.generation,unsigned(voice.hands_free.state),
                    unsigned(voice.hands_free.slcEstablished),(unsigned long)voice.hands_free.rings,
                    unsigned(voice.hands_free.lastCommandOk),console.dropped);
    } else console.print("VOICE status=%s\n",statusName(result));
  }
  console.pump();
  delay(1);
}
