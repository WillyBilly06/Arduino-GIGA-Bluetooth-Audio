// Connectionless Peripheral Broadcast (CPB, Bluetooth 4.1 "CSB") between two
// GIGA R1 WiFi boards, alongside the normal A2DP receiver.
//
// Transmitter (CPB_TRANSMITTER 1): broadcasts a 16-byte payload (sequence
// number, millis(), text) every 100 ms and starts a synchronization train so
// receivers can find it. Its Bluetooth address prints at startup.
// Receiver (CPB_TRANSMITTER 0): set TRANSMITTER_ADDRESS to that address; the
// board scans for the train, synchronizes and prints every new payload.
//
// CPB has no acknowledgement or retransmission: a receiver keeps only the
// newest complete payload. Payload bytes are not encrypted.
#include <Arduino.h>
#include <GigaBredr.h>
using namespace giga_bredr;

#ifndef CPB_TRANSMITTER
#define CPB_TRANSMITTER 1
#endif
// Receiver only: the transmitter's address as it prints "CPB address=...".
static const char TRANSMITTER_ADDRESS[] = "00:00:00:00:00:00";

GigaBredr radio;
bool started = false;
uint32_t sequence = 0, lastUpdate = 0, lastReport = 0;

static bool parseAddress(const char* text, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(text, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (unsigned i = 0; i < 6; ++i) { if (v[i] > 255) return false; out[i] = uint8_t(v[i]); }
  return true;
}
static const char* stateName(CpbState s) {
  switch (s) {
    case CpbState::Off: return "off";
    case CpbState::Starting: return "starting";
    case CpbState::Transmitting: return "transmitting";
    case CpbState::Scanning: return "scanning";
    case CpbState::Synchronizing: return "synchronizing";
    case CpbState::Receiving: return "receiving";
    case CpbState::Waiting: return "waiting";
    case CpbState::Stopping: return "stopping";
    default: return "failed";
  }
}

void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  ReceiverConfig config;
  config.name = CPB_TRANSMITTER ? "GIGA CPB TX" : "GIGA CPB RX";
  config.enable_a2dp = true;
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.cpb.max_payload_bytes = 64;          // Allocated only because CPB is on.
  config.cpb.packet_types = CpbDm1 | CpbDh1;  // One 27-byte packet per instant.
  config.cpb.lt_addr = 1;
#if CPB_TRANSMITTER
  config.cpb.role = CpbRole::Transmitter;
  config.cpb.interval_min_slots = 0x50;       // 50..100 ms between CPB instants.
  config.cpb.interval_max_slots = 0xa0;
#else
  config.cpb.role = CpbRole::Receiver;
  if (!parseAddress(TRANSMITTER_ADDRESS, config.cpb.transmitter_address))
    Serial.println("Set TRANSMITTER_ADDRESS");
#endif
  const Status status = radio.begin(config);
  started = status == Status::Ok;
  Serial.print("CPB begin status=");
  Serial.println(statusName(status));
}

void loop() {
  if (!started) { delay(10); return; }
  radio.poll();
  const uint32_t now = millis();
#if CPB_TRANSMITTER
  if (now - lastUpdate >= 100) {
    lastUpdate = now;
    uint8_t payload[16] = {};
    const uint32_t seq = sequence;
    memcpy(payload, &seq, 4);
    memcpy(payload + 4, &now, 4);
    memcpy(payload + 8, "GIGA-CPB", 8);
    if (radio.cpbSetData(payload, sizeof payload) == Status::Ok) ++sequence; // Busy: retry next loop.
  }
#else
  uint8_t payload[64];
  size_t length = 0;
  uint32_t clock = 0;
  if (radio.cpbRead(payload, sizeof payload, length, &clock) && length >= 8) {
    uint32_t seq = 0, sent = 0;
    memcpy(&seq, payload, 4);
    memcpy(&sent, payload + 4, 4);
    char line[96];
    snprintf(line, sizeof line, "CPB rx seq=%lu tx_ms=%lu len=%u clock=%08lx",
             (unsigned long)seq, (unsigned long)sent, unsigned(length), (unsigned long)clock);
    Serial.println(line);
  }
#endif
  if (now - lastReport >= 2000) {
    lastReport = now;
    const ReceiverStats rs = radio.stats();
    CpbStats s;
    if (radio.cpbStats(s) == Status::Ok) {
      char line[300];
      snprintf(line, sizeof line,
               "CPB address=%02X:%02X:%02X:%02X:%02X:%02X state=%s lt=%u interval=%u train=%u/%lu/%lu "
               "updates=%lu fragments=%lu scans=%lu syncs=%lu losses=%lu packets=%lu errors=%lu messages=%lu "
               "overwritten=%lu cmd_errors=%lu last=%04x:%02x",
               rs.radio.address[0], rs.radio.address[1], rs.radio.address[2], rs.radio.address[3], rs.radio.address[4], rs.radio.address[5],
               stateName(s.state), s.lt_addr, s.interval_slots, s.train_running, (unsigned long)s.trains_started,
               (unsigned long)s.trains_completed, (unsigned long)s.data_updates, (unsigned long)s.fragments_sent,
               (unsigned long)s.scans, (unsigned long)s.syncs, (unsigned long)s.sync_losses, (unsigned long)s.packets,
               (unsigned long)s.packet_errors, (unsigned long)s.messages, (unsigned long)s.messages_overwritten,
               (unsigned long)s.command_errors, s.last_opcode, s.last_status);
      Serial.println(line);
    }
  }
  delay(1); // Yield main; the radio/audio workers keep running.
}
