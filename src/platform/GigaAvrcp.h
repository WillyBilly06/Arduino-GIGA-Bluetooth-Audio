#pragma once
#include "GigaRadio.h"

namespace giga_bredr { namespace detail {

// All entry points are serialized with radioPoll on the host thread. Metadata
// callbacks borrow bounded storage for the duration of the call; copy if queued.
// The owner applies volume once in its PCM render path. This module never scales
// PCM and never sends SetAbsoluteVolume back to the phone.
struct AvrcpStatus {
    bool connected = false;
    uint8_t volume = 127;
    uint8_t playback_status = 0xff;
    uint32_t song_length_ms = 0xffffffffu;
    uint32_t song_position_ms = 0xffffffffu;
    uint32_t malformed_events = 0;
    uint32_t rejected_commands = 0;
};

// Requires initialized L2CAP + SDP. Owns AVRCP init/deinit and SDP handles
// 0x10002 (controller) + 0x10003 (volume target). Requires a volume callback
// wired to the PCM render owner. No hardware/transport ownership.
Status radioAvrcpInit(const RadioCallbacks& callbacks);
void radioAvrcpPoll();
void radioAvrcpDeinit();
AvrcpStatus radioAvrcpStatus();
// Optional host-side volume change: applies the callback once if changed and
// sends VolumeChanged only for local changes with an active registration.
Status radioAvrcpSetVolume(uint8_t volume_0_127);

}}
