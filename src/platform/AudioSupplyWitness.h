#pragma once
#include <stdint.h>

// Pure scalar contracts shared by private capture and compile-only checks.
// This header contains no observer state, SDK dependency or runtime hook.
namespace giga_bredr { namespace detail { namespace audio_supply {

constexpr uint32_t SnapshotWords = 24u;
constexpr uint32_t FirstRecordCount = 3u;
constexpr uint32_t NewStateBudgetBytes = 472u;
constexpr uint32_t QueueDepthMaximum = 64u;

struct alignas(4) Snapshot { uint32_t words[SnapshotWords]; };
static_assert(sizeof(Snapshot) == 96u && alignof(Snapshot) >= 4u,
              "Fixed scalar supply record payload");

struct QueueView {
    uint32_t epoch, version_before, accepted, dequeued, depth, version_after, saturated;
};
enum class DecoderPhase : uint32_t {
    Unknown = 0u, WaitWake = 1u, WaitCodec = 2u, WaitQueue = 3u,
    PacketOwned = 4u, Decoding = 5u, OutputWrite = 6u,
    BackpressureWait = 7u, Idle = 8u
};
struct PhaseView {
    uint32_t epoch, version_before;
    DecoderPhase phase;
    uint32_t at_cycles, version_after;
};
enum Flag : uint32_t {
    Unknown = 1u << 0, EpochInvalid = 1u << 1, VersionOdd = 1u << 2,
    VersionChanged = 1u << 3, CounterSaturated = 1u << 4,
    CountOrderInvalid = 1u << 5, QueueMismatch = 1u << 6,
    CounterDisabled = 1u << 7, TimeAmbiguous = 1u << 8,
    PhaseInvalid = 1u << 9
};

// Views are already single-pass scalar copies of atomic publications. Equal
// even versions alone cannot establish epoch continuity or prevent wrap alias.
// The future publisher must prohibit version wrap and retain saturation.
constexpr uint32_t publicationFlags(uint32_t expected_epoch, uint32_t epoch,
                                    uint32_t before, uint32_t after) {
    uint32_t reasons = 0u;
    if (!expected_epoch || !epoch || epoch != expected_epoch) reasons |= EpochInvalid;
    if ((before | after) & 1u) reasons |= VersionOdd;
    if (before != after) reasons |= VersionChanged;
    if (before == UINT32_MAX || after == UINT32_MAX) reasons |= CounterSaturated;
    return reasons ? reasons | Unknown : 0u;
}

constexpr uint32_t queueFlags(uint32_t expected_epoch, QueueView view) {
    uint32_t reasons = publicationFlags(expected_epoch, view.epoch,
                                        view.version_before, view.version_after);
    if (view.saturated || view.accepted == UINT32_MAX ||
        view.dequeued == UINT32_MAX || view.depth == UINT32_MAX)
        reasons |= CounterSaturated;
    if (view.accepted < view.dequeued) reasons |= CountOrderInvalid;
    // Subtract only after checking order: unsigned wrap is never a count.
    if (view.depth > QueueDepthMaximum ||
        (view.accepted >= view.dequeued && view.accepted - view.dequeued != view.depth))
        reasons |= QueueMismatch;
    return reasons ? reasons | Unknown : 0u;
}

// counter_enabled means availability AND known continuous counter history for
// this epoch. Merely reading the current DWT enable bit cannot establish that.
// No timer, SDK or hardware access occurs in any scalar helper.
constexpr uint32_t phaseFlags(uint32_t expected_epoch, PhaseView view,
                              uint32_t now_cycles, bool counter_enabled) {
    uint32_t reasons = publicationFlags(expected_epoch, view.epoch,
                                        view.version_before, view.version_after);
    const uint32_t phase = static_cast<uint32_t>(view.phase);
    if (!phase || phase > static_cast<uint32_t>(DecoderPhase::Idle)) reasons |= PhaseInvalid;
    if (!counter_enabled) reasons |= CounterDisabled;
    // A disabled/unavailable counter has no interpretable duration. When
    // available, reject half-range and older spans, including backwards time.
    if (counter_enabled && uint32_t(now_cycles - view.at_cycles) >= 0x80000000u)
        reasons |= TimeAmbiguous;
    return reasons ? reasons | Unknown : 0u;
}

constexpr uint32_t phaseAgeCycles(uint32_t expected_epoch, PhaseView view,
                                  uint32_t now_cycles, bool counter_enabled) {
    return phaseFlags(expected_epoch, view, now_cycles, counter_enabled) == 0u
        ? uint32_t(now_cycles - view.at_cycles) : UINT32_MAX;
}

enum class SupplyPoint : uint32_t {
    Unknown, QueuedWorkPresent, InFlightWork, NoQueuedWork
};
// Point availability of separate publications, never an atomic cross-cell
// instant, exclusive thread residence, decoder fault or upstream cause.
constexpr SupplyPoint supplyPoint(uint32_t expected_epoch, QueueView queue,
                                  PhaseView phase, uint32_t now_cycles,
                                  bool counter_enabled) {
    const uint32_t queue_flags = queueFlags(expected_epoch, queue);
    const uint32_t phase_flags = phaseFlags(expected_epoch, phase, now_cycles, counter_enabled);
    if (queue_flags || phase_flags) return SupplyPoint::Unknown;
    if (queue.depth) return SupplyPoint::QueuedWorkPresent;
    switch (phase.phase) {
    case DecoderPhase::PacketOwned:
    case DecoderPhase::Decoding:
    case DecoderPhase::OutputWrite:
    case DecoderPhase::BackpressureWait:
        return SupplyPoint::InFlightWork;
    case DecoderPhase::WaitWake:
    case DecoderPhase::Idle:
        return SupplyPoint::NoQueuedWork;
    default:
        return SupplyPoint::Unknown;
    }
}

}}} // namespace giga_bredr::detail::audio_supply
