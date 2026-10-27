# Clock Host Execution Lane Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move only owned clock actuation to the existing higher-priority host execution lane, with a bounded one-slot handoff and truthful terminal accounting.

**Architecture:** Decoder computes control and consumes normal ACKs; host polls radio first, then tries codec_mutex once outside radio_mutex and executes one eligible request. One fixed mailbox retains an immutable decision and raw result through stop/configure/end; no result is overwritten or ACKed into a reset controller. No priority rewriting is used: the pinned RTX setter overwrites effective/base priorities and does not preserve active PI.

**Tech Stack:** C++14, Arduino GIGA CM7/stock mbed_giga4.6.0/RTX, existing Mutex/TryLock/EventFlags/Semaphore, official ARM GCC7 compile-only checks. No native execution.

**Spec:** `docs/superpowers/specs/2026-10-26-clock-host-execution-design.md`, corrected SHA256 `190f6164d4b78a7529670ece88f9f9146c591df394964b31068d4ad66db6baf2`; original frozen consultation SHA `fc9063536648cb84e84a9833e68ee43a2399acd304a4eaf8703dc216ecfb4805` remains unchanged. The correction clears pending_hint UNDER codec_mutex, with notifications only after unlock.

## Global Constraints

- Work only in `build/dev10-clock-host-lane-worktree`, branch `codex/clock-host-lane`, base `0b37d1f29b482328599036ac7c23084f4494e792`; populated477-file ring-parent manifest `1a5746f1baf5e3078db513680091f74b58ac90fc24d373b765154ca201536d92` is the source basis.
- All hardware backend/seven snapshots/all DWT/read sites/readbacks/masks/whole20us/CUM/all44/BLE/codecs/source/latency remain unchanged. `src/platform/GigaI2s.cpp`, `I2sClockTrim.h`, `I2sBuffer.h`, scheduler witness files, OwnedHost/core/vendor/Memory and SDK are BYTE-EXACT parent.
- No new priority/thread/heap/busywait/kernelLock or RTOS call within the measured backend. Existing host AboveNormal32/decoder Normal24, UART0/DMA6 and all hardware context guards remain.
- Observe gate1000us; controller windows/decision separation1000000us; limit500ppm/slew10ppm per second; existing250000us sample-gap rule. Do not change control math or `AudioClockControl.h`.
- Single request/result slot under existing codec_mutex; atomic pending_hint ONLY; immutable `(stream,output,serial)` key. No overwrite/coalescing of unacknowledged decisions; only wakes coalesce.
- Host polls FIRST, outside radio_mutex uses ONE codec_mutex.trylock attempt; never waits/spins/re-enters radio or adds a queue/event lock in the new service.
- Worker drains terminal BEFORE observe; notify host AFTER EACH processOne returns/unlocks, including early returns and all64 iterations. Clear hint under mutex; notify after unlocking.
- schedulerPrepare only immediately before actual backend execution. Witness every actual executed result exactly once before lifecycle reset can erase its epoch; never fabricate a result for cancellation.
- Normal ACK only on decoder. A retired controller receives no ACK; explicit canceled-before-execution/executed-retired outcomes close accounting. Existing ACK-fault fallback remains, not a duplicate successful ACK.
- End order: runningfalse/wake, hostjoin, decoder wake/final-drain/join, then i2sEnd/resource destruction. No locked cross-thread wait, extra physical retry or publication.
- Public headers/API/enums/config defaults and log row formats UNCHANGED. Private fixed counters only; existing applied_valid is false between actual execution and worker reconciliation. Existing local-backend-fault publication reports mailbox protocol/saturation/ACK faults.
- Root alone owns official compiler/tools, Git commits, source freeze/build, linked review and any later normal phone field. Parser/security/native/fuzz/wire workflows remain stopped. Current prior ring field CUM1/91/max475.85625us is FAIL despite44sampled0; no pass transfer.

## Review Focus

1. Radio callback stop/reconfigure wins before worker consumes DONE: keep actual timing/result, never ACK the new controller (Task1 cases3/4, Task2 lifecycle audit).
2. A hint/event arrives while codec_mutex is held or just released: no spin, no erased new hint, no wake before unlock (Task1 case7, Task2 ordering review).
3. End occurs during apply or decoder wait: host can finish, worker finalizer consumes/cancels exactly once, no self-join or freed output access (Task1 case5, Task2 end review).
4. Delayed request/micros wrap/counter saturation/duplicate call: exact boundary behavior, no fabricated zero timing or replay (Task1 cases6/8/9).
5. Foreground snapshot or PI waiter overlaps result publication: coherent existing stats, no new lock-order cycle/priority change; hardware thread context stays M7 thread mode (Task2 source/type/stack review; Task3 linked/field review).

---

## File map and fixed implementation choices

Production changes ONLY: create `src/detail/AudioClockMailbox.h`; modify `src/GigaBredr.cpp`. Test-only additions: `tests/test_clock_mailbox.cpp` and `tools/check_clock_mailbox.py`. Do not edit public `AudioClock.h`, control math, backend, existing fixtures or native runner. No new feature flag: existing ARDUINO RTOS path uses host lane; existing non-ARDUINO synchronous fallback stays byte/functionally unchanged and is compile-only, not native-run here.

The header may include `AudioClockControl.h` and `../platform/I2sClockTrim.h` for existing value types; it performs NO hardware/RTOS/atomic operation. It is externally serialized, constexpr-testable, standard-layout/trivially-copyable fixed storage; ARM sizeof<=256B. Impl adds this mailbox plus `std::atomic<bool> clock_pending_hint{false}`; no additional atomic/queue. Private accounting is per Impl/begin and survives clockBegin/Stop. Do not reset it per epoch.

### Task 1: Actual bounded mailbox and compile-only lifecycle assertions

**Files:**
- Create: `src/detail/AudioClockMailbox.h`
- Create: `tests/test_clock_mailbox.cpp`
- Create: `tools/check_clock_mailbox.py`

**Interfaces:** In `giga_bredr::detail`, define these exact value types:

- `enum class ClockMailState : uint8_t { Empty, Queued, Executing, Done, Canceled };`
- `enum class ClockMailCode : uint8_t { Ok, Empty, Busy, InvalidDecision, KeyMismatch, InvalidState, Expired, Saturated };`
- `enum class ClockMailReason : uint8_t { None, Ineligible, StaleGap, LifecycleReset, Shutdown, EpochRetired };`
- `enum class ClockMailTerminal : uint8_t { AckedResult, CanceledBeforeExec, RetiredExecuted, AckFault };`
- `struct ClockMailKey { uint32_t stream=0, output=0, serial=0; };`
- `struct ClockMailContext { uint32_t decode_errors=0, dropped=0; uint64_t discarded=0, missing=0; };`
- `struct ClockMailRequest { AudioClockDecision decision; uint32_t queued_us=0; ClockMailContext context; };`
- `struct ClockMailCompletion { ClockTrimResult raw; ClockMailReason reason=ClockMailReason::None; bool executed=false, retired=false, witnessed=false; };`
- `struct ClockMailCounts { uint32_t issued=0, executed=0, recorded=0, acked=0, canceled=0, retired=0, ack_faults=0; bool unknown=false; };`
- `constexpr bool clockMailAdd(uint32_t& value, bool& unknown);` adds1 unless alreadyUINT32_MAX; then saturates/setsunknown/returnsfalse. No wrap.

`class AudioClockMailbox` has only these mutating methods (all constexpr; caller already owns serialization):

- `ClockMailCode offer(const ClockMailRequest& request);`
- `ClockMailCode claim(ClockMailKey key, uint32_t now_us);`
- `ClockMailCode complete(ClockMailKey key, const ClockTrimResult& result, bool witnessed);`
- `ClockMailCode cancel(ClockMailKey key, ClockMailReason reason);`
- `ClockMailCode retire(ClockMailReason reason);`
- `ClockMailCode finish(ClockMailKey key, ClockMailTerminal terminal);`

Const getters (all constexpr/no mutation): `ClockMailState state() const`, `const ClockMailRequest& request() const`, `const ClockMailCompletion& completion() const`, `ClockMailCounts counts() const`, and `ClockMailKey key() const`. References are readable only under the caller's existing serialization; no unlocked payload access. `constexpr AudioClockMailbox()=default;` initializes Empty. No mutable getter/test seeding API. `key` equality compares all3nonzero fields; offer additionally requires decision.update and lower<=requested<=upper. Bounds remain controller-owned; complete MUST retain even bad/partial/out-of-range raw results for worker validation.

State contract:

| Operation | Valid transition / exact behavior |
|---|---|
| offer | Empty→Queued; copies immutable request; issued++. Nonempty→Busy unchanged. Invalid key/bounds→InvalidDecision unchanged. unknown or issuedMAX→Saturated/no new request. |
| claim | Queued/exactkey; unsigned age<=250000 and<2^31: Executing/executed++. Age250001 or half-range ambiguous: Canceled(reasonStaleGap,executedfalse), returnsExpired. Wrongkey/noQueued changes no payload/counter. |
| complete | Executing/exactkey/witnessedtrue→Done; stores complete raw result/executedtrue/witnessedtrue; recorded++. No filtering of raw status/epoch/duration. Duplicate/wrong-state/falsewitness rejects without replay. |
| cancel | Queued/exactkey→Canceled, reason retained, executedfalse. No fabricated ClockTrimResult. |
| retire | Empty→Empty. Queued→Canceled/retiredtrue. Done→Done/retiredtrue with RAW unchanged. Canceled→Canceled/retiredtrue preserving first cancellation reason. Executing→InvalidState/no mutation: lifecycle cannot see it while host owns codec_mutex. |
| finish | Done/current→AckedResult; Canceled→CanceledBeforeExec; Done/retired→RetiredExecuted; Done/Canceled→AckFault. Exact slotkey required. Increment selected terminal counter, then Empty. A duplicate finish returnsEmpty/no new count. |

When counts.unknown=false: `uint64(issued)==acked+canceled+retired+ack_faults+(state!=Empty)` and `executed==recorded+(state==Executing)`. Saturation makes accounting UNKNOWN, stops new offers and maps to the existing Impl local-backend-fault path; it never wraps or pretends balance. Exact rejected-operation returns: Empty slot→Empty except offer; non-Queued claim→Busy; complete/cancel/finish in another nonempty state→InvalidState; eligible state with wrong key→KeyMismatch; false witnessed marker→InvalidDecision; invalid terminal/state pairing→InvalidState. Failed operations leave request/raw/terminal counts unchanged. Counter overflow before offer/claim rejects with Saturated before transition; overflow while complete/finish still retains Done/clears consumed slot respectively, flags unknown and returns Saturated so an already executed result/ACK is never replayed. No callback, waiting or throwing from header.

- [ ] **Step 1: Write compile-only assertions against these actual interfaces.** Use existing real AudioClockDecision/ClockTrimResult, tiny logical values, no packet/wire input and no main. Add named constexpr cases with the following explicit assertions:

| Case | Required sequence and assertions |
|---|---|
|1 queued_done_ack|Offer key11/22/33 at100; duplicate offerBusy; claim at101; raw output22/applied5/duration123/overrun1; complete witnessedtrue; finishAckedResult. Empty,issued1/executed1/recorded1/acked1; raw fields preserved before finish.|
|2 cancel_live|Offer→cancelIneligible→finishCanceledBeforeExec; issued1/canceled1,executed=recorded0. No raw duration/result invented.|
|3 stop_before_exec|Offer→retireLifecycleReset; save requestkey; simulate new currentkey12/23/1; assert old retired key retained, offerBusy until finishCanceledBeforeExec, new offer then succeeds.|
|4 done_then_reconfigure|Offer→claim→complete→retireEpochRetired; raw byte-field values/old key unchanged; finishRetiredExecuted; no model ACK on new key; issued1/executed1/recorded1/retired1.|
|5 shutdown_final_drain|QUEUED shutdown cancels; DONE shutdown retains actual raw, consumed once; Executing retireInvalidState unchanged, then complete/done terminal succeeds; duplicate finishEmpty.|
|6 stale_wrap|Age250000 claimOk, age250001 Expired/notexecuted, queued0xfffffff0→now0x10 age32Ok, age0x80000000 Expired.|
|7 duplicate_wake_key|Repeated state checks/claim afterDone do not execute/record twice; wrongkey claim/complete/finish reject and old raw/key survives; only correctkey can finish.|
|8 saturation|Direct real clockMailAdd:MAX-1→MAX true/unknownfalse; nextaddfalse/MAX/unknowntrue. ARM class size<=256 and standard-layout/trivially-copyable.|
|9 truthful_bad_result|Complete raw with output999/partialtrue/acceptedfalse/invalidduration and applied beyond offered upper; retain all fields; finishAckFault allowed, no reinterpretation into successful zero result.|
|10 sequential_lifetimes|Two complete issue/consume cycles, cancellation between, then retiredDONE; assert equations after EVERY operation and no old key replay.|

Use fixture-local `constexpr ClockMailRequest request(uint32_t stream,uint32_t output,uint32_t serial,uint32_t queued_us)` with decision requested5/lower0/upper10/updatetrue and zero fault context. Raw logical-result factories set the exact values in the table; they invoke no backend. Include `detail/AudioClockMailbox.h` from the actual source root. Compile these exact assertions (functions implement only the table scenarios using real mailbox operations):

```cpp
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
```

- [ ] **Step 2: Create the Root-only runner, then perform a real ARM red compile before helper implementation.** Exact CLI: `tools/check_clock_mailbox.py --source-root PATH --label LABEL [--prepare | --compile --guard PATH]`. Default prints a read-only plan; --prepare writes NEW label+UUID source/pins/receipt; --compile alone imports the pinned guard and invokes official ARM GCC7 `-std=gnu++14 -Os -mcpu=cortex-m7 -mthumb -mfloat-abi=softfp -mfpu=fpv5-d16 -Wall -Wextra -Werror -I <source-root/src> -c`, never links/runs the object. Use actual header includes, no reimplemented mailbox. Hash the fixture/header/control/backend-type dependencies before/after compile; a concurrent source change invalidates the attempt rather than becoming a pass. Compile fixture with private scheduler0 and1 (for1 add ARDUINO_GIGA/CORE_CM7), new per-attempt directory,600s owned-job timeout; record source/compiler/guard/object hashes,stdout/stderr/return/quiescence/errors. Missing header/API is the expected legitimate initial RED, not an observed result until Root records it. Do not execute native fixtures.

Root command from worktree (absolute Python/guard paths):

`& 'C:/Users/ngocv/AppData/Local/Programs/Python/Python312/python.exe' -B tools/check_clock_mailbox.py --source-root . --label mailbox-red --compile --guard 'E:/Arduino Giga R1 BREDR library/build/clock-selective-transition-m7-20261026/build_guard.py'`

Compiler SHA `0b007086d0b2e4a403440c09a907dd9f5da296a4163378d2cd2e85e35d8c13b5`; guard SHA `ca6303777db7187fb206450684b1ea99263f4ef964517732fc70176b50aa9942`. Failure/timeout remains failed, no auto retry/relaxed flags.

- [ ] **Step 3: Implement the header's fixed state transitions/counters only.** Reject without mutation before any state transition. claim is the sole execution-count frontier; complete is the sole recorded-result frontier; finish is the sole terminal/slot-clear frontier. Add no atomic/RTOS/hardware call or generic queue abstraction.
- [ ] **Step 4: Root runs `--label mailbox-green --compile` with the same pinned toolchain.** Required result: every static_assert compiles/return0/quiescent for both layouts; otherwise keep failure and fix source. Compile-only scalar/lifecycle assertions do NOT prove atomic publication, real scheduling, hardware timing or concurrency.
- [ ] **Step 5: Root reviews and commits this independently testable component.** Stage ONLY the new header/fixture/runner; suggested commit `feat: add bounded clock request lifecycle mailbox`. No SDK/native runner changes.

### Task 2: Integrate worker, host and lifecycle as one ownership change

**Files:** Modify `src/GigaBredr.cpp:151-203,283-350,377-444,470-590,678-696`; extend `tests/test_clock_mailbox.cpp` only if a discovered state sequence is absent. No other production leaf.

**Interfaces:** Add private Impl member `detail::AudioClockMailbox clock_mailbox; std::atomic<bool> clock_pending_hint{false};` and these exact helpers:

- `bool clockDrainMailbox(bool finalizing);` codec_mutex held; returns whether Empty after terminal handling/cancel-finalization.
- `void clockRetireMailbox(detail::ClockMailReason reason);` codec_mutex held; retires before any clock_control reset/output release, clears hint under lock, never waits.
- `void clockPublishControlSnapshot();` codec_mutex held; factor the existing clock_control.snapshot/publication tail without changing control math/stat mapping.
- `void serviceClockMailbox();` host thread only; one codec TryLock, NO radio lock; actual hardware executor.
- `void notifyClockHostAfterWorker();` RTOS worker only, outside codec lock; acquire-load hint and coalescent-set host_events1. Non-ARDUINO is a no-op.

- [ ] **Step 1: Split normal ACK reconciliation from actuation in clockFeedback.** Call clockDrainMailbox(false) FIRST, before eligibility/1000us gate. If a slot remains, return without observe: its existing pending decision must not be erased by observe/suspend. Otherwise retain original snapshot/queue observation/control code and call observe once when due. On decision.update, build immutable request from decision, now and existing current decode_errors/dropped/discarded/source.missing; offer before returning. Success stores hinttrue RELEASE under lock; do not signal host there. If mailbox counts.unknown is already set, fault and return BEFORE observe. An otherwise unexpected offer failure negatively acknowledges the exact current decision using existing fallback semantics and marks clock_local_backend_fault; no hardware, fabricated result, retry or overwrite. Offer rejection does not invent an issued mailbox entry; accounting becomes explicitly unqualified on protocol/saturation fault. Non-ARDUINO retains the old synchronous sequence.
- [ ] **Step 2: Implement serviceClockMailbox in this exact order.** Acquire hint first; one TryLock(codec_mutex), return immediately on failure. Confirm Queued; revalidate running/playing/configured/clockEnabled/eligible, request stream==stream_epoch, output==clock_output_epoch, current I2S initialized/running/rate/output, backend notfaulted/localfaultfalse, and captured decode/drop/discard/missing unchanged. Use existing read-only I2S snapshots OUTSIDE the old measured call. Do not take queue_mutex or recompute PI/packet accounting; eligibility is a bounded decision snapshot, not a claim of no newly arrived RTP error. Existing RTP/audio gates remain mandatory. If ineligible cancel; claim uses <=250000us age/half-range rule. On cancellation clear hint under lock, unlock then wake decoder. No schedulerPrepare/CW result for no-call cancellation.
- [ ] **Step 3: For successful claim only, execute original backend once and publish raw result.** schedulerPrepare immediately precedes `detail::i2sClockTrim(request.decision.output_epoch, request.decision.requested_q16, request.decision.lower_q16, request.decision.upper_q16)` using decision key. Keep backend/platform source BYTE-EXACT; no RTOS/helper/log within it. Call original clockWitness(counters.clock, decision, result) ONCE while this epoch still owns the mutex, then complete(...,true). Copy original raw timing/fraction/readback/status into counters; set applied_validfalse until worker reconciliation, publish coherent clock_published without changing PI state. Publish Done and clear hintfalse RELEASE while STILL locked. Only after unlocking call existing wake.release(); full semaphore already implies a pending wake, no spin/retry. No user callbacks.
- [ ] **Step 4: Put service after original radio poll scope and running check in hostMain.** Preserve `{ Lock radio_mutex; radioPoll(); }`, then service, then existing EventFlags wait1ms/fallback. Keep host priority/thread/stack creation unchanged. In decoderMain, after EVERY processOne returns, call notifyClockHostAfterWorker before loop iteration continues; no notification from inside processOne/clockFeedback. Add final codec-mutex-protected clockDrainMailbox(true) after decoder loop. This covers all early returns and existing UnlockDuringWait without new unlocks.
- [ ] **Step 5: Implement decoder terminal consumption.** For current nonretiredDone only (same controller identity; running/playing may already befalse during finalization), use the old epoch/accepted/partial/ACK/fallback/backendfault logic326–340, but do NOT call clockWitness again. One result is consumed once; original unsuccessful ACK attempt may use its existing negative fallback. Finish AckedResult on valid terminal ACK, AckFault on unreconciled ACK and set clock_local_backend_fault. Canceled/current pending gets negative ACK then CanceledBeforeExec; retired/mismatched controller gets no ACK, Done→RetiredExecuted or Canceled→CanceledBeforeExec. Preserve old rawresult, never apply or ACK again. Publish applied_ppm/control stats coherently only after reconciliation; applied_valid true only under original accepted/epoch/backend conditions. Unknown/saturation/protocol fault uses existing local-backend-fault path and admits no new work. Publish that existing fault and applied_validfalse before any early return, so foreground stats cannot hide the failure behind eligibility/pending gates.
- [ ] **Step 6: Retire before every reset/release, not after.** Invoke clockRetireMailbox before clockBegin/clockStop resets and before releaseCodec can i2sEnd/free resources. Empty is harmless; Queued becomes retiredCanceled; Done remains retainedDone/retired with raw/witness marker unchanged. Do NOT clear mailbox/counters in clockBegin/Stop. New observe waits for old terminal consumption. Executing cannot be observed by a lifecycle caller under the same nonrecursive ownership path: no backend/user callback invokes reset; treat any contrary source path as a blocking review failure, never teardown an in-flight output. No cross-thread wait under locks.
- [ ] **Step 7: Close end/partial-begin paths.** Preserve existing end runningfalse→hostjoin→decoderwake/join→releaseCodec. In-flight service finishes under mutex before hostjoin completes; decoder finalizer consumes currentDone normally or retiredDone without ACK, cancels Queued and leaves Empty/hintfalse. If worker never started, no decision could be issued; after joins foreground retirement may clear a nonexecuted placeholder only, never invent ACK/result. Verify no host/decoder self-end call or join under codec_mutex is introduced. Before Impl deletion assert the ownership invariant through the reviewed path; do not invent an unbounded drain loop.
- [ ] **Step 8: Root source/ARM checks before commit.** Run Task1 compile-only assertions again. Official ARM compile-c the ACTUAL GigaBredr.cpp using the exact existing stock4.6 CM7 recipe flags/includes (both scheduler0/1 typechecks where supported), pinned guard/600s; no native mocks executed. Record object/layout/private mailbox<=256B; obtain static frame/call-chain evidence for new host service because host stack remains8192 and full IRQ/high-water is unproved. Source review checks one active-M7 backend caller from serviceClockMailbox; witness-before-complete; clearhint-underlock; events-after-unlock; every reset retirement; no new radio/queue lock cycle; public API/log/default/priority unchanged; backend hashes EXACT. Read-only behavior models are not concurrency proof. Root commits ONLY cpp/any necessary fixture extension after review, suggested `refactor: execute owned clock requests on host lane`.

### Task 3: Freeze the actual candidate, full M7 audit and fresh linked review

**Files/artifacts:** Root creates NEW `build/dev10-clock-host-lane-candidate-20261026` and `build/dev10-clock-host-lane-m7-20261026`; no overwrite of ring/R2/f42/SDK stages. Source-audit metadata only; no additional production file changes.

**Interfaces:** Consumes Tasks1/2 exact source hashes. Produces new source projection, actual image/ELF/buildfacts/audit/companion receipts. Prior ring7d710/d582 and its linked proof are ancestry ONLY.

- [ ] **Step 1: Root freezes changed source with exact inverse/protected-parent checks.** Enumerate actual added paths/ordered rows; expect only one new library header row and NO new library translation unit (169 remains); do not silently reuse477/367 count once fixture/docs/header paths are added. Pin exact declared metadata/test additions separately, preserve all other parent leaves and frozenRoot469/stockSDK4/archivef730. Plan/spec hashes travel with the source. No library.properties/default/codecs/OwnedHost changes.
- [ ] **Step 2: Root prepares one r1 official full M7 build with4jobs/600s owned guard.** Retain all seven decoder defaults/all six ops/32K main stack/owned-host4/HCD/ABI/license/codec guards and scheduler R2 macro+both wrap flags. Failure remains failed; no native suites, tool bypass or automatic hardware retry. Actual image/facts stay UNKNOWN until terminal build+audit; no inherited pass/image.
- [ ] **Step 3: Fresh source/linked reviewer checks actual new ELF.** New host service call origin, copy/raw ownership, no scheduling call inside unchanged backend, old seven snapshots/all DWT/read sites/mask/restore/readback/full20us/CUM order, schedulerPrepare execution key, default host32/decoder24 unchanged. Re-prove redirects/real-once/TCB raw-only and actual static stack/calls for this ELF; no parent proof transfer. Internal mailbox/atomics are foreground-only, never in owned IRQ observer graph. Compiler/link success is not field/concurrency/WCET proof.
- [ ] **Step 4: Root commits only reviewed source/test/docs, retaining generated build evidence externally.** No public push/tag/main/develop/ZIP or stable claim follows this task. Builder/whole-source review must have no unresolved normal lifecycle/ACK blocker before the next task.

### Task 4: Root-owned unchanged NORMAL benchmark and truthful closure

**Files/artifacts:** Root prepares fresh harness/renderer copies for the ACTUAL new source geometry/image/ELF/companion and a NEW `build/codec-lhdc192-clock-host-lane-normal-20261026` case. Do not edit frozen V7/cases or assume logger current counts from this plan.

**Interfaces:** Consume actual Task3 receipts plus latest then-current CLOSED normal case/terminal control history. Produce sealed capture/source/GATT/control/closure/full+active44/CUM and private scheduler interpretation; no results are prefilled here.

- [ ] **Step 1: Root rebinds exact current logger/CIM/source/session/control history and public dev9f42/493052 restore, verifies new source/image/status consumers in ALL runtime helpers and renderer.** Fresh owner/runtime/nonces; old permanently uncertain R2 nonce retained; no failed-case promotion. Source/config witness/profile/UID/readback gates unchanged.
- [ ] **Step 2: Root performs original one-Link/source90natural/GATT60sequential512B/MTU517/d-a-d/all physical witnesses+deadlines/complete saved capture/exact saved-selector IDLE/publicf42 full readback/all resources released.** No controller/bond/global-phone/feature-policy changes, no native/security/wire workflows and no physical retry to green. Hardware action belongs to Root's existing user authorization and exact runtime admission, not to this plan file.
- [ ] **Step 3: Analyze ONLY sealed saved evidence.** Full/active44 maxima INCLUDING noncumulative clock samples, CUM over_n/over_total/unknown/coverage and whole20us remain mandatory. Retain controller refused/backendfault and executed-vs-decision distinctions; no synthetic zero result for canceled slots. Scheduler roles/counters remain boundary observations, not exclusive residence or preemption subtraction. CUM475.85625us priorFAIL remains historical; a new finite pass is not proof of cause/cure/stability/fidelity/physical200ms/E2E/allcodec support. Later named-codec/BLE/profile goal work remains.

## Implementation decisions and technical limits

- No new public API/log rows; private counters and the existing applied_valid/backendfault fields suffice for this prototype. This limits runtime visibility of cancellation detail; do not claim that unlogged lifecycle transitions were empirically exercised. Compile-only tests and source/linked review cover their contracts; field shows only actual observed paths.
- Host revalidates request identity, source lifetime/backend and cheap captured fault counters; it does not rescan full RTP history or move controller math/queue locking into the host. The finite decision sample may become older before execution; age<=250ms is not a no-new-RTP-error guarantee. Existing full RTP/audio gates cannot be waived. A requirement for stronger per-packet revalidation would be a distinct source choice, not an unstated extra lock/API.
- New host execution has no proved response/WCET bound, and foreground codec ownership may defer it. It can delay the next radio poll for the existing whole backend span; UART/DMA/timers/cache/bus can still expand it. No claim that moving thread context guarantees20us.
- Exactly one terminal outcome per issued decision, NOT an ACK after a controller has been destroyed. Counters are private per-begin; raw executed results are witnessed before reset, terminal slot survives reset until decoder consumes it. No third owner writes PI state.
- Root's independent plan review precedes implementation. This document chooses interfaces/order/error accounting; it grants no additional execution/publication capability. No production code has been written by the planner.
