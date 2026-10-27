#include "AudioSupplyRuntime.h"
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
#include <Arduino.h>
#include <mbed.h>
#include <type_traits>
extern "C" {
#include "rtx_os.h"
}

namespace giga_bredr { namespace detail { namespace audio_supply {
namespace {
static_assert(sizeof(uintptr_t) == 4u, "Pinned ARM pointer ABI");
static_assert(__atomic_always_lock_free(4, nullptr), "No atomic lock/helper allowed");
static_assert(std::is_standard_layout<osRtxInfo_t>::value, "Pinned RTX layout");
static_assert(sizeof(osRtxInfo_t) == 164u, "Pinned RTX size");
static_assert(offsetof(osRtxInfo_t, thread.run.curr) == 20u &&
              offsetof(osRtxInfo_t, thread.run.next) == 24u, "Pinned run pointers");

// Every persistent scalar below is accessed through load/store, including data
// from rejected publications. No plain payload read can race begin or a writer.
struct alignas(4) QueueCell {
    uint32_t version, epoch, accepted, dequeued, depth, at_cycles, saturated, available;
};
struct alignas(4) ReceiveCell {
    uint32_t version, epoch, total, at_cycles, saturated, available;
};
struct alignas(4) PhaseCell {
    uint32_t version, epoch, phase, at_cycles, generation, available, counter_serial, wall_us;
};
struct alignas(4) RoleCell { uint32_t version, ids[3]; };
struct alignas(4) Record { uint32_t words[SnapshotWords], generation; };
struct alignas(4) State {
    uint32_t generation, active, stream, output, core_hz, counter_generation;
    uint32_t counter_serial, counter_valid_serial, queue_mutation;
    QueueCell queue;
    ReceiveCell receive;
    PhaseCell phase;
    RoleCell roles;
    HostCreditCell host;
    Record records[FirstRecordCount];
    uint32_t captured, lost;
    // Foreground is sole writer. begin never resets the consumer cursor.
    uint32_t consumer_generation, consumer_cursor;
};
State state{};
static_assert(std::is_trivial<State>::value && std::is_standard_layout<State>::value,
              "Atomic scalar POD only");
static_assert(sizeof(State) == 472u, "Account for every new persistent scalar");
static_assert(sizeof(state) <= NewStateBudgetBytes, "New observer state budget");
static_assert(552u + sizeof(state) <= 1024u, "Combined private observer budget");

// Low ten bits retain the immutable scalar core's reasons. Upper bits describe
// separate publications and point availability; these are not residence/cause.
enum RuntimeFlag : uint32_t {
    ReceiveUnknown = 1u << 10, QueueUnknown = 1u << 11,
    PhaseUnknown = 1u << 12, ContextInvalid = 1u << 13,
    RolesUnknown = 1u << 14, GenerationChanged = 1u << 15,
    OutputInvalid = 1u << 16, ReceiveTimeUnknown = 1u << 17,
    QueueTimeUnknown = 1u << 18, TimingContractAssumed = 1u << 19,
    WallFresh = 1u << 20, WallAgeInvalid = 1u << 21,
    QueueMutation = 1u << 22
};
uint32_t load(const uint32_t& value) {
    return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
}
void store(uint32_t& target, uint32_t value) {
    __atomic_store_n(&target, value, __ATOMIC_RELEASE);
}
uint32_t increment(uint32_t value) { return value == UINT32_MAX ? value : value + 1u; }
// A writer owns its cell. UINT32_MAX permanently poisons this publication rather
// than wrapping to an even version that could alias a previous reader.
uint32_t open(uint32_t& version) {
    const uint32_t old = load(version);
    const uint32_t odd = old >= UINT32_MAX - 2u ? UINT32_MAX : old + 1u;
    store(version, odd);
    return odd;
}
void close(uint32_t& version, uint32_t odd) {
    store(version, odd == UINT32_MAX ? UINT32_MAX : odd + 1u);
}
struct CounterSample { uint32_t cycles; bool available; };
CounterSample counter() {
    const bool enabled = (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) &&
                         (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk);
    // No reads of CYCCNT while it is unavailable; never enable/reset the DWT.
    return {enabled ? DWT->CYCCNT : UINT32_MAX, enabled};
}
bool aligned(uint32_t pointer) { return pointer && !(pointer & 3u); }
uint32_t current() {
    return uint32_t(reinterpret_cast<uintptr_t>(
        __atomic_load_n(&osRtxInfo.thread.run.curr, __ATOMIC_RELAXED)));
}
uint32_t next() {
    return uint32_t(reinterpret_cast<uintptr_t>(
        __atomic_load_n(&osRtxInfo.thread.run.next, __ATOMIC_RELAXED)));
}
void mutateQueue(uint32_t stream, uint32_t depth, bool admission) {
    // Existing queue lock serializes Host/decoder writers AND begin's reset.
    const uint32_t generation = load(state.active);
    if (!generation || generation != load(state.generation) ||
        stream != load(state.stream) || stream != load(state.queue.epoch)) return;
    const CounterSample sample = counter();
    if (!sample.available) store(state.counter_valid_serial, 0u);
    const uint32_t version = open(state.queue.version);
    uint32_t a = load(state.queue.accepted), d = load(state.queue.dequeued);
    if (admission) a = increment(a); else d = increment(d);
    store(state.queue.accepted, a);
    store(state.queue.dequeued, d);
    store(state.queue.depth, depth);
    store(state.queue.at_cycles, sample.cycles);
    store(state.queue.available, sample.available ? 1u : 0u);
    if (a == UINT32_MAX || d == UINT32_MAX || version == UINT32_MAX)
        store(state.queue.saturated, 1u);
    close(state.queue.version, version);
}
// Root supplies the conditional contract only after the existing backend's
// accepted, same-output duration-valid observation. No external debugger/other
// writer resets or disables DWT during the epoch is an explicit assumption.
// Enable bits alone NEVER establish continuity; phase must have observed the
// same generation contract too. Any observed disable invalidates it until a
// later good backend observation. This does not prove external reset history.
bool continuousCounter(uint32_t generation, CounterSample now,
                       uint32_t phase_available, uint32_t phase_contract) {
    if (!now.available) store(state.counter_valid_serial, 0u);
    const uint32_t contract = load(state.counter_valid_serial);
    return now.available && phase_available && phase_contract &&
           phase_contract != UINT32_MAX && phase_contract == contract &&
           load(state.counter_generation) == generation;
}

constexpr size_t RowBytes = 2u + 9u + 8u * 9u + 1u; // H0 + ordinal + words + NUL
constexpr size_t ConsolePrefixBytes = 24u;
static_assert(RowBytes == 84u && RowBytes + ConsolePrefixBytes + 1u <= 128u,
              "Foreground row including prefix/newline/NUL");
struct Row {
    char* out; size_t used;
    void character(char c) { out[used++] = c; }
    void hex(uint32_t value) {
        character(' ');
        for (unsigned shift = 32u; shift; shift -= 4u) {
            const uint32_t nibble = (value >> (shift - 4u)) & 15u;
            character(char(nibble < 10u ? '0' + nibble : 'a' + nibble - 10u));
        }
    }
};
} // namespace

void begin(uint32_t stream_epoch, uint32_t output_epoch) {
    // Serialized lifecycle/Host caller holds codec+queue locks; DMA is stopped
    // and no queue/RX writer can overlap this reset, including begin(0, 0).
    // Decoder phase is deliberately untouched.
    store(state.active, 0u);
    store(state.counter_valid_serial, 0u);
    store(state.queue_mutation, 0u);
    const uint32_t previous = load(state.generation);
    if (previous >= UINT32_MAX - 1u) {
        store(state.generation, UINT32_MAX);
        return; // No generation wrap or record-stamp alias.
    }
    const uint32_t generation = previous + 1u;
    store(state.generation, generation); // Invalidate old foreground copies first.
    store(state.stream, stream_epoch);
    store(state.output, output_epoch);
    store(state.core_hz, SystemCoreClock);
    store(state.captured, 0u);
    store(state.lost, 0u);
    store(state.queue.version, 1u);
    store(state.queue.epoch, stream_epoch);
    store(state.queue.accepted, 0u);
    store(state.queue.dequeued, 0u);
    store(state.queue.depth, 0u);
    store(state.queue.at_cycles, UINT32_MAX);
    store(state.queue.saturated, 0u);
    store(state.queue.available, 0u);
    store(state.queue.version, 2u);
    store(state.receive.version, 1u);
    store(state.receive.epoch, stream_epoch);
    store(state.receive.total, 0u);
    store(state.receive.at_cycles, UINT32_MAX);
    store(state.receive.saturated, 0u);
    store(state.receive.available, 0u);
    store(state.receive.version, 2u);
    // Record payloads/stamps need no clearing: only this never-reused generation
    // can publish them. All payload stores/loads remain atomic during reuse.
    if (stream_epoch && output_epoch) store(state.active, generation);
}
void end() {
    store(state.active, 0u); // Retain records, stamps and generation for draining.
}
void hostCreditInvalidate() {
    // Already odd remains odd: Main cannot accidentally publish old payload.
    store(state.host.version,hostInvalidateVersion(load(state.host.version)));
}
uint32_t hostCreditGeneration() {
    const uint32_t g=load(state.active);
    return g&&g!=UINT32_MAX&&g==load(state.generation)?g:0u;
}
void hostCreditPublish(uint32_t generation,uint32_t host,uint32_t transport) {
    // Main can reach shared profile helpers during end(). It may invalidate
    // there, but only the registered Host thread can commit this cell.
    if(__get_IPSR()||current()!=load(state.roles.ids[1]))return;
    if(!generation||generation!=hostCreditGeneration())return;
    const uint32_t odd=hostStartVersion(load(state.host.generation),generation,load(state.host.version));
    store(state.host.version,odd);
    store(state.host.generation,generation);
    store(state.host.host,host);
    store(state.host.transport,transport);
    if(generation!=hostCreditGeneration())return; // Leave odd; never close a late view.
    store(state.host.version,hostCloseVersion(odd));
}
void counterObserved(uint32_t stream, uint32_t output, bool valid) {
    const uint32_t generation = load(state.active);
    if (!valid || !generation || generation != load(state.generation) ||
        stream != load(state.stream) || output != load(state.output)) {
        store(state.counter_valid_serial, 0u);
        return;
    }
    const CounterSample sample = counter();
    if (!sample.available) { store(state.counter_valid_serial, 0u); return; }
    if (load(state.counter_generation) == generation && load(state.counter_valid_serial))
        return; // Preserve a still-valid contract serial across good observations.
    // This is the sole foreground writer of lifetime serial. begin never resets
    // it; invalidators only clear valid_serial. Saturate before reuse/wrap.
    const uint32_t previous = load(state.counter_serial);
    if (previous >= UINT32_MAX - 1u) {
        store(state.counter_serial, UINT32_MAX);
        store(state.counter_valid_serial, 0u);
        return;
    }
    const uint32_t serial = previous + 1u;
    store(state.counter_serial, serial);
    store(state.counter_generation, generation);
    store(state.counter_valid_serial, serial);
    if (load(state.generation) != generation || load(state.active) != generation)
        store(state.counter_valid_serial, 0u);
}
void registerRoles(uintptr_t main_id, uintptr_t host_id, uintptr_t decoder_id) {
    const uint32_t version = open(state.roles.version);
    store(state.roles.ids[0], uint32_t(main_id));
    store(state.roles.ids[1], uint32_t(host_id));
    store(state.roles.ids[2], uint32_t(decoder_id));
    close(state.roles.version, version);
}
void receiveEntry() {
    // Host sole active writer; lifecycle reset is serialized before workers.
    // No mutable Impl state is accessed.
    const uint32_t generation = load(state.active);
    if (!generation || generation != load(state.generation)) return;
    const CounterSample sample = counter();
    if (!sample.available) store(state.counter_valid_serial, 0u);
    const uint32_t version = open(state.receive.version);
    const uint32_t total = increment(load(state.receive.total));
    store(state.receive.total, total);
    store(state.receive.at_cycles, sample.cycles);
    store(state.receive.available, sample.available ? 1u : 0u);
    if (total == UINT32_MAX || version == UINT32_MAX) store(state.receive.saturated, 1u);
    close(state.receive.version, version);
}
void queueMutation(uint32_t stream, bool busy) {
    // Existing queue lock serializes mutation owners and lifecycle reset. The
    // marker covers the actual mutation-to-publication gap, never lock waiting.
    const uint32_t generation = load(state.active);
    if (!generation || generation != load(state.generation) ||
        stream != load(state.stream)) return;
    store(state.queue_mutation, busy ? generation : 0u);
}
void accepted(uint32_t stream, uint32_t depth) { mutateQueue(stream, depth, true); }
void dequeued(uint32_t stream, uint32_t depth) { mutateQueue(stream, depth, false); }
void markPhase(DecoderPhase phase, uint32_t expected_epoch) {
    if (__get_IPSR()) return; // Thread-only: never call micros from an IRQ.
    // Decoder owns this cell even while Host reconfigures the context.
    const uint32_t generation = load(state.active);
    if (!generation || generation != load(state.generation)) return;
    const uint32_t stream = load(state.stream);
    if (expected_epoch && expected_epoch != stream) return;
    // Take the independent anchor before CYCCNT, so a long preemption between
    // the two cannot make an old cycle sample appear to have a fresh wall age.
    // Both reads happen before the publication becomes odd.
    const uint32_t wall_us = uint32_t(micros());
    const CounterSample sample = counter();
    if (!sample.available) store(state.counter_valid_serial, 0u);
    const uint32_t contract = sample.available &&
        load(state.counter_generation) == generation ? load(state.counter_valid_serial) : 0u;
    if (load(state.phase.generation) != generation) {
        // New generation permits the sole writer to reset its exhausted version.
        // This odd interval contains stores only, no blocking operation.
        store(state.phase.version, 1u);
    }
    const uint32_t version = load(state.phase.generation) != generation
        ? 1u : open(state.phase.version);
    store(state.phase.generation, generation);
    store(state.phase.epoch, stream);
    store(state.phase.phase, static_cast<uint32_t>(phase));
    store(state.phase.at_cycles, sample.cycles);
    store(state.phase.available, sample.available ? 1u : 0u);
    store(state.phase.counter_serial, contract);
    store(state.phase.wall_us, wall_us);
    // A late old-generation publisher is never accepted as the new phase.
    if (load(state.generation) != generation || load(state.active) != generation)
        store(state.phase.epoch, 0u);
    close(state.phase.version, version);
}

void firstStarvation(uint32_t output_epoch, uint32_t at_us,
                     uint32_t pcm_queued, uint32_t half, uint32_t real) {
    const uint32_t generation = load(state.active);
    if (!generation || generation != load(state.generation)) return;
    const uint32_t index = load(state.captured);
    if (index >= FirstRecordCount) {
        store(state.lost, increment(load(state.lost)));
        return;
    }
    const uint32_t stream = load(state.stream);
    const uint32_t configured_output = load(state.output);
    const CounterSample now = counter();

    // One bounded seqlock pass per cell, never retry rejected data.
    const uint32_t rv0 = load(state.receive.version);
    const uint32_t re = load(state.receive.epoch);
    const uint32_t rt = load(state.receive.total);
    const uint32_t rs = load(state.receive.saturated);
    const uint32_t ra = load(state.receive.available);
    const uint32_t rv1 = load(state.receive.version);

    const uint32_t queue_mutation_before = load(state.queue_mutation);
    QueueView queue{};
    queue.version_before = load(state.queue.version);
    queue.epoch = load(state.queue.epoch);
    queue.accepted = load(state.queue.accepted);
    queue.dequeued = load(state.queue.dequeued);
    queue.depth = load(state.queue.depth);
    queue.saturated = load(state.queue.saturated);
    const uint32_t qa = load(state.queue.available);
    queue.version_after = load(state.queue.version);
    const uint32_t queue_mutation_after = load(state.queue_mutation);

    PhaseView phase{};
    phase.version_before = load(state.phase.version);
    phase.epoch = load(state.phase.epoch);
    phase.phase = static_cast<DecoderPhase>(load(state.phase.phase));
    phase.at_cycles = load(state.phase.at_cycles);
    const uint32_t pg = load(state.phase.generation);
    const uint32_t pa = load(state.phase.available);
    const uint32_t pcg = load(state.phase.counter_serial);
    const uint32_t phase_wall_us = load(state.phase.wall_us);
    phase.version_after = load(state.phase.version);

    const uint32_t roles_v0 = load(state.roles.version);
    const uint32_t main_role = load(state.roles.ids[0]);
    const uint32_t host_role = load(state.roles.ids[1]);
    const uint32_t decoder_role = load(state.roles.ids[2]);
    const uint32_t roles_v1 = load(state.roles.version);
    const uint32_t current_thread = current(), next_thread = next();

    // One bounded scalar pass. Host validity is independent of phase/queue.
    const uint32_t hv0=load(state.host.version);
    const uint32_t hg=load(state.host.generation);
    const uint32_t hw=load(state.host.host);
    const uint32_t ht=load(state.host.transport);
    const uint32_t hv1=load(state.host.version);

    uint32_t rx_flags = publicationFlags(stream, re, rv0, rv1);
    if (rs || rt == UINT32_MAX) rx_flags |= Unknown | CounterSaturated;
    const uint32_t q_flags = queueFlags(stream, queue);
    const uint32_t phase_expected = pg == generation ? stream : 0u;
    // Existing refill at_us is the independent capture anchor: no extra clock
    // read, timer action or critical section occurs in this IRQ path.
    const bool wall_fresh = pg == generation &&
        publicationFlags(stream, phase.epoch, phase.version_before, phase.version_after) == 0u &&
        uint32_t(at_us - phase_wall_us) < 1000000u;
    const bool contract_known = continuousCounter(generation, now, pa, pcg);
    const bool continuous = contract_known && wall_fresh;
    const uint32_t p_flags = phaseFlags(phase_expected, phase, now.cycles, continuous);
    const uint32_t phase_age = phaseAgeCycles(phase_expected, phase, now.cycles, continuous);
    const SupplyPoint core_point = supplyPoint(phase_expected, queue, phase, now.cycles, continuous);
    const bool queue_mutation = queue_mutation_before || queue_mutation_after;
    const SupplyPoint point = queue_mutation ? SupplyPoint::Unknown : core_point;
    uint32_t flags = rx_flags | q_flags | p_flags |
        (static_cast<uint32_t>(point) << 30u);
    if (rx_flags) flags |= ReceiveUnknown;
    if (q_flags) flags |= QueueUnknown;
    if (queue_mutation) flags |= Unknown | QueueUnknown | QueueMutation;
    if (p_flags) flags |= PhaseUnknown;
    if (phase_age == UINT32_MAX) flags |= Unknown | PhaseUnknown;
    if (contract_known) flags |= TimingContractAssumed;
    if (wall_fresh) flags |= WallFresh;
    else flags |= Unknown | WallAgeInvalid;
    if (load(state.counter_serial) == UINT32_MAX) flags |= Unknown | CounterSaturated;
    if (!ra) flags |= Unknown | ReceiveTimeUnknown;
    if (!qa) flags |= Unknown | QueueTimeUnknown;
    if (__get_IPSR() != 63u) flags |= Unknown | ContextInvalid;
    if (!output_epoch || output_epoch != configured_output) flags |= Unknown | OutputInvalid;
    if ((roles_v0 | roles_v1) & 1u || roles_v0 != roles_v1 ||
        roles_v0 == UINT32_MAX || !aligned(main_role) || !aligned(host_role) ||
        !aligned(decoder_role) || main_role == host_role || main_role == decoder_role ||
        host_role == decoder_role) flags |= Unknown | RolesUnknown;
    if (load(state.generation) != generation || load(state.active) != generation)
        flags |= Unknown | EpochInvalid | GenerationChanged;
    flags |= hostCreditPublicationFlags(generation,hg,hv0,hv1,hw,ht);

    Snapshot snapshot{{index + 1u, stream, output_epoch, at_us, now.cycles, flags,
        half, real, pcm_queued, rt, hg, queue.accepted, queue.dequeued, queue.depth,
        hw, static_cast<uint32_t>(phase.phase), phase.at_cycles, ht,
        current_thread, next_thread, main_role, host_role, decoder_role,
        load(state.core_hz)}};
    Record& record = state.records[index];
    // Invalidate old stamp before reuse, then publish only after atomic payload.
    store(record.generation, 0u);
    for (uint32_t i = 0u; i < SnapshotWords; ++i) store(record.words[i], snapshot.words[i]);
    if (load(state.generation) != generation || load(state.active) != generation) return;
    store(record.generation, generation);
    store(state.captured, index + 1u);
}

bool nextRow(char* destination, size_t capacity) {
    if (!destination || capacity < RowBytes) return false;
    const uint32_t generation = load(state.generation);
    if (!generation || generation == UINT32_MAX) return false;
    if (load(state.consumer_generation) != generation) {
        store(state.consumer_generation, generation);
        store(state.consumer_cursor, 0u);
    }
    const uint32_t cursor = load(state.consumer_cursor);
    if (cursor >= FirstRecordCount * 3u) return false;
    const uint32_t index = cursor / 3u, row = cursor % 3u;
    const Record& record = state.records[index];
    const uint32_t stamp_before = load(record.generation);
    if (stamp_before != generation) return false;
    const uint32_t ordinal = load(record.words[0]);
    uint32_t words[8];
    for (uint32_t i = 0u; i < 8u; ++i) words[i] = load(record.words[row * 8u + i]);
    const uint32_t stamp_after = load(record.generation);
    if (stamp_before != stamp_after || load(state.generation) != generation) return false;
    Row output{destination, 0u};
    output.character('H'); output.character(char('0' + row));
    output.hex(ordinal);
    for (uint32_t i = 0u; i < 8u; ++i) output.hex(words[i]);
    output.character(0);
    // The formatted row is a local, coherent old/new-generation copy. If begin
    // raced formatting, discard it without advancing the single-consumer cursor.
    if (load(state.generation) != generation) { destination[0] = 0; return false; }
    store(state.consumer_cursor, cursor + 1u);
    return true;
}
}}} // namespace giga_bredr::detail::audio_supply
#endif
