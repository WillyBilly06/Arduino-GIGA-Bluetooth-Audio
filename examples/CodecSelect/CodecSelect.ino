// Minimal A2DP receiver that compiles in only the codecs it lists.
//
// GIGA_BREDR_CODECS(...) picks the decoders linked into this sketch; every
// other decoder is left out of flash and is not offered to phones. Names:
// Sbc (SBC/SBC-XQ, always keep it), Aac, AacHe, Aptx, AptxHd, Ldac, LhdcV5,
// Opus, Ssc. Remove the line to get every codec.
//
// Wiring: I2S DAC on BCLK=D70, DIN=D72, LRCK=D74 (no MCLK needed).
#include <GigaBredr.h>
using namespace giga_bredr;

GIGA_BREDR_CODECS(Sbc, Aac, LhdcV5);

GigaBredr receiver;

void setup() {
  Serial.begin(115200);
  ReceiverConfig config;
  config.name = "GIGA Speaker";
  Status status = receiver.begin(config);
  Serial.print("begin: ");
  Serial.println(statusName(status));
  Serial.print("codecs compiled in (bit mask): 0x");
  Serial.println(receiver.availableCodecs(), HEX);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    const ReceiverStats s = receiver.stats();
    Serial.print(s.radio.connected ? "connected " : "waiting   ");
    if (s.playing) {
      Serial.print(codecName(s.configuration.codec));
      Serial.print(" ");
      Serial.print(s.configuration.sample_rate);
      Serial.print(" Hz ");
      Serial.print(s.configuration.bits_per_sample);
      Serial.print("-bit");
    }
    Serial.println();
  }
  delay(10);
}
