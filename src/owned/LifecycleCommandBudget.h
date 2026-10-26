#ifndef PRIVATE_LIFECYCLE_COMMAND_BUDGET_H
#define PRIVATE_LIFECYCLE_COMMAND_BUDGET_H

namespace private_lifecycle {

// Facts have already been validated by the future caller. This enum performs
// no event parsing, status/handle/owner checks, or admission classification.
enum class ValidatedLifecycle {
    Other,
    AdmittedLeSuccess,
    MatchingLiveLeDisconnectSuccess,
    RejectedLeSuccess
};

// Only these already-validated classes have a narrower synchronous budget.
// The caller retains responsibility for classifying all real event facts.
constexpr unsigned commandBudget(unsigned conservative, ValidatedLifecycle fact) {
    switch (fact) {
    case ValidatedLifecycle::AdmittedLeSuccess:
    case ValidatedLifecycle::MatchingLiveLeDisconnectSuccess:
        return 0;
    case ValidatedLifecycle::RejectedLeSuccess:
        return 1;
    case ValidatedLifecycle::Other:
        break;
    }
    return conservative;
}

} // namespace private_lifecycle

#endif
