#pragma once
#include "Types.h"

namespace giga_bredr {
// One tap sends the normal AVRCP press and release as separate transactions.
enum class AvrcpCommand : uint8_t { Play=0x44, Stop=0x45, Pause=0x46, Next=0x4b, Previous=0x4c };
enum class AvrcpPlaybackState : uint8_t { Stopped=0, Playing=1, Paused=2, ForwardSeek=3, ReverseSeek=4, Unknown=0xff };
enum class AvrcpCommandState : uint8_t { Idle, Queued, Pending, Completed, Rejected, TimedOut, Cancelled, Disconnected, TransportError };
struct AvrcpStatus {
    bool connected = false;
    uint8_t volume = 127;
    uint8_t playback_status = 0xff; // AVRCP observed value; not inferred from A2DP.
    uint32_t song_length_ms = 0xffffffffu;
    uint32_t song_position_ms = 0xffffffffu; // Last observed position, not a running clock.
    uint32_t malformed_events = 0;
    uint32_t rejected_commands = 0;
    uint32_t generation = 0; // Current AVRCP channel session; zero when detached.
    uint32_t command_generation = 0, command_id = 0; // Last admitted tap, including after detach.
    AvrcpCommand command = AvrcpCommand::Play;
    AvrcpCommandState command_state = AvrcpCommandState::Idle;
    uint8_t press_response = 0xff, release_response = 0xff; // Actual AV/C response codes; ff unknown.
    bool cancel_requested = false;
    AvrcpPlaybackState playbackState() const { return static_cast<AvrcpPlaybackState>(playback_status); }
};
// Copied UTF-8 values, at most127 bytes each plus NUL. Bit(attribute_id-1)
// marks a known/truncated attribute: title1, artist2, album3, playing-time7.
// A known empty string differs from an absent value. No browsing or cover art.
struct AvrcpMetadata {
    uint32_t generation = 0;
    uint8_t valid_attributes = 0, truncated_attributes = 0;
    char title[128] = {}, artist[128] = {}, album[128] = {}, playing_time[128] = {};
};
}
