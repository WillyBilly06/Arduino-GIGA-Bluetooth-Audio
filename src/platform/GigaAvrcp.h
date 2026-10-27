#pragma once
#include "GigaRadio.h"
#include "../GigaAvrcp.h"

namespace giga_bredr { namespace detail {

// All entry points are serialized with radioPoll on the host thread. Metadata
// callbacks borrow bounded storage for the duration of the call; copy if queued.
// The owner applies volume once in its PCM render path. This module never scales
// PCM and never sends SetAbsoluteVolume back to the phone.
using AvrcpStatus = giga_bredr::AvrcpStatus;

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
AvrcpMetadata radioAvrcpMetadata();
Status radioAvrcpCommand(AvrcpCommand command,uint32_t expected_generation,uint32_t& request_id);
Status radioAvrcpCancel(uint32_t expected_generation,uint32_t request_id);

}}
