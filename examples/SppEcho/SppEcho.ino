// A2DP receiver + Bluetooth Classic Serial Port Profile echo.
// Pair a phone/PC, play audio as usual, and open the "GIGA Serial" SPP
// service (RFCOMM channel 3) from e.g. Linux:
//   python3 -c "import socket;s=socket.socket(socket.AF_BLUETOOTH,socket.SOCK_STREAM,socket.BTPROTO_RFCOMM);s.connect(('XX:XX:XX:XX:XX:XX',3));s.send(b'hi');print(s.recv(64))"
// or an Android "Serial Bluetooth Terminal"-style app. Every received byte is
// written back; statistics print on the USB serial monitor every two seconds.
#include <Arduino.h>
#include <GigaBredr.h>
#include <GigaQspiBondStorage.h>
using namespace giga_bredr;
GigaBredr receiver;
giga_bredr::GigaQspiBondStorage bonds; // Paired peers reconnect after reset/upload.
bool started = false;
uint8_t pending[256];
size_t pendingBytes = 0, pendingOffset = 0;
uint32_t lastReport = 0;
bool wasConnected = false;
void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  ReceiverConfig config;
  config.classic_bonds = bonds.storage();
  config.name = "GIGA SPP Echo";
  config.enable_a2dp = true;
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.spp.enabled = true;            // Default is off.
  config.spp.service_name = "GIGA Serial";
  config.spp.channel = 3;               // Avoids the HFP(1)/HSP(2) defaults.
  config.spp.rx_buffer_bytes = 2048;    // Allocated only because SPP is enabled.
  config.spp.tx_buffer_bytes = 2048;
  const Status status = receiver.begin(config);
  started = status == Status::Ok;
  Serial.print("SPP echo begin status=");
  Serial.println(int(status));
}
void loop() {
  if (!started) { delay(10); return; }
  receiver.poll();
  // Refill only after the previous chunk is fully accepted, so echo order is
  // preserved and a slow peer applies backpressure through RFCOMM credits.
  if (pendingOffset == pendingBytes) {
    pendingOffset = 0;
    pendingBytes = receiver.sppRead(pending, sizeof pending); // 0 if empty or radio busy.
  }
  if (pendingOffset < pendingBytes) {
    size_t accepted = 0;
    const Status status = receiver.sppWrite(pending + pendingOffset, pendingBytes - pendingOffset, accepted);
    pendingOffset += accepted; // Ok may be partial; Busy/OutputFull retry next loop().
    if (status == Status::InvalidArgument) pendingOffset = pendingBytes; // Peer disconnected.
  }
  const bool connected = receiver.sppConnected();
  if (connected != wasConnected) {
    wasConnected = connected;
    Serial.println(connected ? "SPP connected" : "SPP disconnected");
  }
  const uint32_t now = millis();
  if (now - lastReport >= 2000) {
    lastReport = now;
    SppStats s;
    if (receiver.sppStats(s) == Status::Ok) {
      char line[200];
      snprintf(line, sizeof line,
               "SPP connected=%u gen=%lu n1=%u credits=%u rx=%lu tx=%lu rx_drop=%lu tx_drop=%lu rejected=%lu pending=%lu playing=%u",
               s.connected, (unsigned long)s.generation, s.frame_bytes, s.peer_credits,
               (unsigned long)s.rx_bytes, (unsigned long)s.tx_bytes, (unsigned long)s.rx_dropped_bytes,
               (unsigned long)s.tx_dropped_bytes, (unsigned long)s.tx_rejected_bytes, (unsigned long)s.tx_pending,
               receiver.stats().playing);
      Serial.println(line);
    }
  }
  delay(1); // Yield main; independent radio/audio workers keep running.
}
