#pragma once
#include "GigaHid.h"
// Ready-made HID report descriptors for HidConfig::report_descriptor.
// All are boot-protocol compatible: the keyboard input report is the 8-byte
// boot layout {modifiers, reserved, key[6]} with a 1-byte LED output report
// {NumLock, CapsLock, ScrollLock, Compose, Kana}; the mouse input report is
// {buttons, x, y, wheel} whose first three bytes are the boot layout.
namespace giga_bredr {
namespace hid_descriptors {
#define GIGA_HID_KEYBOARD_BODY \
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, \
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01, \
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, \
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01, \
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00
#define GIGA_HID_MOUSE_BODY \
    0x09, 0x01, 0xA1, 0x00, \
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, \
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01, \
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, \
    0xC0
// Boot keyboard, no report IDs: input 8 bytes, output (LEDs) 1 byte.
// Use HidSubclassKeyboard, boot_keyboard=true, hidSend(0, report, 8).
static const uint8_t BootKeyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, GIGA_HID_KEYBOARD_BODY, 0xC0};
// Mouse, no report IDs: input 4 bytes {buttons, x, y, wheel} (signed x/y/wheel).
// Use HidSubclassPointing, boot_mouse=true, hidSend(0, report, 4).
static const uint8_t Mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, GIGA_HID_MOUSE_BODY, 0xC0};
// Keyboard (ID 1, input 8 / output 1), mouse (ID 2, input 4) and consumer
// control (ID 3, input 2: one 16-bit usage such as 0x00CD play/pause).
// Use HidSubclassCombo, boot_keyboard=boot_mouse=true.
static const uint8_t KeyboardMouseCombo[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, GIGA_HID_KEYBOARD_BODY, 0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, GIGA_HID_MOUSE_BODY, 0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0};
#undef GIGA_HID_KEYBOARD_BODY
#undef GIGA_HID_MOUSE_BODY
constexpr uint8_t KeyboardReportId = 1, MouseReportId = 2, ConsumerReportId = 3;
constexpr size_t KeyboardReportBytes = 8, MouseReportBytes = 4, ConsumerReportBytes = 2;
constexpr uint8_t ModifierLeftCtrl = 0x01, ModifierLeftShift = 0x02, ModifierLeftAlt = 0x04, ModifierLeftGui = 0x08;
constexpr uint8_t LedNumLock = 0x01, LedCapsLock = 0x02, LedScrollLock = 0x04;
// US-layout ASCII to {modifier, usage}. Returns false for unmapped characters.
inline bool asciiToKey(char c, uint8_t& modifier, uint8_t& usage) {
    static const char shifted[] = "!@#$%^&*()";
    static const char plain[] = "-=[]\\;'`,./";
    static const char plain_shift[] = "_+{}|:\"~<>?";
    static const uint8_t punctuation[] = {0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    modifier = 0; usage = 0;
    if (c >= 'a' && c <= 'z') { usage = uint8_t(0x04 + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { modifier = ModifierLeftShift; usage = uint8_t(0x04 + (c - 'A')); return true; }
    if (c >= '1' && c <= '9') { usage = uint8_t(0x1E + (c - '1')); return true; }
    if (c == '0') { usage = 0x27; return true; }
    if (c == '\n' || c == '\r') { usage = 0x28; return true; }
    if (c == '\t') { usage = 0x2B; return true; }
    if (c == ' ') { usage = 0x2C; return true; }
    for (unsigned i = 0; shifted[i]; ++i) if (c == shifted[i]) { modifier = ModifierLeftShift; usage = uint8_t(i == 9 ? 0x27 : 0x1E + i); return true; }
    for (unsigned i = 0; plain[i]; ++i) {
        if (c == plain[i]) { usage = punctuation[i]; return true; }
        if (c == plain_shift[i]) { modifier = ModifierLeftShift; usage = punctuation[i]; return true; }
    }
    return false;
}
}
}
