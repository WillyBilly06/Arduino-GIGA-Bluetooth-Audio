// A2DP receiver + Bluetooth Classic HID keyboard/mouse (HID Device profile).
// Pair a phone/PC with "GIGA HID Keyboard": music playback keeps working and
// the same device appears as a keyboard + mouse. Every 10 seconds, or when a
// button on D2 (to GND) is pressed, the GIGA types a line into the focused
// text field. Caps Lock from the host is mirrored on the built-in LED.
// Bonds are stored in QSPI flash, so the host can reconnect after a reset
// (this device never pages the host: reconnect from the host side).
#include <Arduino.h>
#include <GigaBredr.h>
#include <GigaHidDescriptors.h>
#include <GigaQspiBondStorage.h>
using namespace giga_bredr;
namespace hd = giga_bredr::hid_descriptors;

GigaBredr receiver;
giga_bredr::GigaQspiBondStorage bonds; // Paired hosts survive reset/upload.
constexpr int ButtonPin = 2;
constexpr uint32_t TypeIntervalMs = 10000;
constexpr bool MouseJiggle = false; // true: nudge the pointer every 2 s.
const char* const Line = "Hello from the GIGA R1!\n";

bool started = false, wasConnected = false, keyDown = false;
const char* typing = nullptr; // Next character, or nullptr when idle.
uint32_t lastTyped = 0, lastJiggle = 0, lastReport = 0;
int lastButton = HIGH;
int8_t jiggle = 4;

// Runs from receiver.poll() in loop(), never from the radio thread.
void onOutputReport(HidReportType type, uint8_t id, const uint8_t* data, size_t length, void*) {
  if (type != HidReportType::Output || id != hd::KeyboardReportId || !length) return;
  const bool caps = data[0] & hd::LedCapsLock;
  digitalWrite(LED_BUILTIN, caps ? LOW : HIGH); // GIGA RGB LED is active-low.
  Serial.print("Host LEDs=0x");
  Serial.println(data[0], HEX);
}

// One key press or release per call; Busy/OutputFull simply retry next loop().
void serviceTyping() {
  if (!typing) return;
  uint8_t report[hd::KeyboardReportBytes] = {}; // {modifiers, reserved, keys[6]}
  if (!keyDown) {
    uint8_t modifier = 0, usage = 0;
    while (*typing && !hd::asciiToKey(*typing, modifier, usage)) ++typing; // Skip unmapped.
    if (!*typing) { typing = nullptr; return; }
    report[0] = modifier;
    report[2] = usage;
  }
  const Status status = receiver.hidSend(hd::KeyboardReportId, report, sizeof report);
  if (status == Status::Ok) {
    if (keyDown) ++typing; // Release sent: next character.
    keyDown = !keyDown;
  } else if (status == Status::InvalidArgument) { // Host disconnected.
    typing = nullptr;
    keyDown = false;
  }
}

void setup() {
  Serial.begin(921600); // Never wait for a USB monitor before starting radio.
  pinMode(ButtonPin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  ReceiverConfig config;
  config.classic_bonds = bonds.storage();
  config.name = "GIGA HID Keyboard";
  config.enable_a2dp = true; // HID shares the Classic host with A2DP/AVRCP.
  config.ble.enabled = false;
  config.i2s.bclk = 70; config.i2s.data = 72; config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.hid.enabled = true; // Default is off: no SDP record, no listener.
  config.hid.report_descriptor = hd::KeyboardMouseCombo;
  config.hid.report_descriptor_bytes = sizeof hd::KeyboardMouseCombo;
  config.hid.subclass = HidSubclassCombo;
  config.hid.boot_keyboard = true; // BIOS/boot hosts get the 8-byte boot report.
  config.hid.boot_mouse = true;
  config.hid.service_name = "GIGA HID";
  config.hid.description = "Keyboard/Mouse";
  config.hid.output_report = onOutputReport;
  // The default class of device stays audio/loudspeaker. Some hosts only list
  // or treat a device as a keyboard with a peripheral class of device:
  // config.hid.class_of_device = HidClassOfDeviceCombo; // 0x2405C0
  const Status status = receiver.begin(config);
  started = status == Status::Ok;
  Serial.print("HID keyboard begin status=");
  Serial.println(int(status));
}

void loop() {
  if (!started) { delay(10); return; }
  receiver.poll();
  const uint32_t now = millis();
  const bool connected = receiver.hidConnected();
  if (connected != wasConnected) {
    wasConnected = connected;
    lastTyped = now;
    Serial.println(connected ? "HID host connected" : "HID host disconnected");
  }
  const int button = digitalRead(ButtonPin);
  if (button == LOW && lastButton == HIGH && connected && !typing) typing = Line;
  lastButton = button;
  if (connected && !typing && now - lastTyped >= TypeIntervalMs) { lastTyped = now; typing = Line; }
  serviceTyping();
  if (MouseJiggle && connected && now - lastJiggle >= 2000) {
    const uint8_t mouse[hd::MouseReportBytes] = {0, uint8_t(jiggle), 0, 0}; // {buttons, x, y, wheel}
    if (receiver.hidSend(hd::MouseReportId, mouse, sizeof mouse) == Status::Ok) { lastJiggle = now; jiggle = int8_t(-jiggle); }
  }
  if (now - lastReport >= 5000) {
    lastReport = now;
    HidStats s;
    if (receiver.hidStats(s) == Status::Ok) {
      char text[200];
      snprintf(text, sizeof text,
               "HID connected=%u gen=%lu boot=%u suspended=%u sent=%lu rejected=%lu dropped=%lu outputs=%lu get=%lu set=%lu playing=%u",
               s.connected, (unsigned long)s.generation, s.boot_protocol, s.suspended, (unsigned long)s.reports_sent,
               (unsigned long)s.reports_rejected, (unsigned long)s.reports_dropped, (unsigned long)s.output_reports,
               (unsigned long)s.get_reports, (unsigned long)s.set_reports, receiver.stats().playing);
      Serial.println(text);
    }
  }
  delay(1); // Yield main; independent radio/audio workers keep running.
}
