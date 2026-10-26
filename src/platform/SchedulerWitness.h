#pragma once
#include <stdint.h>
#include <stddef.h>
#ifndef GIGA_BREDR_PRIVATE_SCHED_WITNESS
#define GIGA_BREDR_PRIVATE_SCHED_WITNESS 0
#endif
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
#if !defined(ARDUINO_GIGA) || !defined(CORE_CM7)
#error Private scheduler witness requires the pinned GIGA Cortex-M7 build
#endif
namespace giga_bredr { namespace detail {
struct ClockTrimResult;
// Sole writer is hostMain. Never reset while a host thread can exist.
extern uint32_t scheduler_host_progress;
extern uint32_t scheduler_capture_phase; // 0=before wait1,1=claimed interval,2=after endpoint.
inline void schedulerWait1Phase(uint32_t phase) {
    __atomic_store_n(&scheduler_capture_phase, phase, __ATOMIC_RELEASE);
}
inline uint32_t schedulerHostProgress() {
    return __atomic_load_n(&scheduler_host_progress, __ATOMIC_RELAXED);
}
inline void schedulerHostLoopProgress() {
    const uint32_t value = schedulerHostProgress();
    if (value != UINT32_MAX) __atomic_store_n(&scheduler_host_progress, value + 1u, __ATOMIC_RELAXED);
}
void schedulerRegisterRoles(uintptr_t main_id, uintptr_t host_id, uintptr_t decoder_id);
void schedulerShutdown();
void schedulerPrepare(uint32_t stream, uint32_t output, uint32_t decision);
void schedulerBegin(uint32_t expected_output);
void schedulerEnd(const ClockTrimResult&, uint32_t mark, uint32_t host_before);
// SPSC consumer: call at most once per foreground poll. Finish after callback.
bool schedulerNextRow(char* destination, size_t capacity, uint32_t& ordinal);
void schedulerConsoleReceipt(bool accepted); // Foreground sink only; admission, not USB delivery.
void schedulerRowFinished(uint32_t ordinal);
}}
#endif
