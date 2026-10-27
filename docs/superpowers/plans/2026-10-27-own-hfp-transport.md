# Own RFCOMM and HF/HS control implementation plan

> For agentic workers: execute inline task by task; Root owns compiler, Git and hardware.

Goal: add bounded, independently testable own-stack RFCOMM and HF/HS control modules without advertising incomplete call audio.

Architecture: C RFCOMM core owns one generation-qualified mux with two DLCs; the existing L2CAP owner supplies security, reserved RX capacity and durable output. HF/HS control uses bounded line assembly and one outstanding AT transaction. A separate voice contract defines mono S16 PCM, codec identity and microphone lifetime; it does not reuse A2DP codecs.

Spec: the Root task and docs/hfp-transport-status.md (sources and integration gaps).
Tech stack: existing C11/C++14 core conventions, fixed POD storage, no SDK/RTOS dependency.

## Global constraints
- Only new bth_rfcomm.*, Rfcomm*, Hfp*, Hsp*, dedicated tests/docs; no integration-file edits.
- No heap, blocking, IRQ work, timers/global registration, BTstack runtime, guessed controller VSC, phone call or contact operation.
- No HF/HS SDP records, scan UUIDs or PSM3 admission until integration and real audio are complete and reviewed.
- Root executes all tests; compile-only checks never count as runtime behavior evidence.

## Review focus
- Stale generations, DLC reuse and teardown must not revive credits or call state.
- Short output must not consume an AT command, credit or state transition.
- Fragmented/oversize AT lines and malformed lengths/FCS must fail boundedly.
- Credit-only UIH is permitted with zero TX credits; credit octet is excluded from length and reduces N1 payload by one.
- Feature bits must describe implemented control and externally verified codec availability; no implicit microphone.

## Tasks
- [x] Add tests/test_rfcomm.cpp: literal SABM/UA/UIH FCS vectors; malformed/canonical length; paired initiator/responder PN, SABM, bilateral MSC, credit depletion/replenishment, legacy FC, capacity rollback, stale owner, DISC/reuse.
- [x] Root confirms missing bth_rfcomm.h RED. Implement src/owned/core/bth_rfcomm.h/.c. Root compiles C/C++ and executes portable fixture (or pre-radio target selftest); record separately.
- [x] Add tests/test_hfp_control.cpp: reordered CIND map, fragmented SLC, BRSF/BAC gates, OK without result, ERROR/timeout, unsolicited ring/CIEV/gain, codec selection, call-command injection rejection, generation/reset/capacity.
- [x] Root confirms missing HfpControl.h RED. Implement HfpControl/HspControl/HfpAt and HfpAudio contract. Root compiles and executes fixtures, including no-mic/provider epoch behavior.
- [x] Document exact L2CAP/security/owner hooks and synchronous audio blockers. Freeze source for Root integration/review; no integration or live call claim.

Root suggested portable runtime command (only Root may execute): compile tests/test_rfcomm.cpp plus src/owned/core/bth_rfcomm.c with GIGA_BREDR_PORTABLE_TEST_MAIN; compile tests/test_hfp_control.cpp plus new profile .cpp modules similarly. Official ARM compile both scheduler modes and add one bounded target runtime fixture if native execution is unavailable.

Implementation handoff: source/fixtures written and initial missing-module ARM RED verified. Behavioral regression RED was not executed before source-review corrections. Final ARM compile, portable fixture execution and integration remain pending; checkmarks above record source work, not behavior qualification. Root retains all verification ownership.
