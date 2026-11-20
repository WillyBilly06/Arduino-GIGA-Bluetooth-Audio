// A2DP receiver + OBEX Object Push server: receive files and vCards.
// Pair a phone/PC, play audio as usual, then "Share" a contact, photo or file
// over Bluetooth to "GIGA OBEX Push" (Android: Share > Bluetooth; Linux:
//   obexctl / `bluetooth-sendto --device=XX:XX:XX:XX:XX:XX file.txt`).
// Each object is streamed to the callbacks below in bounded chunks: nothing is
// buffered whole, so any size works. Name, size and CRC-32 print on USB serial.
#include <Arduino.h>
#include <GigaBredr.h>
#include <GigaQspiBondStorage.h>
using namespace giga_bredr;
GigaBredr receiver;
GigaQspiBondStorage bonds; // Paired peers reconnect after reset/upload.
bool started = false;
uint32_t crc = 0, received = 0, expected = 0, lastReport = 0;
char current[96] = {};
uint32_t crc32Update(uint32_t value, const uint8_t* data, size_t length) {
  value = ~value;
  while (length--) {
    value ^= *data++;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
  }
  return ~value;
}
// Callbacks run from receiver.poll() in loop(), never on the radio thread.
void objectBegin(const char* name, const char* type, uint32_t length, void*) {
  strncpy(current, name[0] ? name : "(unnamed)", sizeof current - 1);
  crc = 0; received = 0; expected = length;
  Serial.print("OBEX begin name=\""); Serial.print(current); Serial.print("\" type=\""); Serial.print(type);
  Serial.print("\" length=");
  if (length == ObexUnknownLength) Serial.println("unknown"); else Serial.println(length);
}
void objectData(const uint8_t* data, size_t length, void*) {
  crc = crc32Update(crc, data, length); // Store to SD/QSPI here instead if needed.
  received += length;
}
void objectEnd(bool ok, void*) {
  char line[200];
  snprintf(line, sizeof line, "OBEX %s name=\"%s\" bytes=%lu%s crc32=%08lx",
           ok ? "complete" : "FAILED", current, (unsigned long)received,
           expected != ObexUnknownLength && expected != received ? " (length mismatch)" : "", (unsigned long)crc);
  Serial.println(line);
}
void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  ReceiverConfig config;
  config.classic_bonds = bonds.storage();
  config.name = "GIGA OBEX Push";
  config.enable_a2dp = true;               // OBEX shares the Classic actor with A2DP.
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.obex.opp_enabled = true;          // Default is off.
  config.obex.opp_service_name = "GIGA Object Push";
  config.obex.opp_channel = 4;             // Avoids HFP(1)/HSP(2)/SPP(3) defaults.
  config.obex.event_buffer_bytes = 4096;   // Allocated only because OBEX is enabled.
  config.obex.on_object_begin = objectBegin;
  config.obex.on_object_data = objectData;
  config.obex.on_object_end = objectEnd;
  const Status status = receiver.begin(config);
  started = status == Status::Ok;
  Serial.print("OBEX push begin status="); Serial.println(int(status));
}
void loop() {
  if (!started) { delay(10); return; }
  receiver.poll(); // Delivers the OBEX callbacks; a slow loop() only slows the sender.
  const uint32_t now = millis();
  if (now - lastReport >= 5000) {
    lastReport = now;
    ObexStats s;
    if (receiver.obexStats(s) == Status::Ok) {
      char line[160];
      snprintf(line, sizeof line, "OBEX connected=%u completed=%lu failed=%lu rejected=%lu bytes=%lu errors=%lu playing=%u",
               s.opp_connected, (unsigned long)s.objects_completed, (unsigned long)s.objects_failed,
               (unsigned long)s.objects_rejected, (unsigned long)s.object_bytes, (unsigned long)s.protocol_errors,
               receiver.stats().playing);
      Serial.println(line);
    }
  }
  delay(1); // Yield main; independent radio/audio workers keep running.
}
