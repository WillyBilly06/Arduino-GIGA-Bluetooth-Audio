> **Workspace-only historical evidence:** Links in this page that point outside
> the library refer to local `build/dev...` artifacts. They are not part of a
> copied source package. The historical observations below are unchanged.

# Normal setup-response validation

The local library validates command-specific result sizes before interpreting
an error status or selecting a fallback. Invalid results finish setup as
MALFORMED with availability/controller-flow/receive-pacing false. Unknown write
or post-write verification outcomes require reset and admit no subsequent setup
intent. Valid completion/error-status forms retain their existing behavior.

This is a defensive change to our own decoded-response parser. Existing opcode
selection, supported-command bit84 check, initialization/defaults, security/link
code and default-off receive pacing remain unchanged. It adds no controller
command or capability workaround. The stock observation of bit84clear/flow0
still yields FlowUnavailable/reason7 without a Write Flow request. HFP/SCO/audio
availability is not established by this patch.

Direct, in-memory parser units pass4856 model cases/128479 assertions under
AddressSanitizer. They exercise our parser APIs only: no transport encoding or
dispatch, firmware asset, external target or device operation. These are unit
counts, not physical feature qualifications or an external vulnerability test.

The regression is included in the normal `tools/run_owned_tests.py` runner.
The full combined source passes99 native runtime invocations and eight child
suites:143 compiler commands,99 runtime commands and eight child commands,
250 recorded zero exits. Child sanitizer modes are recorded in their receipts.
All source bytes remain pinned before/after the checks.

The exact426 production files/191 C/C++ units pass the stock Arduino Mbed
GIGA4.6.0 Cortex-M7 build/static audit at default UART2M, all seven codec defaults
enabled. The BIN is545360 bytes, SHA-256
`aac5edf0d680186b6078ec149bf1719e2a37eb6bbb0eee2dda241ca7596b220d`.
Arduino reports539384 program bytes and54288 static RAM bytes. These section
figures do not establish runtime peak memory, stack high-water or timing bounds.

Local workspace evidence (outside the distributed source ZIP):

- [Focused defensive source review and tests](../../../dev305-owned-sco-response-validation-20261028/REPORT.md).
- [Full native receipt](../../../dev307-owned-sco-validation-m7-20261028/native-full/receipt.json).
- [Official M7 audit](../../../dev307-owned-sco-validation-m7-20261028/Receiver/build-facts.json).
- [Exact local integration](../../../dev308-sco-validation-integration-20261028/b5fa9ca3c79d4ec482b2ef8a9c27cd55/receipt.json).

No firmware was flashed for these checks. The original R4 image remains on the
board; the phone is unbonded and normal pairing awaits the user's choice. The
remaining control/volume/clock/voice/codec/electrical/acoustic/latency evidence
is scoped in the [feature matrix](all-feature-status-2026-10-28.md).

The separate additional controller-probe review was stopped by automatic safety
review for possible cybersecurity risk. That experiment was not run on hardware
and is not resumed by this defensive library change. Its drafts remain held;
no controller-probe code is included in this source integration.
