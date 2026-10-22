#pragma once
#include <stdint.h>
namespace giga_bredr {
// Worker feedback changes only the exclusively owned PLL2 fractional numerator.
// ppm values describe requested/readback coefficients, not measured wire rate.
struct AudioClockConfig {
    bool enabled = true;
    uint16_t limit_ppm = 500; // 0..500, absolute correction limit.
    uint16_t slew_ppm_per_second = 10; // 0..10, at most one decision per second.
};
enum class AudioClockState : uint8_t { Disabled, Inactive, Learning, Tracking, Suspended, BackendFault };
enum class AudioClockBackendStatus : uint8_t {
    Unavailable, Applied, Unchanged, Inactive, StaleEpoch, InvalidArgument,
    Unrepresentable, Unsupported, ForeignClock, TimerUnavailable, TimerStalled,
    ReadbackFault, InvalidContext
};
// One coherent last worker observation at observation_us, or an explicit
// begin/stop publication. Live counters between observations remain private.
// This is not a current wire-rate measurement or an IRQ snapshot API.
struct AudioClockStats {
    AudioClockState state = AudioClockState::Inactive;
    AudioClockBackendStatus backend_status = AudioClockBackendStatus::Unavailable;
    // enabled is the requested I2S control path; eligible separately reports
    // whether this epoch has at least four actual rounded DMA halves of ring.
    bool enabled = true, eligible = false, backend_fault = false, applied_valid = false, inventory_valid = false;
    uint32_t stream_epoch = 0, output_epoch = 0, expected_output_epoch = 0, sample_rate = 0, observation_us = 0;
    uint32_t ring_capacity = 0, required_ring_frames = 0;
    // Exact current-stream source frames: A-D-P is ring + unfinished PCM.
    // P includes both real primed DMA halves. Z includes absent input/holds.
    uint64_t decoded_frames = 0, discarded_frames = 0, prepared_frames = 0, missing_frames = 0;
    // F/G include valid zero-output entries in F, and only decoded frames in G.
    uint64_t completed_packets = 0, completed_frames = 0;
    uint64_t estimated_inventory_frames = 0, target_frames = 0, frames_per_packet_q16 = 0;
    uint32_t compressed_units_q16 = 0, retained_units_q16 = 0, assembly_discards = 0;
    int32_t requested_ppm_q16 = 0, applied_ppm_q16 = 0;
    uint16_t fraction = 0, readback_fraction = 0;
    uint32_t observations = 0, decisions = 0, accepted = 0, refused = 0, invalid_samples = 0;
    uint32_t duration_cycles = 0, wait_cycles[2] = {}, maximum_critical_cycles = 0;
    uint8_t wall_overruns = 0;
    bool duration_valid = false;
};
}
