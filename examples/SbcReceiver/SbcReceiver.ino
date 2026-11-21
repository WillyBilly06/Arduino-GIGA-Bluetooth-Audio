#include <GigaBredr.h>
using namespace giga_bredr;
// Link only the SBC decoder (includes SBC-XQ bitstreams): the smallest build.
GIGA_BREDR_CODECS(Sbc);
GigaBredr receiver;

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name = "GIGA SBC Receiver";
  Serial.println(statusName(receiver.begin(config)));
}
void loop() {
  receiver.poll();
  delay(1);
}
