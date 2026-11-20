// A2DP receiver + Message Access client (MAP MCE): list the connected phone's
// inbox and print one message on USB serial.
// Pair the phone and connect it (play audio as usual). A message listing of
// telecom/msg/inbox starts 5 s after the encrypted connection. Serial commands:
//   'l' list the inbox again   'f' list the folders of telecom/msg
//   'm' get the first listed message (bMessage)   'a' abort
// Most phones ask their user to allow message access the first time
// (Android: "Allow access to your messages"; iPhone: "Show Notifications" for
// the accessory). The GIGA looks up the phone's MAS over SDP, opens an RFCOMM
// channel and streams the XML/bMessage bytes in bounded chunks to the callbacks
// below; nothing is buffered whole. No notification server is offered, so new
// messages are not pushed: list again to see them.
#include <Arduino.h>
#include <GigaBredr.h>
#include <GigaQspiBondStorage.h>
using namespace giga_bredr;
GigaBredr receiver;
GigaQspiBondStorage bonds; // Paired phones reconnect after reset/upload.
bool started = false, listed = false;
uint32_t connectedSince = 0, lastReport = 0, entries = 0;
MapOperation current = MapOperation::None;
char firstHandle[17] = "";
char element[320]; // One XML element ("<msg .../>") or one bMessage line.
size_t elementLength = 0;
bool inElement = false, overflow = false;
// Copies attribute name="value" of an XML element into out ("" if absent).
// Entities such as &amp; are printed as sent.
void attribute(const char* text, const char* name, char* out, size_t capacity) {
  out[0] = 0;
  const size_t n = strlen(name);
  for (const char* p = strstr(text, name); p; p = strstr(p + 1, name)) {
    if ((p != text && p[-1] != ' ') || p[n] != '=' || (p[n + 1] != '"' && p[n + 1] != '\'')) continue;
    const char quote = p[n + 1];
    const char* value = p + n + 2;
    size_t k = 0;
    while (value[k] && value[k] != quote && k + 1 < capacity) { out[k] = value[k]; ++k; }
    out[k] = 0;
    return;
  }
}
void listingElement(const char* text) {
  char a[17], b[64], c[64], d[24];
  if (!strncmp(text, "msg ", 4)) {
    attribute(text, "handle", a, sizeof a); attribute(text, "subject", b, sizeof b);
    attribute(text, "sender_name", c, sizeof c); attribute(text, "datetime", d, sizeof d);
    if (!entries) strcpy(firstHandle, a);
    ++entries;
    char line[220];
    snprintf(line, sizeof line, "  msg %s from \"%s\" at %s: %s", a, c, d, b);
    Serial.println(line);
  } else if (!strncmp(text, "folder ", 7)) {
    attribute(text, "name", b, sizeof b);
    ++entries;
    Serial.print("  folder "); Serial.println(b);
  }
}
// Callbacks run from receiver.poll() in loop(), never on the radio thread.
void mapData(const uint8_t* data, size_t length, void*) {
  for (size_t i = 0; i < length; ++i) {
    const char ch = char(data[i]);
    if (current == MapOperation::GetMessage) { // bMessage: print CRLF lines.
      if (ch == '\n') {
        element[elementLength] = 0;
        if (elementLength && element[elementLength - 1] == '\r') element[elementLength - 1] = 0;
        Serial.print("  | "); Serial.println(element);
        elementLength = 0;
      } else if (elementLength + 1 < sizeof element) element[elementLength++] = ch;
      continue;
    }
    if (ch == '<') { inElement = true; overflow = false; elementLength = 0; continue; }
    if (!inElement) continue;
    if (ch == '>') {
      element[elementLength] = 0; inElement = false;
      if (!overflow) listingElement(element);
      continue;
    }
    if (elementLength + 1 < sizeof element) element[elementLength++] = ch;
    else overflow = true; // Over-long elements are skipped.
  }
}
void mapEnd(const MapResult& r, void*) {
  char text[200];
  snprintf(text, sizeof text, "MAP %s op=%u entries=%lu bytes=%lu response=0x%02x listing_size=%u new_message=%u mse_time=%s%s",
           r.ok ? "complete" : "FAILED", unsigned(r.operation), (unsigned long)entries, (unsigned long)r.bytes,
           r.response, unsigned(r.listing_size), unsigned(r.new_message), r.mse_time[0] ? r.mse_time : "-",
           r.aborted ? " (aborted)" : "");
  Serial.println(text);
  if (!r.ok && !r.response) Serial.println("  (no response: phone not connected/encrypted, MAS not found, or access not allowed)");
  current = MapOperation::None;
}
// Busy: radio owner busy or an operation still active; retry later.
bool admitted(Status s, MapOperation op) {
  if (s != Status::Busy) { Serial.print("MAP request status="); Serial.println(int(s)); }
  if (s == Status::Ok) { current = op; entries = 0; elementLength = 0; inElement = false; }
  return s != Status::Busy;
}
void listInbox() {
  firstHandle[0] = 0;
  // At most 10 messages from offset 0, subjects truncated to 40 characters.
  listed = admitted(receiver.mapGetMessageListing("telecom/msg/inbox", 10, 0, 40), MapOperation::GetMessageListing);
}
void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  ReceiverConfig config;
  config.classic_bonds = bonds.storage();
  config.name = "GIGA Messages";
  config.enable_a2dp = true;               // MAP shares the Classic actor with A2DP.
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.obex.map_enabled = true;          // Default is off; adds no SDP record.
  config.obex.event_buffer_bytes = 4096;   // Allocated only because OBEX is enabled.
  config.obex.on_map_data = mapData;
  config.obex.on_map_end = mapEnd;
  const Status status = receiver.begin(config);
  started = status == Status::Ok;
  Serial.print("MAP client begin status="); Serial.println(int(status));
}
void loop() {
  if (!started) { delay(10); return; }
  receiver.poll(); // Delivers the MAP callbacks.
  const uint32_t now = millis();
  const bool connected = receiver.stats().radio.connected;
  if (!connected) { connectedSince = 0; listed = false; }
  else if (!connectedSince) connectedSince = now ? now : 1;
  if (connected && !listed && now - connectedSince >= 5000) listInbox();
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 'l') listInbox();
    if (command == 'f') admitted(receiver.mapGetFolderListing("telecom/msg"), MapOperation::GetFolderListing);
    if (command == 'm') {
      if (!firstHandle[0]) Serial.println("MAP: no handle yet (list first)");
      else admitted(receiver.mapGetMessage(firstHandle), MapOperation::GetMessage);
    }
    if (command == 'a') { Serial.print("MAP abort status="); Serial.println(int(receiver.mapAbort())); }
  }
  if (now - lastReport >= 5000) {
    lastReport = now;
    ObexStats s;
    if (receiver.obexStats(s) == Status::Ok) {
      char text[200];
      snprintf(text, sizeof text, "MAP state=%u mas_channel=%u mas_version=%04x instance=%u types=%02x features=%08lx completed=%lu failed=%lu bytes=%lu connected=%u",
               unsigned(s.map_state), s.mas_channel, s.mas_version, s.mas_instance_id, s.mas_message_types,
               (unsigned long)s.mas_features, (unsigned long)s.map_completed, (unsigned long)s.map_failed,
               (unsigned long)s.map_bytes, connected);
      Serial.println(text);
    }
  }
  delay(1); // Yield main; independent radio/audio workers keep running.
}
