#pragma once
#include <stdint.h>
#include <limits.h>
#include "AudioClockControl.h"
#include "../platform/I2sClockTrim.h"

namespace giga_bredr { namespace detail {

enum class ClockMailState : uint8_t { Empty, Queued, Executing, Done, Canceled };
enum class ClockMailCode : uint8_t { Ok, Empty, Busy, InvalidDecision, KeyMismatch, InvalidState, Expired, Saturated };
enum class ClockMailReason : uint8_t { None, Ineligible, StaleGap, LifecycleReset, Shutdown, EpochRetired };
enum class ClockMailTerminal : uint8_t { AckedResult, CanceledBeforeExec, RetiredExecuted, AckFault };

struct ClockMailKey { uint32_t stream=0, output=0, serial=0; };
struct ClockMailContext {
    uint32_t decode_errors=0, dropped=0;
    uint64_t discarded=0, missing=0;
};
struct ClockMailRequest {
    AudioClockDecision decision;
    uint32_t queued_us=0;
    ClockMailContext context;
};
struct ClockMailCompletion {
    ClockTrimResult raw;
    ClockMailReason reason=ClockMailReason::None;
    bool executed=false, retired=false, witnessed=false;
};
struct ClockMailCounts {
    uint32_t issued=0, executed=0, recorded=0, acked=0, canceled=0, retired=0, ack_faults=0;
    bool unknown=false;
};

constexpr bool clockMailAdd(uint32_t& value, bool& unknown) {
    if (value == UINT32_MAX) {
        unknown = true;
        return false;
    }
    ++value;
    return true;
}

// A single fixed slot. The caller owns serialization for every method and
// payload reference. No hardware, RTOS operation or unlocked publication lives
// here. Accounting belongs to this mailbox lifetime, not a controller epoch.
class AudioClockMailbox {
public:
    constexpr AudioClockMailbox() = default;

    constexpr ClockMailCode offer(const ClockMailRequest& request) {
        if (state_ != ClockMailState::Empty) return ClockMailCode::Busy;
        const auto& d = request.decision;
        if (!d.update || !d.stream_epoch || !d.output_epoch || !d.serial ||
            d.lower_q16 > d.requested_q16 || d.requested_q16 > d.upper_q16)
            return ClockMailCode::InvalidDecision;
        if (counts_.unknown) return ClockMailCode::Saturated;
        if (!clockMailAdd(counts_.issued, counts_.unknown)) return ClockMailCode::Saturated;
        request_ = request;
        completion_ = ClockMailCompletion{};
        state_ = ClockMailState::Queued;
        return ClockMailCode::Ok;
    }

    constexpr ClockMailCode claim(ClockMailKey key, uint32_t now_us) {
        if (state_ == ClockMailState::Empty) return ClockMailCode::Empty;
        if (state_ != ClockMailState::Queued) return ClockMailCode::Busy;
        if (!matches(key)) return ClockMailCode::KeyMismatch;
        // Reject before either execution or expiry can change this slot when
        // execution accounting can no longer describe a new claim.
        if (counts_.unknown) return ClockMailCode::Saturated;
        if (counts_.executed == UINT32_MAX) {
            counts_.unknown = true;
            return ClockMailCode::Saturated;
        }
        const uint32_t age = uint32_t(now_us - request_.queued_us);
        if (age >= 0x80000000u || age > 250000u) {
            completion_.reason = ClockMailReason::StaleGap;
            state_ = ClockMailState::Canceled;
            return ClockMailCode::Expired;
        }
        clockMailAdd(counts_.executed, counts_.unknown);
        state_ = ClockMailState::Executing;
        return ClockMailCode::Ok;
    }

    constexpr ClockMailCode complete(ClockMailKey key, const ClockTrimResult& result, bool witnessed) {
        if (state_ == ClockMailState::Empty) return ClockMailCode::Empty;
        if (state_ != ClockMailState::Executing) return ClockMailCode::InvalidState;
        if (!matches(key)) return ClockMailCode::KeyMismatch;
        if (!witnessed) return ClockMailCode::InvalidDecision;
        // Failed, partial, bad-epoch and invalid-timing results are still the
        // actual result. The consumer, rather than this transport, validates it.
        completion_.raw = result;
        completion_.executed = true;
        completion_.witnessed = true;
        state_ = ClockMailState::Done;
        return clockMailAdd(counts_.recorded, counts_.unknown) ? ClockMailCode::Ok : ClockMailCode::Saturated;
    }

    constexpr ClockMailCode cancel(ClockMailKey key, ClockMailReason reason) {
        if (state_ == ClockMailState::Empty) return ClockMailCode::Empty;
        if (state_ != ClockMailState::Queued) return ClockMailCode::InvalidState;
        if (!matches(key)) return ClockMailCode::KeyMismatch;
        completion_.reason = reason;
        state_ = ClockMailState::Canceled;
        return ClockMailCode::Ok;
    }

    constexpr ClockMailCode retire(ClockMailReason reason) {
        if (state_ == ClockMailState::Empty) return ClockMailCode::Empty;
        if (state_ == ClockMailState::Executing) return ClockMailCode::InvalidState;
        if (state_ == ClockMailState::Queued) {
            completion_.reason = reason;
            state_ = ClockMailState::Canceled;
        } else if (state_ == ClockMailState::Done) {
            completion_.reason = reason;
        }
        // Canceled keeps its first reason. In all cases raw/witnessed/executed
        // and the immutable old request survive the lifecycle reset.
        completion_.retired = true;
        return ClockMailCode::Ok;
    }

    constexpr ClockMailCode finish(ClockMailKey key, ClockMailTerminal terminal) {
        if (state_ == ClockMailState::Empty) return ClockMailCode::Empty;
        if (state_ != ClockMailState::Done && state_ != ClockMailState::Canceled)
            return ClockMailCode::InvalidState;
        if (!matches(key)) return ClockMailCode::KeyMismatch;
        bool counted = false;
        switch (terminal) {
        case ClockMailTerminal::AckedResult:
            if (state_ != ClockMailState::Done || completion_.retired) return ClockMailCode::InvalidState;
            counted = clockMailAdd(counts_.acked, counts_.unknown);
            break;
        case ClockMailTerminal::CanceledBeforeExec:
            if (state_ != ClockMailState::Canceled) return ClockMailCode::InvalidState;
            counted = clockMailAdd(counts_.canceled, counts_.unknown);
            break;
        case ClockMailTerminal::RetiredExecuted:
            if (state_ != ClockMailState::Done || !completion_.retired) return ClockMailCode::InvalidState;
            counted = clockMailAdd(counts_.retired, counts_.unknown);
            break;
        case ClockMailTerminal::AckFault:
            counted = clockMailAdd(counts_.ack_faults, counts_.unknown);
            break;
        default:
            return ClockMailCode::InvalidState;
        }
        // Consumption cannot be replayed even if its counter saturated. Payload
        // remains readable under serialization until the next successful offer.
        state_ = ClockMailState::Empty;
        return counted ? ClockMailCode::Ok : ClockMailCode::Saturated;
    }

    constexpr ClockMailState state() const { return state_; }
    constexpr const ClockMailRequest& request() const { return request_; }
    constexpr const ClockMailCompletion& completion() const { return completion_; }
    constexpr ClockMailCounts counts() const { return counts_; }
    constexpr ClockMailKey key() const {
        return {request_.decision.stream_epoch, request_.decision.output_epoch, request_.decision.serial};
    }

private:
    constexpr bool matches(ClockMailKey key) const {
        return key.stream && key.output && key.serial &&
            key.stream == request_.decision.stream_epoch && key.output == request_.decision.output_epoch &&
            key.serial == request_.decision.serial;
    }

    ClockMailRequest request_{};
    ClockMailCompletion completion_{};
    ClockMailCounts counts_{};
    ClockMailState state_=ClockMailState::Empty;
};

}} // namespace giga_bredr::detail
