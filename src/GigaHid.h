#pragma once
#include "Types.h"
namespace giga_bredr {
// Bluetooth Classic HID Device profile (HIDP 1.1): the GIGA acts as a
// keyboard, mouse, combo or gamepad to a phone/PC. Default disabled: no SDP
// record, no EIR UUID, no L2CAP 0x11/0x13 listener and no storage. Requires the
// Classic actor (enable_a2dp=true) and an encrypted link. The host connects
// the control (PSM 0x11) and interrupt (PSM 0x13) channels; this device does
// not page the host (see reconnect_initiate).
constexpr uint8_t HidSubclassKeyboard = 0x40, HidSubclassPointing = 0x80, HidSubclassCombo = 0xC0,
                  HidSubclassJoystick = 0x04, HidSubclassGamepad = 0x08;
// Peripheral major class (0x05) with the receiver's Audio+Rendering service
// bits kept. The default class of device stays 0x240414 (audio/loudspeaker);
// some hosts list or route HID devices only for a peripheral class of device.
constexpr uint32_t HidClassOfDeviceKeyboard = 0x240540, HidClassOfDeviceMouse = 0x240580,
                   HidClassOfDeviceCombo = 0x2405C0, HidClassOfDeviceGamepad = 0x240508;
// Boot protocol report IDs (HIDP 1.1 boot reports always carry them).
constexpr uint8_t HidBootKeyboardReportId = 1, HidBootMouseReportId = 2;
enum class HidReportType : uint8_t { Input = 1, Output = 2, Feature = 3 };
// Runs from GigaBredr::poll() (loop context), never from the radio thread.
// report_id is the descriptor's report ID (0 without report IDs), also in boot
// protocol. data holds the received bytes (zero-padded copies are cached).
using HidOutputCallback = void (*)(HidReportType type, uint8_t report_id, const uint8_t* data, size_t length, void* user);
struct HidConfig {
    bool enabled = false;
    // Copied at begin(): 1..512 bytes. Every Input/Output/Feature report must
    // be 1..64 bytes; a descriptor using report IDs must declare one before
    // its first main item. GigaHidDescriptors.h has ready-made descriptors.
    const uint8_t* report_descriptor = nullptr;
    uint16_t report_descriptor_bytes = 0;
    uint8_t subclass = HidSubclassKeyboard; // SDP HIDDeviceSubclass (low two bits zero).
    uint8_t country_code = 0;               // SDP HIDCountryCode (0 = not localized).
    const char* service_name = "GIGA HID";  // 1..32 bytes.
    const char* description = "Keyboard";   // nullptr omits; else 1..32 bytes.
    const char* provider = nullptr;         // nullptr omits; else 1..32 bytes.
    bool virtual_cable = true;
    // Advertised only: this host stack never pages the HID host, so the
    // default false asks the host to reconnect (NormallyConnectable=true).
    bool reconnect_initiate = false;
    bool normally_connectable = true;
    // Boot protocol support. Keyboard: an 8-byte boot-layout input report (and
    // optional LED output report) at report ID 1, or ID 0 without report IDs.
    // Mouse: a >=3-byte boot-layout input report at ID 2 (ID 0 if mouse only).
    bool boot_keyboard = false, boot_mouse = false;
    uint32_t class_of_device = 0; // 0 keeps the audio class of device 0x240414.
    // HID 1.1 Device ID (PnP Information 0x1200) record.
    bool device_id = true;
    uint16_t vendor_id_source = 2, vendor_id = 0x2341, product_id = 0x0001, version = 0x0100;
    HidOutputCallback output_report = nullptr; // Keyboard LEDs, SET_REPORT output/feature.
    void* user = nullptr;
};
struct HidStats {
    bool enabled = false, connected = false, control_open = false, interrupt_open = false;
    bool suspended = false, boot_protocol = false;
    uint32_t generation = 0;  // Nonzero per connected control+interrupt pair.
    uint32_t connections = 0; // Pairs connected during this begin().
    uint32_t reports_queued = 0, reports_sent = 0;
    uint32_t reports_rejected = 0; // hidSend() OutputFull (queue full).
    uint32_t reports_dropped = 0;  // Queued reports discarded at disconnect/protocol change.
    uint32_t reports_filtered = 0; // Accepted but not sent: no boot form in boot protocol.
    uint32_t output_reports = 0, output_overwritten = 0; // Overwritten before poll() delivered.
    uint32_t get_reports = 0, set_reports = 0, protocol_changes = 0;
    uint32_t error_handshakes = 0, unsupported_requests = 0, ignored_requests = 0, malformed = 0;
    uint32_t suspends = 0, unplugs = 0;
    uint8_t tx_pending = 0;
};
}
