#include <GigaBredr.h>
#include "Console.h"
using namespace giga_bredr;

GigaBredr receiver;
ReceiverConsole console;

void metadataChanged(uint8_t attribute, const char* text, size_t bytes, void*) {
  console.print("METADATA attribute=%u bytes=%u text=%.*s\n",
                unsigned(attribute), unsigned(bytes), int(bytes), text);
}

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name="GIGA Audio Remote";
  config.ble.enabled=true;
  config.ble.maximum_mtu=517;
  config.metadata_changed=metadataChanged;
  config.i2s.bclk=70; config.i2s.data=72; config.i2s.lrck=74; config.i2s.mclk=-1;
  const auto result=receiver.begin(config);
  console.print("BEGIN %s; p=play a=pause s=stop n=next b=previous\n", statusName(result));
}

void loop() {
  receiver.poll();
  // One explicit key requests one tap. Busy is reported, never replayed.
  for(unsigned budget=0;budget<8 && Serial.available();++budget) {
    const int key=Serial.read();
    AvrcpCommand command;
    switch(key) {
      case 'p': command=AvrcpCommand::Play; break;
      case 'a': command=AvrcpCommand::Pause; break;
      case 's': command=AvrcpCommand::Stop; break;
      case 'n': command=AvrcpCommand::Next; break;
      case 'b': command=AvrcpCommand::Previous; break;
      default: continue;
    }
    const auto peer=receiver.avrcpStatus();
    if(!peer.connected) { console.print("AVRCP detached\n"); continue; }
    uint32_t request=0;
    const auto result=receiver.avrcpCommand(command,peer.generation,request);
    console.print("TAP generation=%lu request=%lu status=%s\n",
                  (unsigned long)peer.generation,(unsigned long)request,statusName(result));
  }
  static uint32_t previous=0;
  const uint32_t now=millis();
  if(uint32_t(now-previous)>=1000) {
    previous=now;
    const auto peer=receiver.avrcpStatus();
    console.print("AVRCP connected=%u generation=%lu playback=%u position_ms=%lu command=%lu/%lu state=%u responses=%02x/%02x console_drops=%u\n",
                  unsigned(peer.connected),(unsigned long)peer.generation,unsigned(peer.playback_status),
                  (unsigned long)peer.song_position_ms,(unsigned long)peer.command_generation,
                  (unsigned long)peer.command_id,unsigned(peer.command_state),
                  unsigned(peer.press_response),unsigned(peer.release_response),console.dropped);
  }
  console.pump();
  delay(1);
}
