#include "SchedulerWitness.h"
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
#include "I2sClockTrim.h"
#include <Arduino.h>
#include <mbed.h>
#include <type_traits>
extern "C" {
#include "rtx_os.h"
void __real_osRtxTick_Handler(void);
void __real_osRtxPendSV_Handler(void);
}

namespace giga_bredr { namespace detail {
alignas(4) uint32_t scheduler_host_progress = 0;
alignas(4) uint32_t scheduler_capture_phase = 0;
namespace {
static_assert(sizeof(uintptr_t) == 4, "Pinned ARM pointer ABI");
static_assert(__atomic_always_lock_free(4, nullptr), "No atomic helper/lock allowed");
static_assert(std::is_standard_layout<osRtxThread_t>::value, "Pinned TCB layout");
static_assert(std::is_standard_layout<osRtxInfo_t>::value, "Pinned RTX layout");
static_assert(sizeof(osRtxThread_t) == 68 && sizeof(osRtxInfo_t) == 164, "Pinned RTX sizes");
static_assert(offsetof(osRtxThread_t, id) == 0 && offsetof(osRtxThread_t, state) == 1 &&
              offsetof(osRtxThread_t, priority) == 32 && offsetof(osRtxThread_t, priority_base) == 33, "Pinned TCB offsets");
static_assert(offsetof(osRtxInfo_t, thread.run.curr) == 20 &&
              offsetof(osRtxInfo_t, thread.run.next) == 24, "Pinned run pointers");

// Low16 contains LOCAL applicability/role coverage AND integrity reasons.
// Only IntegrityMask accumulates. High bytes: seven-bit seen count plus lifetime-ever bit.
enum Flag : uint32_t {
    Unknown=1u, TickOverflow=2u, PendOverflow=4u, CountSaturated=8u,
    CounterInvalid=16u, KeyInvalid=32u, RolesInvalid=64u, ReportBusy=128u,
    WrapAmbiguous=256u, NotStarted=512u, HostSaturated=1024u,
    OutputUnknown=2048u, RoleCoverage=4096u, ContextInvalid=8192u,
    FormatInvalid=16384u, Stopped=32768u
};
constexpr uint32_t LocalMask=NotStarted | RoleCoverage;
constexpr uint32_t IntegrityMask=0xffffu & ~LocalMask;
enum EventFlag : uint32_t {
    Present=1u, Complete=2u, CounterValid=4u, SpanValid=8u,
    ExpectedContext=16u, ClaimedWindow=32u, SameKey=64u
};
constexpr uint32_t StructuralRequired=Present | Complete | ExpectedContext | SameKey;
constexpr uint32_t TimingRequired=CounterValid | SpanValid;
struct Key { uint32_t stream, output, decision, ordinal; };
struct Event { volatile uint32_t enter, leave, curr0, next0, curr1, next1, flags; };
struct Bucket { Event events[2]; volatile uint32_t seen, complete, overflow, invalid; };
struct Record {
    Key key;
    uint32_t roles[3]; // main, host, decoder; addresses only, no TCB reads.
    uint32_t start, end, t2, mark, t3, host_before, host_after, flags;
    uint32_t configured_hz, status, mask;
    uint32_t begin_counter_enabled, lifetime_seen[2];
    Bucket hooks[2];
};
struct State {
    alignas(4) uint32_t active, roles_valid, roles[3];
    volatile uint32_t lifetime_seen[2], lifetime_invalid[2]; // Independent IRQ writers.
    Key prepared;
    uint32_t prepared_valid, next_ordinal, producer_unknown;
    // Main is the sole writer of consumer_unknown and all row/ack fields.
    alignas(4) uint32_t consumer_unknown, report_state;
    uint32_t row, awaiting_ack, ack_seen, ack_accepted;
    Record capture, report;
};
State state{};
static_assert(std::is_trivial<State>::value && std::is_standard_layout<State>::value, "POD only");
static_assert(sizeof(Event) == 28, "Fixed event payload");
static_assert(sizeof(State) + sizeof(scheduler_host_progress) + sizeof(scheduler_capture_phase) <= 1024, "Private BSS budget");
constexpr size_t RowPayloadMaximum = 74;
constexpr size_t WorstConsolePrefix = 24; // GBR_EVENT ms=4294967295 plus trailing space.
constexpr size_t BatchConsoleMaximum = 2u*(74u+WorstConsolePrefix+1u)+4u*(70u+WorstConsolePrefix+1u);
static_assert(BatchConsoleMaximum == 578 && BatchConsoleMaximum <= 600, "Protocol2 six rows including prefix/newline");
static_assert(BatchConsoleMaximum <= 673, "Conditional baseline console headroom");

uint32_t load(const uint32_t& value) { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }
void publish(uint32_t& target, uint32_t value) { __atomic_store_n(&target, value, __ATOMIC_RELEASE); }
void increment(volatile uint32_t& value) { const uint32_t old=value; if (old != UINT32_MAX) value=old+1u; }
void producerUnknown(uint32_t reason) { publish(state.producer_unknown, load(state.producer_unknown) | Unknown | reason); }
void consumerUnknown(uint32_t reason) {
    // Foreground-only writer; producer reads atomically without changing it.
    publish(state.consumer_unknown, load(state.consumer_unknown) | Unknown | reason);
}
bool counterEnabled() {
    return (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) && (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk);
}
bool aligned(uint32_t pointer) { return pointer && !(pointer & 3u); }
bool known(uint32_t pointer) {
    return aligned(pointer) && (pointer == state.roles[0] || pointer == state.roles[1] || pointer == state.roles[2]);
}
uint32_t current() { return uint32_t(reinterpret_cast<uintptr_t>(__atomic_load_n(&osRtxInfo.thread.run.curr, __ATOMIC_RELAXED))); }
uint32_t next() { return uint32_t(reinterpret_cast<uintptr_t>(__atomic_load_n(&osRtxInfo.thread.run.next, __ATOMIC_RELAXED))); }
struct Ticket { uint32_t ordinal; Event* event; };
Ticket enterHook(unsigned hook) {
    // Lifetime hook coverage is independent of per-window payload counts.
    const bool expected=__get_IPSR() == (hook ? 14u : 15u);
    if (expected) increment(state.lifetime_seen[hook]);
    else increment(state.lifetime_invalid[hook]);
    const uint32_t ordinal = load(state.active);
    if (!ordinal) return {0, nullptr};
    Bucket& bucket = state.capture.hooks[hook];
    increment(bucket.seen);
    if (__get_IPSR() != (hook ? 14u : 15u)) increment(bucket.invalid);
    if (bucket.seen > 2u) { increment(bucket.overflow); return {ordinal, nullptr}; }
    Event& event = bucket.events[bucket.seen-1u];
    const bool enabled = counterEnabled();
    const uint32_t phase=load(scheduler_capture_phase);
    event.enter = DWT->CYCCNT;
    event.curr0 = current(); event.next0 = next();
    event.flags = Present | (enabled ? uint32_t(CounterValid) : 0u) |
        (expected ? uint32_t(ExpectedContext) : 0u) | ((phase & 3u)<<8);
    return {ordinal, &event};
}
void leaveHook(unsigned hook, Ticket ticket) {
    if (!ticket.ordinal) return;
    Bucket& bucket = state.capture.hooks[hook];
    increment(bucket.complete);
    if (load(state.active) != ticket.ordinal || state.capture.key.ordinal != ticket.ordinal) {
        increment(bucket.invalid); return;
    }
    if (!ticket.event) return;
    Event& event = *ticket.event;
    event.curr1 = current(); event.next1 = next();
    event.leave = DWT->CYCCNT;
    const uint32_t phase=load(scheduler_capture_phase);
    uint32_t flags = event.flags | SameKey | ((phase & 3u)<<10);
    if (!counterEnabled()) flags &= ~CounterValid;
    if (uint32_t(event.leave-event.enter) < 0x80000000u) flags |= SpanValid;
    if (__get_IPSR() != (hook ? 14u : 15u)) flags &= ~ExpectedContext;
    __atomic_store_n(&event.flags, flags | Complete, __ATOMIC_RELEASE);
}
uint32_t seenSeven(uint32_t value) { return value > 127u ? 127u : value; }
bool within(uint32_t value, uint32_t first, uint32_t last) {
    return uint32_t(value-first) <= uint32_t(last-first);
}
bool intersects(uint32_t first, uint32_t last, uint32_t start, uint32_t end) {
    return within(first,start,end) || within(last,start,end) || within(start,first,last);
}
bool phaseOverlaps(uint32_t entry, uint32_t leave) {
    return entry==1u || leave==1u || (entry==0u && leave==2u);
}
// Fixed bounded foreground formatter; no printf/allocator in the IRQ callgraph.
struct Row {
    char* out; size_t capacity, used; bool valid;
    void character(char c) { if (used+1u >= capacity || used >= RowPayloadMaximum) valid=false; else out[used++]=c; }
    void hex(uint32_t value, unsigned digits=8) {
        static const char alphabet[]="0123456789abcdef";
        character(' ');
        for (unsigned n=digits; n; --n) character(alphabet[(value>>((n-1u)*4u)) & 15u]);
    }
    bool finish() { if (!valid || used >= capacity) return false; out[used]=0; return true; }
};
}

void schedulerRegisterRoles(uintptr_t main_id, uintptr_t host_id, uintptr_t decoder_id) {
    // Caller owns codec_mutex. IDs are copied in thread context, never dereferenced.
    publish(state.roles_valid, 0);
    if (load(state.active)) { consumerUnknown(RolesInvalid); return; }
    const uint32_t a=uint32_t(main_id), b=uint32_t(host_id), c=uint32_t(decoder_id);
    if (!aligned(a) || !aligned(b) || !aligned(c) || a==b || a==c || b==c) { consumerUnknown(RolesInvalid); return; }
    state.roles[0]=a; state.roles[1]=b; state.roles[2]=c;
    publish(state.roles_valid, 1);
}
void schedulerShutdown() {
    // Main invalidates BEFORE any worker deletion. No pointer is dereferenced.
    publish(state.roles_valid, 0); publish(state.active, 0);
    if (load(state.report_state)) consumerUnknown(Stopped | OutputUnknown);
    state.awaiting_ack=0; publish(state.report_state, 0);
}
void schedulerPrepare(uint32_t stream, uint32_t output, uint32_t decision) {
    if (load(state.active) || state.prepared_valid) producerUnknown(KeyInvalid);
    if (state.next_ordinal == UINT32_MAX) { state.prepared_valid=0; producerUnknown(CountSaturated | KeyInvalid); return; }
    ++state.next_ordinal;
    state.prepared={stream,output,decision,state.next_ordinal};
    state.prepared_valid=stream && output && decision;
    if (!state.prepared_valid) producerUnknown(KeyInvalid);
}
void schedulerBegin(uint32_t expected_output) {
    if (load(state.active)) { producerUnknown(KeyInvalid); return; }
    state.capture=Record{};
    state.capture.key=state.prepared;
    state.prepared_valid=state.prepared_valid && state.prepared.output==expected_output;
    if (!state.prepared_valid || !state.prepared.ordinal) { producerUnknown(KeyInvalid); return; }
    state.prepared_valid=0;
    if (!load(state.roles_valid)) { producerUnknown(RolesInvalid); return; }
    for (unsigned i=0;i<3;++i) state.capture.roles[i]=state.roles[i];
    // Original apply may enable DWT before its measured wait; no loss is claimed here.
    state.capture.begin_counter_enabled=counterEnabled() ? 1u : 0u;
    publish(scheduler_capture_phase,0u);
    publish(state.active, state.capture.key.ordinal);
}
void schedulerEnd(const ClockTrimResult& result, uint32_t mark, uint32_t host_before) {
    const uint32_t ordinal=load(state.active);
    publish(state.active,0);
    if (!ordinal || ordinal!=state.capture.key.ordinal || !load(state.roles_valid)) { producerUnknown(KeyInvalid | RolesInvalid); return; }
    Record& record=state.capture;
    record.start=result.scheduler_wait1_start; record.end=result.scheduler_wait1_end;
    record.t2=result.restore_t2; record.mark=mark; record.t3=result.restore_t3;
    record.host_before=host_before; record.host_after=result.scheduler_host_after;
    record.configured_hz=result.configured_core_hz; record.status=uint32_t(result.status); record.mask=result.witness_mask;
    const bool started=(result.witness_mask & 0x40u)!=0;
    const bool timing_valid=started && result.duration_valid && (result.witness_mask & 0x9fu)==0x1fu && result.restore_flags==7u;
    if (result.output_epoch!=record.key.output) record.flags |= Unknown | KeyInvalid;
    if (result.status==ClockTrimCode::InvalidContext) record.flags |= Unknown | ContextInvalid;
    if (!started) record.flags |= NotStarted; // LOCAL N/A, not an integrity fault or a zero wait.
    else if (!timing_valid) record.flags |= Unknown | CounterInvalid;
    bool geometry_valid=false;
    if (timing_valid) {
        geometry_valid=uint32_t(record.end-record.start)<0x80000000u &&
            within(record.t2,record.start,record.end) && within(record.mark,record.t2,record.t3) &&
            within(record.t3,record.start,record.end);
        if (!geometry_valid) record.flags |= Unknown | WrapAmbiguous;
        if (geometry_valid && (host_before==UINT32_MAX || record.host_after==UINT32_MAX || record.host_after<host_before))
            record.flags |= Unknown | HostSaturated;
    }
    for (unsigned h=0;h<2;++h) {
        Bucket& bucket=record.hooks[h];
        record.lifetime_seen[h]=__atomic_load_n(&state.lifetime_seen[h],__ATOMIC_RELAXED);
        if (state.lifetime_invalid[h] || bucket.invalid || bucket.complete!=bucket.seen)
            record.flags |= Unknown | ContextInvalid;
        if (bucket.overflow) record.flags |= Unknown | (h ? PendOverflow : TickOverflow);
        if (record.lifetime_seen[h]==UINT32_MAX || state.lifetime_invalid[h]==UINT32_MAX ||
            bucket.seen>127u || bucket.complete==UINT32_MAX) record.flags |= Unknown | CountSaturated;
        // seen==complete==0 is an empty retained bank, not a failure or an absence proof.
        for (unsigned slot=0;slot<2 && slot<bucket.seen;++slot) {
            Event& event=bucket.events[slot];
            uint32_t flags=event.flags;
            const uint32_t entry_phase=(flags>>8)&3u, leave_phase=(flags>>10)&3u;
            if ((flags & StructuralRequired)!=StructuralRequired || entry_phase>2u || leave_phase>2u || leave_phase<entry_phase)
                record.flags |= Unknown | ContextInvalid;
            if (!(flags & SameKey)) record.flags |= Unknown | KeyInvalid;
            const bool pointers_valid=aligned(event.curr0) && aligned(event.next0) && aligned(event.curr1) && aligned(event.next1);
            if (!pointers_valid) record.flags |= Unknown | ContextInvalid;
            else if (!known(event.curr0) || !known(event.next0) || !known(event.curr1) || !known(event.next1))
                record.flags |= RoleCoverage; // LOCAL aligned OTHER identity; no TCB dereference.
            const bool phase_overlap=phaseOverlaps(entry_phase,leave_phase);
            if (!started && phase_overlap) record.flags |= Unknown | KeyInvalid;
            bool claimed=started && phase_overlap;
            if (geometry_valid && (flags & TimingRequired)==TimingRequired)
                claimed=intersects(event.enter,event.leave,record.start,record.end);
            if (claimed) {
                flags |= ClaimedWindow;
                if (!(flags & CounterValid)) record.flags |= Unknown | CounterInvalid;
                if (!(flags & SpanValid)) record.flags |= Unknown | WrapAmbiguous;
            } else flags &= ~ClaimedWindow;
            // Initial/pre-start and post-end event timer coverage is local, never stream poison.
            event.flags=flags;
        }
    }
    publish(state.producer_unknown, load(state.producer_unknown) | (record.flags & IntegrityMask));
    record.flags |= load(state.producer_unknown) | load(state.consumer_unknown);
    record.flags |= seenSeven(record.hooks[0].seen)<<16 | seenSeven(record.hooks[1].seen)<<24;
    if (record.lifetime_seen[0]) record.flags |= 1u<<23;
    if (record.lifetime_seen[1]) record.flags |= 1u<<31;
    if (load(state.report_state)) { producerUnknown(ReportBusy | OutputUnknown); return; }
    state.report=record; // Immutable until the main consumer releases state2.
    publish(state.report_state,1);
}
bool schedulerNextRow(char* destination, size_t capacity, uint32_t& ordinal) {
    ordinal=0;
    const uint32_t owner=load(state.report_state);
    if (!owner) return false;
    if (owner==1u) { state.row=0; state.awaiting_ack=0; publish(state.report_state,2); }
    if (state.awaiting_ack || state.row>=6u || !destination || capacity<76u) {
        consumerUnknown(FormatInvalid | OutputUnknown); publish(state.report_state,0); return false;
    }
    const Record& record=state.report;
    Row row{destination,capacity,0,true};
    if (state.row<2u) {
        row.character('2'); row.character(char('0'+state.row)); // Protocol2 headers20/21.
        row.hex(record.key.ordinal);
        if (!state.row) {
            row.hex(record.key.stream); row.hex(record.key.output); row.hex(record.key.decision);
            for (unsigned i=0;i<3;++i) row.hex(record.roles[i]);
            row.hex(record.flags | load(state.producer_unknown) | load(state.consumer_unknown));
        } else {
            row.hex(record.start); row.hex(record.end); row.hex(record.t2); row.hex(record.mark); row.hex(record.t3);
            row.hex(record.host_before); row.hex(record.host_after);
        }
    } else {
        const unsigned hook=(state.row-2u)/2u, slot=(state.row-2u)%2u;
        const Event& event=record.hooks[hook].events[slot];
        row.character('E'); row.character(char('0'+hook)); row.character(char('0'+slot));
        row.hex(record.key.ordinal);
        const uint32_t export_unknown=(load(state.producer_unknown) | load(state.consumer_unknown)) ? 128u : 0u;
        row.hex(event.flags | export_unknown,3); // Preserve entry/return phase bits8..11.
        row.hex(event.enter); row.hex(event.leave); row.hex(event.curr0); row.hex(event.next0); row.hex(event.curr1); row.hex(event.next1);
    }
    if (!row.finish()) { consumerUnknown(FormatInvalid | OutputUnknown); publish(state.report_state,0); return false; }
    ordinal=record.key.ordinal; state.awaiting_ack=1; state.ack_seen=state.ack_accepted=0;
    return true;
}
void schedulerConsoleReceipt(bool accepted) {
    if (state.awaiting_ack) { state.ack_seen=1; state.ack_accepted=accepted ? 1u : 0u; }
}
void schedulerRowFinished(uint32_t ordinal) {
    if (load(state.report_state)!=2u || state.report.key.ordinal!=ordinal || !state.awaiting_ack) {
        consumerUnknown(KeyInvalid | OutputUnknown); return;
    }
    if (!state.ack_seen || !state.ack_accepted) consumerUnknown(OutputUnknown);
    state.awaiting_ack=0;
    if (++state.row==6u) publish(state.report_state,0);
}

// Wrappers retain exactly one unconditional real call, regardless of capture state.
extern "C" void __wrap_osRtxTick_Handler(void) {
    Ticket ticket=enterHook(0);
    __real_osRtxTick_Handler();
    leaveHook(0,ticket);
}
extern "C" void __wrap_osRtxPendSV_Handler(void) {
    Ticket ticket=enterHook(1);
    __real_osRtxPendSV_Handler();
    leaveHook(1,ticket);
}
}}
#endif
