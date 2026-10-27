#pragma once
#include "AudioSupplyWitness.h"
#include <stddef.h>
#ifndef GIGA_BREDR_PRIVATE_SCHED_WITNESS
#define GIGA_BREDR_PRIVATE_SCHED_WITNESS 0
#endif

namespace giga_bredr { namespace detail { namespace audio_supply {
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
#if !defined(ARDUINO_GIGA) || !defined(CORE_CM7)
#error Private supply witness requires the pinned GIGA Cortex-M7 build
#endif
// Serialized lifecycle/Host caller owns codec+queue locks; no concurrent
// queue/RX writers and DMA must be stopped, including inactive begin(0, 0).
void begin(uint32_t stream_epoch, uint32_t output_epoch);
// Deactivate only: retained records can still be consumed in foreground.
void end();
// After the existing measured endpoint only. This is a conditional owned-app
// continuity contract, invalidated by unavailable/invalid/stale observations.
// The existing backend endpoint caller is the sole foreground publisher.
void counterObserved(uint32_t stream, uint32_t output, bool valid);
// Main is the sole writer. Pointer values are cached, never dereferenced.
void registerRoles(uintptr_t main_id, uintptr_t host_id, uintptr_t decoder_id);
// Host callback ENTRY, before queue locking; this is not RF/UART arrival.
void receiveEntry();
// With queue lock held, mark immediately before actual push/take, then clear
// after accepted/dequeued publication or failed mutation. Never across blocking.
void queueMutation(uint32_t stream, bool busy);
// Publish only after successful mutations, while the existing queue lock is held.
void accepted(uint32_t stream, uint32_t depth);
void dequeued(uint32_t stream, uint32_t depth);
// Decoder thread is the sole writer. micros() is sampled before the odd
// publication; publish before blocking, never while version odd.
void markPhase(DecoderPhase phase, uint32_t expected_epoch = 0);
// First empty-PCM transition of an episode only; bounded IRQ capture.
void firstStarvation(uint32_t output_epoch, uint32_t at_us,
                     uint32_t pcm_queued, uint32_t half, uint32_t real);
// Single foreground consumer; one S0/S1/S2 row, at most once per poll.
bool nextRow(char* destination, size_t capacity);
#else
inline void begin(uint32_t, uint32_t) {}
inline void end() {}
inline void counterObserved(uint32_t, uint32_t, bool) {}
inline void registerRoles(uintptr_t, uintptr_t, uintptr_t) {}
inline void receiveEntry() {}
inline void queueMutation(uint32_t, bool) {}
inline void accepted(uint32_t, uint32_t) {}
inline void dequeued(uint32_t, uint32_t) {}
inline void markPhase(DecoderPhase, uint32_t = 0) {}
inline void firstStarvation(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
inline bool nextRow(char*, size_t) { return false; }
#endif
}}} // namespace giga_bredr::detail::audio_supply
