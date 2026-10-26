// Compile-only lifecycle contract against the actual mailbox; no target/native execution.
// The unchanged legacy dependencies use compact multi-statement lines. Keep
// strict diagnostics for the new mailbox and fixture without reformatting them.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "detail/AudioClockControl.h"
#include "platform/I2sClockTrim.h"
#pragma GCC diagnostic pop
#include "detail/AudioClockMailbox.h"
#include <limits.h>
#include <type_traits>
#include <utility>

namespace {
using namespace giga_bredr::detail;

static_assert(std::is_same<decltype(std::declval<const AudioClockMailbox&>().state()), ClockMailState>::value, "state value getter");
static_assert(std::is_same<decltype(std::declval<const AudioClockMailbox&>().request()), const ClockMailRequest&>::value, "request is read-only");
static_assert(std::is_same<decltype(std::declval<const AudioClockMailbox&>().completion()), const ClockMailCompletion&>::value, "completion is read-only");
static_assert(std::is_same<decltype(std::declval<AudioClockMailbox&>().request()), const ClockMailRequest&>::value, "no mutable request getter");
static_assert(std::is_same<decltype(std::declval<AudioClockMailbox&>().completion()), const ClockMailCompletion&>::value, "no mutable completion getter");
static_assert(std::is_same<decltype(std::declval<const AudioClockMailbox&>().counts()), ClockMailCounts>::value, "counts value getter");
static_assert(std::is_same<decltype(std::declval<const AudioClockMailbox&>().key()), ClockMailKey>::value, "key value getter");
static_assert(std::is_standard_layout<AudioClockMailbox>::value, "fixed standard-layout mailbox");
static_assert(std::is_trivially_copyable<AudioClockMailbox>::value, "fixed copyable mailbox");
static_assert(sizeof(AudioClockMailbox) <= 256, "bounded ARM mailbox including private scheduler layout");

constexpr ClockMailRequest request(uint32_t stream, uint32_t output, uint32_t serial, uint32_t queued_us) {
    ClockMailRequest r{};
    r.decision.stream_epoch = stream; r.decision.output_epoch = output; r.decision.serial = serial;
    r.decision.requested_q16 = 5; r.decision.lower_q16 = 0; r.decision.upper_q16 = 10;
    r.decision.update = true; r.queued_us = queued_us;
    return r;
}
constexpr bool same_key(ClockMailKey a, ClockMailKey b) {
    return a.stream == b.stream && a.output == b.output && a.serial == b.serial;
}
constexpr bool same_request(const ClockMailRequest& a, const ClockMailRequest& b) {
    return a.decision.stream_epoch == b.decision.stream_epoch &&
        a.decision.output_epoch == b.decision.output_epoch && a.decision.serial == b.decision.serial &&
        a.decision.requested_q16 == b.decision.requested_q16 && a.decision.lower_q16 == b.decision.lower_q16 &&
        a.decision.upper_q16 == b.decision.upper_q16 && a.decision.update == b.decision.update &&
        a.queued_us == b.queued_us && a.context.decode_errors == b.context.decode_errors &&
        a.context.dropped == b.context.dropped && a.context.discarded == b.context.discarded &&
        a.context.missing == b.context.missing;
}
// Every raw field has a distinct logical value so partial copies are observable.
constexpr ClockTrimResult raw_result() {
    ClockTrimResult r{};
    r.status = ClockTrimCode::Applied; r.output_epoch = 22; r.duration_cycles = 123;
    r.wait_cycles[0] = 17; r.wait_cycles[1] = 19; r.max_critical_cycles = 23;
    r.requested_q16 = 5; r.applied_q16 = 5; r.fraction = 29; r.readback_fraction = 31;
    r.configured_core_hz = 64000000; r.wait1_parts[0] = 37; r.wait1_parts[1] = 41;
    r.wait1_parts[2] = 43; r.wait1_parts[3] = 47; r.witness_mask = 0x7f;
    r.restore_t2 = 53; r.restore_t3 = 59; r.restore_split[0] = 61; r.restore_split[1] = 67;
    r.restore_icsr = 71; r.restore_basepri = 73; r.restore_seen = 1; r.restore_flags = 7;
    r.restore_pending = 2; r.wall_overruns = 1; r.accepted = true; r.partial = false; r.duration_valid = true;
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
    r.scheduler_wait1_start = 79; r.scheduler_wait1_end = 83; r.scheduler_host_after = 89;
#endif
    return r;
}
constexpr bool same_raw(const ClockTrimResult& a, const ClockTrimResult& b) {
    // Separate checks retain every field comparison while avoiding the old
    // compiler's failed constexpr evaluation of the combined scalar expression.
    if (a.status != b.status) return false;
    if (a.output_epoch != b.output_epoch) return false;
    if (a.duration_cycles != b.duration_cycles) return false;
    if (a.max_critical_cycles != b.max_critical_cycles) return false;
    if (a.requested_q16 != b.requested_q16) return false;
    if (a.applied_q16 != b.applied_q16) return false;
    if (a.fraction != b.fraction) return false;
    if (a.readback_fraction != b.readback_fraction) return false;
    if (a.configured_core_hz != b.configured_core_hz) return false;
    if (a.witness_mask != b.witness_mask) return false;
    if (a.restore_t2 != b.restore_t2) return false;
    if (a.restore_t3 != b.restore_t3) return false;
    if (a.restore_icsr != b.restore_icsr) return false;
    if (a.restore_basepri != b.restore_basepri) return false;
    if (a.restore_seen != b.restore_seen) return false;
    if (a.restore_flags != b.restore_flags) return false;
    if (a.restore_pending != b.restore_pending) return false;
    if (a.wall_overruns != b.wall_overruns) return false;
    if (a.accepted != b.accepted) return false;
    if (a.partial != b.partial) return false;
    if (a.duration_valid != b.duration_valid) return false;
    for (unsigned i = 0; i < 2; ++i) {
        if (a.wait_cycles[i] != b.wait_cycles[i]) return false;
        if (a.restore_split[i] != b.restore_split[i]) return false;
    }
    for (unsigned i = 0; i < 4; ++i) if (a.wait1_parts[i] != b.wait1_parts[i]) return false;
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
    if (a.scheduler_wait1_start != b.scheduler_wait1_start) return false;
    if (a.scheduler_wait1_end != b.scheduler_wait1_end) return false;
    if (a.scheduler_host_after != b.scheduler_host_after) return false;
#endif
    return true;
}
constexpr bool balanced(const AudioClockMailbox& m) {
    const auto c = m.counts();
    return !c.unknown && uint64_t(c.issued) == uint64_t(c.acked) + c.canceled + c.retired + c.ack_faults +
        (m.state() != ClockMailState::Empty ? 1u : 0u) &&
        uint64_t(c.executed) == uint64_t(c.recorded) + (m.state() == ClockMailState::Executing ? 1u : 0u);
}

// Catches overwrite, missing execution/recording, raw-field loss and duplicate terminal accounting.
constexpr bool queued_done_ack() {
    AudioClockMailbox m{}; const auto r = request(11,22,33,100); const ClockMailKey k{11,22,33};
    if (m.state() != ClockMailState::Empty || !balanced(m)) return false;
    if (m.offer(r) != ClockMailCode::Ok || m.state() != ClockMailState::Queued || !balanced(m)) return false;
    if (m.offer(request(12,23,1,100)) != ClockMailCode::Busy || !same_request(m.request(),r)) return false;
    if (m.claim(k,101) != ClockMailCode::Ok || m.state() != ClockMailState::Executing || !balanced(m)) return false;
    const auto raw = raw_result();
    if (m.complete(k,raw,true) != ClockMailCode::Ok || m.state() != ClockMailState::Done || !balanced(m)) return false;
    if (!same_raw(m.completion().raw,raw) || !m.completion().executed || !m.completion().witnessed || m.completion().retired) return false;
    if (!same_request(m.request(),r) || m.finish(k,ClockMailTerminal::AckedResult) != ClockMailCode::Ok || !balanced(m)) return false;
    const auto c = m.counts();
    return m.state() == ClockMailState::Empty && c.issued == 1 && c.executed == 1 && c.recorded == 1 && c.acked == 1 &&
        c.canceled == 0 && c.retired == 0 && c.ack_faults == 0;
}
// Catches cancellation that invents an actuator result or execution.
constexpr bool cancel_live() {
    AudioClockMailbox m{}; const ClockMailKey k{11,22,33}; const ClockTrimResult untouched{};
    if (m.offer(request(11,22,33,100)) != ClockMailCode::Ok) return false;
    if (m.cancel(k,ClockMailReason::Ineligible) != ClockMailCode::Ok || m.state() != ClockMailState::Canceled) return false;
    if (m.completion().reason != ClockMailReason::Ineligible || m.completion().executed || m.completion().witnessed ||
        !same_raw(m.completion().raw,untouched) || !balanced(m)) return false;
    if (m.finish(k,ClockMailTerminal::CanceledBeforeExec) != ClockMailCode::Ok) return false;
    const auto c = m.counts();
    return balanced(m) && c.issued == 1 && c.canceled == 1 && c.executed == 0 && c.recorded == 0 && c.acked == 0;
}
// Catches reset replacing the old slot before its explicit terminal consumption.
constexpr bool stop_before_exec() {
    AudioClockMailbox m{}; const auto old = request(11,22,33,100); const ClockMailKey old_key{11,22,33};
    if (m.offer(old) != ClockMailCode::Ok || m.retire(ClockMailReason::LifecycleReset) != ClockMailCode::Ok) return false;
    const auto saved_key = m.key(); const ClockMailKey current_key{12,23,1};
    if (!same_key(saved_key,old_key) || same_key(saved_key,current_key) || !same_request(m.request(),old)) return false;
    if (m.state() != ClockMailState::Canceled || !m.completion().retired || m.completion().executed ||
        m.completion().reason != ClockMailReason::LifecycleReset || !balanced(m)) return false;
    if (m.retire(ClockMailReason::Shutdown) != ClockMailCode::Ok || m.completion().reason != ClockMailReason::LifecycleReset) return false;
    if (m.offer(request(12,23,1,101)) != ClockMailCode::Busy || !same_key(m.key(),old_key)) return false;
    if (m.finish(saved_key,ClockMailTerminal::CanceledBeforeExec) != ClockMailCode::Ok || !balanced(m)) return false;
    return m.offer(request(12,23,1,101)) == ClockMailCode::Ok && same_key(m.key(),current_key) && balanced(m);
}
// Catches loss of old-epoch timing or an ACK terminal into the new controller identity.
constexpr bool done_then_reconfigure() {
    AudioClockMailbox m{}; const auto r = request(11,22,33,100); const ClockMailKey k{11,22,33};
    const auto raw = raw_result(); const ClockMailKey new_key{12,23,1};
    if (m.offer(r) != ClockMailCode::Ok || m.claim(k,101) != ClockMailCode::Ok || m.complete(k,raw,true) != ClockMailCode::Ok) return false;
    if (m.retire(ClockMailReason::EpochRetired) != ClockMailCode::Ok || m.state() != ClockMailState::Done ||
        !m.completion().retired || !m.completion().witnessed || !m.completion().executed) return false;
    if (!same_key(m.key(),k) || !same_raw(m.completion().raw,raw) || !same_request(m.request(),r)) return false;
    if (m.finish(new_key,ClockMailTerminal::RetiredExecuted) != ClockMailCode::KeyMismatch ||
        m.finish(k,ClockMailTerminal::AckedResult) != ClockMailCode::InvalidState || !same_raw(m.completion().raw,raw)) return false;
    if (m.finish(k,ClockMailTerminal::RetiredExecuted) != ClockMailCode::Ok) return false;
    const auto c = m.counts();
    return balanced(m) && c.issued == 1 && c.executed == 1 && c.recorded == 1 && c.retired == 1 && c.acked == 0;
}
// Catches finalizer replay, teardown of an executing slot and loss of an actual shutdown result.
constexpr bool shutdown_final_drain() {
    const ClockMailKey k{11,22,33}; const auto raw = raw_result(); AudioClockMailbox queued{};
    if (queued.retire(ClockMailReason::Shutdown) != ClockMailCode::Empty || !balanced(queued)) return false;
    if (queued.offer(request(11,22,33,100)) != ClockMailCode::Ok || queued.retire(ClockMailReason::Shutdown) != ClockMailCode::Ok) return false;
    if (queued.state() != ClockMailState::Canceled || queued.completion().reason != ClockMailReason::Shutdown || queued.completion().executed) return false;
    if (queued.finish(k,ClockMailTerminal::CanceledBeforeExec) != ClockMailCode::Ok || queued.finish(k,ClockMailTerminal::CanceledBeforeExec) != ClockMailCode::Empty || !balanced(queued)) return false;
    AudioClockMailbox done{};
    if (done.offer(request(11,22,33,100)) != ClockMailCode::Ok || done.claim(k,101) != ClockMailCode::Ok) return false;
    const auto before = done.request(); const auto counts = done.counts();
    if (done.retire(ClockMailReason::Shutdown) != ClockMailCode::InvalidState || done.state() != ClockMailState::Executing ||
        !same_request(done.request(),before) || done.counts().executed != counts.executed || done.completion().retired || !balanced(done)) return false;
    if (done.complete(k,raw,true) != ClockMailCode::Ok || done.retire(ClockMailReason::Shutdown) != ClockMailCode::Ok ||
        !same_raw(done.completion().raw,raw) || !done.completion().retired) return false;
    if (done.finish(k,ClockMailTerminal::RetiredExecuted) != ClockMailCode::Ok || done.finish(k,ClockMailTerminal::RetiredExecuted) != ClockMailCode::Empty) return false;
    return balanced(done) && done.counts().issued == 1 && done.counts().recorded == 1 && done.counts().retired == 1;
}
// Catches widening the stale gap, off-by-one expiry and signed/subtraction wrap errors.
constexpr bool stale_wrap() {
    const ClockMailKey k{11,22,33}; AudioClockMailbox boundary{}, expired{}, wrap{}, ambiguous{};
    if (boundary.offer(request(11,22,33,100)) != ClockMailCode::Ok || boundary.claim(k,250100) != ClockMailCode::Ok || !balanced(boundary)) return false;
    if (expired.offer(request(11,22,33,100)) != ClockMailCode::Ok || expired.claim(k,250101) != ClockMailCode::Expired) return false;
    if (expired.state() != ClockMailState::Canceled || expired.completion().reason != ClockMailReason::StaleGap ||
        expired.completion().executed || expired.counts().executed != 0 || !balanced(expired)) return false;
    if (wrap.offer(request(11,22,33,0xfffffff0u)) != ClockMailCode::Ok || wrap.claim(k,0x10u) != ClockMailCode::Ok || !balanced(wrap)) return false;
    if (ambiguous.offer(request(11,22,33,0)) != ClockMailCode::Ok || ambiguous.claim(k,0x80000000u) != ClockMailCode::Expired) return false;
    return ambiguous.state() == ClockMailState::Canceled && ambiguous.completion().reason == ClockMailReason::StaleGap &&
        ambiguous.counts().executed == 0 && balanced(ambiguous);
}
// Catches incomplete identity comparison, invalid-state acceptance, unwitnessed results and replay.
constexpr bool duplicate_wake_key() {
    AudioClockMailbox m{}; const ClockMailKey k{11,22,33}; const auto r = request(11,22,33,100); const auto raw = raw_result();
    if (m.claim(k,100) != ClockMailCode::Empty || m.complete(k,raw,true) != ClockMailCode::Empty ||
        m.cancel(k,ClockMailReason::Ineligible) != ClockMailCode::Empty || m.finish(k,ClockMailTerminal::AckFault) != ClockMailCode::Empty) return false;
    auto invalid = r; invalid.decision.update = false;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision || !balanced(m)) return false;
    invalid = r; invalid.decision.stream_epoch = 0;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision) return false;
    invalid = r; invalid.decision.output_epoch = 0;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision) return false;
    invalid = r; invalid.decision.serial = 0;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision) return false;
    invalid = r; invalid.decision.lower_q16 = 6;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision) return false;
    invalid = r; invalid.decision.upper_q16 = 4;
    if (m.offer(invalid) != ClockMailCode::InvalidDecision || !balanced(m)) return false;
    if (m.offer(r) != ClockMailCode::Ok) return false;
    const ClockMailKey wrong[3] = {{12,22,33},{11,23,33},{11,22,34}};
    for (unsigned i = 0; i < 3; ++i) {
        if (m.claim(wrong[i],101) != ClockMailCode::KeyMismatch || m.cancel(wrong[i],ClockMailReason::Ineligible) != ClockMailCode::KeyMismatch ||
            !same_request(m.request(),r) || !same_key(m.key(),k) || !balanced(m)) return false;
    }
    if (m.complete(k,raw,true) != ClockMailCode::InvalidState || m.finish(k,ClockMailTerminal::AckedResult) != ClockMailCode::InvalidState) return false;
    if (m.claim(k,101) != ClockMailCode::Ok || m.claim(k,102) != ClockMailCode::Busy || m.cancel(k,ClockMailReason::Ineligible) != ClockMailCode::InvalidState) return false;
    for (unsigned i = 0; i < 3; ++i) if (m.complete(wrong[i],raw,true) != ClockMailCode::KeyMismatch) return false;
    const ClockTrimResult untouched{};
    if (m.complete(k,raw,false) != ClockMailCode::InvalidDecision || m.state() != ClockMailState::Executing ||
        m.counts().recorded != 0 || !same_raw(m.completion().raw,untouched) || !balanced(m)) return false;
    if (m.complete(k,raw,true) != ClockMailCode::Ok) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (m.state() != ClockMailState::Done || m.claim(k,103) != ClockMailCode::Busy ||
            m.finish(wrong[i],ClockMailTerminal::AckedResult) != ClockMailCode::KeyMismatch ||
            m.complete(k,raw,true) != ClockMailCode::InvalidState || !same_raw(m.completion().raw,raw) ||
            !same_key(m.key(),k) || !same_request(m.request(),r) || !balanced(m)) return false;
    }
    if (m.finish(k,ClockMailTerminal::CanceledBeforeExec) != ClockMailCode::InvalidState || m.finish(k,ClockMailTerminal::RetiredExecuted) != ClockMailCode::InvalidState) return false;
    if (m.finish(k,ClockMailTerminal::AckedResult) != ClockMailCode::Ok || m.finish(k,ClockMailTerminal::AckedResult) != ClockMailCode::Empty) return false;
    return balanced(m) && m.counts().executed == 1 && m.counts().recorded == 1 && m.counts().acked == 1;
}
// Catches wrapping accounting; uses the real add helper without a mutation/seeding API.
constexpr bool saturation() {
    uint32_t value = UINT32_MAX - 1u; bool unknown = false;
    if (!clockMailAdd(value,unknown) || value != UINT32_MAX || unknown) return false;
    if (clockMailAdd(value,unknown) || value != UINT32_MAX || !unknown) return false;
    if (clockMailAdd(value,unknown) || value != UINT32_MAX || !unknown) return false;
    return sizeof(AudioClockMailbox) <= 256 && std::is_standard_layout<AudioClockMailbox>::value && std::is_trivially_copyable<AudioClockMailbox>::value;
}
// Catches filtering a failed/partial/bad-epoch/out-of-range actuator result into synthetic success.
constexpr bool truthful_bad_result() {
    AudioClockMailbox m{}; const ClockMailKey k{11,22,33}; auto raw = raw_result();
    raw.status = ClockTrimCode::ReadbackFault; raw.output_epoch = 999; raw.applied_q16 = 100;
    raw.accepted = false; raw.partial = true; raw.duration_valid = false; raw.duration_cycles = 987;
    raw.wall_overruns = 3; raw.witness_mask = 0x80;
    if (m.offer(request(11,22,33,100)) != ClockMailCode::Ok || m.claim(k,101) != ClockMailCode::Ok ||
        m.complete(k,raw,true) != ClockMailCode::Ok || !same_raw(m.completion().raw,raw)) return false;
    if (!m.completion().executed || !m.completion().witnessed || !balanced(m)) return false;
    if (m.finish(k,ClockMailTerminal::AckFault) != ClockMailCode::Ok) return false;
    AudioClockMailbox canceled{};
    if (canceled.offer(request(11,22,33,100)) != ClockMailCode::Ok || canceled.cancel(k,ClockMailReason::Ineligible) != ClockMailCode::Ok ||
        canceled.finish(k,ClockMailTerminal::AckFault) != ClockMailCode::Ok || !balanced(canceled)) return false;
    return balanced(m) && m.counts().issued == 1 && m.counts().executed == 1 && m.counts().recorded == 1 &&
        m.counts().ack_faults == 1 && m.counts().acked == 0 && canceled.counts().ack_faults == 1 && canceled.counts().executed == 0;
}
// Catches counter resets across epochs and a second terminal/execution for an old identity.
constexpr bool sequential_lifetimes() {
    AudioClockMailbox m{}; const auto raw = raw_result();
    const ClockMailKey a{11,22,33}, b{11,22,34}, c{12,23,1}, d{13,24,1};
#define STEP_BALANCED(operation, expected) if ((operation) != (expected) || !balanced(m)) return false
    if (!balanced(m)) return false;
    STEP_BALANCED(m.offer(request(11,22,33,100)), ClockMailCode::Ok);
    STEP_BALANCED(m.claim(a,101), ClockMailCode::Ok);
    STEP_BALANCED(m.complete(a,raw,true), ClockMailCode::Ok);
    STEP_BALANCED(m.finish(a,ClockMailTerminal::AckedResult), ClockMailCode::Ok);
    STEP_BALANCED(m.finish(a,ClockMailTerminal::AckedResult), ClockMailCode::Empty);
    STEP_BALANCED(m.offer(request(11,22,34,200)), ClockMailCode::Ok);
    STEP_BALANCED(m.claim(a,201), ClockMailCode::KeyMismatch);
    STEP_BALANCED(m.cancel(b,ClockMailReason::Ineligible), ClockMailCode::Ok);
    STEP_BALANCED(m.finish(b,ClockMailTerminal::AckedResult), ClockMailCode::InvalidState);
    STEP_BALANCED(m.finish(b,ClockMailTerminal::CanceledBeforeExec), ClockMailCode::Ok);
    STEP_BALANCED(m.offer(request(12,23,1,300)), ClockMailCode::Ok);
    STEP_BALANCED(m.claim(c,301), ClockMailCode::Ok);
    STEP_BALANCED(m.complete(c,raw,true), ClockMailCode::Ok);
    STEP_BALANCED(m.finish(a,ClockMailTerminal::AckedResult), ClockMailCode::KeyMismatch);
    STEP_BALANCED(m.finish(c,ClockMailTerminal::AckedResult), ClockMailCode::Ok);
    STEP_BALANCED(m.offer(request(13,24,1,400)), ClockMailCode::Ok);
    STEP_BALANCED(m.claim(d,401), ClockMailCode::Ok);
    STEP_BALANCED(m.complete(d,raw,true), ClockMailCode::Ok);
    STEP_BALANCED(m.retire(ClockMailReason::EpochRetired), ClockMailCode::Ok);
    if (!same_raw(m.completion().raw,raw) || !same_key(m.key(),d)) return false;
    STEP_BALANCED(m.finish(d,ClockMailTerminal::RetiredExecuted), ClockMailCode::Ok);
    STEP_BALANCED(m.finish(d,ClockMailTerminal::RetiredExecuted), ClockMailCode::Empty);
    STEP_BALANCED(m.claim(a,402), ClockMailCode::Empty);
#undef STEP_BALANCED
    const auto counts = m.counts();
    return m.state() == ClockMailState::Empty && counts.issued == 4 && counts.executed == 3 && counts.recorded == 3 &&
        counts.acked == 2 && counts.canceled == 1 && counts.retired == 1 && counts.ack_faults == 0;
}

static_assert(queued_done_ack(), "one executed result / one terminal ACK");
static_assert(cancel_live(), "no fabricated result for canceled request");
static_assert(stop_before_exec(), "retire queued before controller reset");
static_assert(done_then_reconfigure(), "retain executed old-epoch result");
static_assert(shutdown_final_drain(), "end drains once / no replay");
static_assert(stale_wrap(), "250000 boundary and uint32 clock wrap");
static_assert(duplicate_wake_key(), "duplicate wakes and wrong keys do not replay");
static_assert(saturation(), "saturating accounting, bounded ARM layout");
static_assert(truthful_bad_result(), "raw faulty result is never filtered");
static_assert(sequential_lifetimes(), "one terminal outcome per issued request");
} // namespace
