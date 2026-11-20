// A2DP receiver + Phonebook Access client (PBAP PCE): pull the connected
// phone's contacts and print their count and names on USB serial.
// Pair the phone and connect it (play audio as usual). A pull starts 5 s after
// the encrypted connection, or when 'p' is typed on the serial monitor ('a'
// aborts). Most phones ask their user to allow contact access the first time
// (Android: "Allow access to contacts and call history"). The GIGA looks up the
// phone's PSE over SDP, opens an RFCOMM channel and streams vCards in bounded
// chunks to the callbacks below; nothing is buffered whole.
#include <Arduino.h>
#include <GigaBredr.h>
#include <GigaQspiBondStorage.h>
using namespace giga_bredr;
GigaBredr receiver;
GigaQspiBondStorage bonds; // Paired phones reconnect after reset/upload.
bool started = false, pulled = false;
uint32_t connectedSince = 0, contacts = 0, lastReport = 0;
char line[160];
size_t lineLength = 0;
bool lineOverflow = false;
// Prints "FN" (formatted name) properties; vCard lines end in CRLF. Folded
// continuation lines and QUOTED-PRINTABLE/charset parameters are printed raw.
void vcardLine(const char* text) {
  if (!strncmp(text, "BEGIN:VCARD", 11)) { ++contacts; return; }
  if (!strncmp(text, "FN", 2) && (text[2] == ':' || text[2] == ';')) {
    const char* value = strchr(text, ':');
    Serial.print("  contact "); Serial.print(contacts); Serial.print(": ");
    Serial.println(value && value[1] ? value + 1 : "(no name)");
  }
}
// Callbacks run from receiver.poll() in loop(), never on the radio thread.
void phonebookData(const uint8_t* data, size_t length, void*) {
  for (size_t i = 0; i < length; ++i) {
    const char c = char(data[i]);
    if (c == '\n') {
      line[lineLength] = 0;
      if (lineLength && line[lineLength - 1] == '\r') line[lineLength - 1] = 0;
      if (!lineOverflow) vcardLine(line);
      lineLength = 0; lineOverflow = false;
    } else if (lineLength + 1 < sizeof line) line[lineLength++] = c;
    else lineOverflow = true; // Long PHOTO/base64 lines are skipped.
  }
}
void phonebookEnd(const PbapResult& r, void*) {
  char text[160];
  snprintf(text, sizeof text, "PBAP %s contacts=%lu bytes=%lu response=0x%02x phonebook_size=%u%s",
           r.ok ? "complete" : "FAILED", (unsigned long)contacts, (unsigned long)r.bytes, r.response,
           unsigned(r.phonebook_size), r.aborted ? " (aborted)" : "");
  Serial.println(text);
  if (!r.ok && !r.response) Serial.println("  (no response: phone not connected/encrypted, PSE not found, or access not allowed)");
}
void startPull() {
  contacts = 0; lineLength = 0; lineOverflow = false;
  // Main phonebook, vCard 2.1, at most 500 entries from offset 0.
  const Status s = receiver.pbapPullPhonebook("telecom/pb.vcf", 500, 0);
  if (s != Status::Busy) { Serial.print("PBAP pull request status="); Serial.println(int(s)); }
  pulled = s != Status::Busy; // Busy: radio owner busy or a pull still active; retry.
}
void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  ReceiverConfig config;
  config.classic_bonds = bonds.storage();
  config.name = "GIGA Phonebook";
  config.enable_a2dp = true;               // PBAP shares the Classic actor with A2DP.
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.obex.pbap_enabled = true;         // Default is off; advertises a PCE SDP record.
  config.obex.event_buffer_bytes = 4096;   // Allocated only because OBEX is enabled.
  config.obex.on_phonebook_data = phonebookData;
  config.obex.on_phonebook_end = phonebookEnd;
  const Status status = receiver.begin(config);
  started = status == Status::Ok;
  Serial.print("PBAP client begin status="); Serial.println(int(status));
}
void loop() {
  if (!started) { delay(10); return; }
  receiver.poll(); // Delivers the PBAP callbacks.
  const uint32_t now = millis();
  const bool connected = receiver.stats().radio.connected;
  if (!connected) { connectedSince = 0; pulled = false; }
  else if (!connectedSince) connectedSince = now ? now : 1;
  if (connected && !pulled && now - connectedSince >= 5000) startPull();
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 'p') startPull();
    if (command == 'a') { Serial.print("PBAP abort status="); Serial.println(int(receiver.pbapAbort())); }
  }
  if (now - lastReport >= 5000) {
    lastReport = now;
    ObexStats s;
    if (receiver.obexStats(s) == Status::Ok) {
      char text[160];
      snprintf(text, sizeof text, "PBAP state=%u pse_channel=%u pse_version=%04x completed=%lu failed=%lu bytes=%lu connected=%u playing=%u",
               unsigned(s.pbap_state), s.pse_channel, s.pse_version, (unsigned long)s.pbap_completed,
               (unsigned long)s.pbap_failed, (unsigned long)s.pbap_bytes, connected, receiver.stats().playing);
      Serial.println(text);
    }
  }
  delay(1); // Yield main; independent radio/audio workers keep running.
}
