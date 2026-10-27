// Actual future production-header constexpr contracts only.
// Root-only official ARM compile; never link or execute this fixture.
#include "platform/AudioSupplyWitness.h"
#include <stdint.h>
using namespace giga_bredr::detail::audio_supply;

static_assert(SnapshotWords == 24u, "wire record has exactly 24 words");
static_assert(sizeof(Snapshot) == 96u && alignof(Snapshot) >= 4u,
              "fixed aligned record payload, independent of private flag");
static_assert(FirstRecordCount == 3u, "retain first three recovery episodes only");
static_assert(NewStateBudgetBytes == 472u && 552u + NewStateBudgetBytes <= 1024u,
              "actual .cpp state must separately assert the combined BSS bound");

constexpr QueueView empty{7u, 2u, 0u, 0u, 0u, 2u, 0u};
constexpr QueueView pending{7u, 8u, 12u, 9u, 3u, 8u, 0u};
constexpr QueueView drained{7u, 10u, 12u, 12u, 0u, 10u, 0u};
static_assert(queueFlags(7u, empty) == 0u, "initial empty coherent epoch is known");
static_assert(queueFlags(7u, pending) == 0u, "accepted minus dequeued equals published depth");
static_assert(queueFlags(7u, drained) == 0u, "drained is known zero, distinct from UNKNOWN");
static_assert((queueFlags(7u, QueueView{7u,8u,12u,9u,2u,8u,0u}) & QueueMismatch) != 0u,
              "depth mismatch cannot be interpreted as a supply count");
static_assert((queueFlags(7u, QueueView{7u,8u,9u,12u,UINT32_MAX-2u,8u,0u}) & CountOrderInvalid) != 0u,
              "wrapped unsigned subtraction is not a valid pending count");
static_assert((queueFlags(7u, QueueView{7u,3u,12u,9u,3u,3u,0u}) & VersionOdd) != 0u,
              "odd publication is UNKNOWN without retry");
static_assert((queueFlags(7u, QueueView{7u,8u,12u,9u,3u,10u,0u}) & VersionChanged) != 0u,
              "changed publication is UNKNOWN without retry");
static_assert((queueFlags(7u, QueueView{6u,8u,12u,9u,3u,8u,0u}) & EpochInvalid) != 0u,
              "stale queue epoch cannot be combined with current output");
static_assert((queueFlags(0u, QueueView{0u,2u,0u,0u,0u,2u,0u}) & EpochInvalid) != 0u,
              "zero epochs are unavailable, not a coherent zero count");
static_assert((queueFlags(7u, QueueView{7u,8u,UINT32_MAX,UINT32_MAX,0u,8u,0u}) & CounterSaturated) != 0u,
              "equal saturated totals do not prove an empty queue");
static_assert((queueFlags(7u, QueueView{7u,8u,12u,9u,3u,8u,1u}) & CounterSaturated) != 0u,
              "sticky saturation survives apparently ordinary totals");
static_assert((queueFlags(7u, QueueView{7u,8u,12u,9u,UINT32_MAX,8u,0u}) & CounterSaturated) != 0u,
              "saturated depth cannot be used as pending work");
static_assert((queueFlags(7u, QueueView{7u,UINT32_MAX,12u,9u,3u,UINT32_MAX,0u}) & CounterSaturated) != 0u,
              "exhausted publication version is UNKNOWN");
static_assert((queueFlags(7u, QueueView{6u,3u,UINT32_MAX,0u,0u,5u,1u}) & Unknown) != 0u,
              "multiple faults remain UNKNOWN, never plausible zero");

constexpr PhaseView waitWake{7u,2u,DecoderPhase::WaitWake,100u,2u};
constexpr PhaseView waitQueue{7u,4u,DecoderPhase::WaitQueue,120u,4u};
constexpr PhaseView decode{7u,6u,DecoderPhase::Decoding,0xfffffff0u,6u};
static_assert(phaseFlags(7u, waitWake,150u,true) == 0u, "pre-wake-wait phase can be observed");
static_assert(phaseFlags(7u, waitQueue,150u,true) == 0u, "pre-queue-lock phase can be observed");
static_assert(phaseFlags(7u, PhaseView{7u,2u,DecoderPhase::WaitCodec,100u,2u},150u,true) == 0u,
              "pre-codec-lock phase must be explicit");
static_assert(phaseFlags(7u, decode,0x10u,true) == 0u &&
              phaseAgeCycles(7u, decode,0x10u,true) == 32u,
              "single wrap below half range has an unambiguous scalar age");
static_assert(phaseAgeCycles(7u, waitWake,150u,true) == 50u, "age uses published phase entry time");
static_assert(phaseFlags(7u, PhaseView{7u,2u,DecoderPhase::OutputWrite,0u,2u},0u,true) == 0u &&
              phaseAgeCycles(7u, PhaseView{7u,2u,DecoderPhase::OutputWrite,0u,2u},0u,true) == 0u,
              "enabled timer zero is valid, unlike unavailable timer");
static_assert((phaseFlags(7u,waitWake,150u,false) & CounterDisabled) != 0u &&
              phaseAgeCycles(7u,waitWake,150u,false) == UINT32_MAX,
              "disabled DWT is UNKNOWN, never zero elapsed");
static_assert((phaseFlags(7u,waitWake,100u+0x80000000u,true) & TimeAmbiguous) != 0u &&
              phaseAgeCycles(7u,waitWake,100u+0x80000000u,true) == UINT32_MAX,
              "exact half range and older are ambiguous");
static_assert((phaseFlags(7u,waitWake,99u,true) & TimeAmbiguous) != 0u,
              "backwards phase time is not a huge valid duration");
static_assert((phaseFlags(7u,PhaseView{6u,2u,DecoderPhase::Decoding,100u,2u},150u,true) & EpochInvalid) != 0u,
              "stale decoder phase remains UNKNOWN");
static_assert((phaseFlags(7u,PhaseView{7u,3u,DecoderPhase::Decoding,100u,3u},150u,true) & VersionOdd) != 0u,
              "odd decoder publication remains UNKNOWN");
static_assert((phaseFlags(7u,PhaseView{7u,2u,DecoderPhase::Decoding,100u,4u},150u,true) & VersionChanged) != 0u,
              "changed decoder publication remains UNKNOWN");
static_assert((phaseFlags(7u,PhaseView{7u,2u,DecoderPhase::Unknown,100u,2u},150u,true) & PhaseInvalid) != 0u &&
              (phaseFlags(7u,PhaseView{7u,2u,static_cast<DecoderPhase>(UINT32_MAX),100u,2u},150u,true) & PhaseInvalid) != 0u,
              "missing and invalid phases cannot masquerade as decoder idle");
static_assert(phaseAgeCycles(7u,PhaseView{6u,2u,DecoderPhase::Decoding,100u,2u},150u,true) == UINT32_MAX,
              "all invalid age results use an explicit unknown sentinel");

// Opposite scalar observations, not causation or a cross-cell atomic instant:
// pending.depth=3 plus WaitQueue is queued decoder work at those publications;
// drained.depth=0 plus WaitWake is no queued packet at those publications.
// Neither scenario attributes upstream absence to phone/RF/UART.
static_assert(queueFlags(7u,pending) == 0u && pending.depth > 0u &&
              phaseFlags(7u,waitQueue,150u,true) == 0u,
              "pending-decoder queue scenario retains both independent validity checks");
static_assert(queueFlags(7u,drained) == 0u && drained.depth == 0u &&
              phaseFlags(7u,waitWake,150u,true) == 0u,
              "no-pending-queue scenario does not turn missing validity into empty");

// Classification is point availability only, and calls the same production validators.
static_assert(supplyPoint(7u,pending,waitQueue,150u,true) == SupplyPoint::QueuedWorkPresent,
              "queued packets are availability, not proof of a decoder fault");
static_assert(supplyPoint(7u,drained,waitWake,150u,true) == SupplyPoint::NoQueuedWork,
              "known empty plus pre-wake-wait is no queued work at publication");
static_assert(supplyPoint(7u,drained,PhaseView{7u,2u,DecoderPhase::Idle,100u,2u},150u,true) == SupplyPoint::NoQueuedWork,
              "explicit idle with known empty remains a point observation");
static_assert(supplyPoint(7u,drained,PhaseView{7u,2u,DecoderPhase::PacketOwned,100u,2u},150u,true) == SupplyPoint::InFlightWork &&
              supplyPoint(7u,drained,PhaseView{7u,2u,DecoderPhase::Decoding,100u,2u},150u,true) == SupplyPoint::InFlightWork &&
              supplyPoint(7u,drained,PhaseView{7u,2u,DecoderPhase::OutputWrite,100u,2u},150u,true) == SupplyPoint::InFlightWork &&
              supplyPoint(7u,drained,PhaseView{7u,2u,DecoderPhase::BackpressureWait,100u,2u},150u,true) == SupplyPoint::InFlightWork,
              "already dequeued work is in flight despite a zero published queue");
static_assert(supplyPoint(7u,drained,waitQueue,150u,true) == SupplyPoint::Unknown,
              "empty queue plus pre-lock phase alone cannot exclude in-flight work");
static_assert(supplyPoint(7u,pending,waitQueue,150u,false) == SupplyPoint::Unknown &&
              supplyPoint(8u,pending,waitQueue,150u,true) == SupplyPoint::Unknown &&
              supplyPoint(7u,QueueView{7u,3u,12u,9u,3u,3u,0u},waitQueue,150u,true) == SupplyPoint::Unknown,
              "invalid publication/time wins over apparently pending scalar data");

static_assert(phaseFlags(7u,waitWake,100u+0x7fffffffu,true) == 0u &&
              phaseAgeCycles(7u,waitWake,100u+0x7fffffffu,true) == 0x7fffffffu,
              "largest valid half-range age remains distinct from UNKNOWN");
constexpr bool rejected_views_are_unknown() {
    const uint32_t rejected[] = {
        queueFlags(7u, QueueView{6u,8u,12u,9u,3u,8u,0u}),
        queueFlags(7u, QueueView{7u,3u,12u,9u,3u,3u,0u}),
        queueFlags(7u, QueueView{7u,8u,12u,9u,3u,10u,0u}),
        queueFlags(7u, QueueView{7u,8u,UINT32_MAX,UINT32_MAX,0u,8u,0u}),
        queueFlags(7u, QueueView{7u,8u,9u,12u,0u,8u,0u}),
        queueFlags(7u, QueueView{7u,8u,12u,9u,2u,8u,0u}),
        phaseFlags(7u,waitWake,150u,false),
        phaseFlags(7u,waitWake,100u+0x80000000u,true),
        phaseFlags(7u,PhaseView{7u,2u,DecoderPhase::Unknown,100u,2u},150u,true)
    };
    for (uint32_t flags : rejected) if (!(flags & Unknown)) return false;
    return true;
}
static_assert(rejected_views_are_unknown(), "reason flags must always carry UNKNOWN");

static_assert((queueFlags(7u, QueueView{7u,2u,65u,0u,65u,2u,0u}) & QueueMismatch) != 0u &&
              (queueFlags(7u, QueueView{7u,2u,65u,0u,65u,2u,0u}) & Unknown) != 0u,
              "coherent arithmetic still rejects depth above the known 64-slot maximum");
static_assert(queueFlags(7u, QueueView{7u,2u,64u,0u,64u,2u,0u}) == 0u,
              "known maximum depth remains an admissible scalar view");
