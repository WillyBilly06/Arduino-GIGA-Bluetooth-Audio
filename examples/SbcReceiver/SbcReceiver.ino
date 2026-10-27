#include <GigaBredr.h>
using namespace giga_bredr;
GigaBredr receiver;

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name = "GIGA SBC Receiver";
  config.codecs = codecBit(Codec::Sbc); // Includes SBC-XQ bitstreams.
  Serial.println(statusName(receiver.begin(config)));
}
void loop() {
  receiver.poll();
  delay(1);
}
