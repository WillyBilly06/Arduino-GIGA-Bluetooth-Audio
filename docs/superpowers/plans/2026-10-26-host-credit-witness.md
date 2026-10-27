# First-empty Host credit witness implementation plan

> **For agentic workers:** Use superpowers:executing-plans. Root owns all execution and hardware; reviewers use filesystem text only.

**Goal:** Capture whether the owned host is retaining copied input when PCM first becomes empty, without changing audio or scheduling policy.

**Architecture:** Add a private 16-byte generation-bound atomic cell to the existing observer. Host actor entries invalidate before mutation and publish after their complete operation. Callback boundaries publish explicitly incomplete snapshots and invalidate again before returning to mutable work. Main may invalidate but never publish. IRQ copies five atomic words once; foreground emits distinct H0/H1/H2 rows.

**Tech Stack:** Arduino GIGA CM7, official GCC7 ARM, existing private SCHED witness, C++14 constexpr assertions.

**Spec:** `build/dev10-host-credit-witness-design-20261026/{report.md,design.json,hook-map.json}` in the parent workspace.

## Global constraints

- Preserve real-PCM prefill, DMA pacing, recovery, queues, host ordering, HNC exemption and all clock/44/CUM/20us gates.
- Exactly 16 additional persistent bytes; Supply472 + Scheduler552 <=1024. No added UART/DMA masked work, clocks, locks, retries, allocations or callback pointers.
- Stage1 transport fields are UNKNOWN. Optional history bit stays reserved zero; zero never means historical absence.
- Generation and version never wrap into valid old data; Main invalidation is idempotent. Host validity cannot erase original phase/queue validity.
- Existing S0/S1/S2 evidence and parsers stay immutable. New records use H0/H1/H2.
- No native project EXEs. All actual compilation/hardware remains Root-owned. Installed cf713 dev10 stays baseline until a separately verified diagnostic.

## Review focus

- Repeated Main invalidation must stay odd until Host commits.
- Raw packets with reused handles remain globally unassigned, never attributed to current Classic owner.
- Failed/partial/retired lease state becomes unknown; no wrapped zero counters.
- Profile/configuration callbacks can change the Supply generation mid actor call; old publications cannot cross epochs.
- New unknown Host bits must not change a valid existing SupplyPoint.

## Tasks

### 1. Scalar protocol and publication contract

- [x] Add `tests/test_host_credit_witness.cpp` assertions; run official ARM and retain expected RED.
- [x] Add pure `src/platform/HostCreditWitness.h` packing, lease, version and generation helpers; retain GREEN.
- [x] Review the scalar protocol, sentinels and exact 16-byte cell independently.

### 2. Runtime integration

- [x] Add Host cell/publication functions and bounded IRQ copy; replace three unused timing fields in the new H protocol.
- [x] Add bounded owner-validated Host extraction, public actor wrappers, callback incomplete boundaries and Main invalidation. Preserve original actor bodies/ordering.
- [x] Compile actual changed ARM objects for SCHED0/1, inspect state and callback coverage, and obtain a fresh source review.

### 3. Linked and field qualification

- [x] Full private M7 build and fresh bounded linked review: actual ELF/DWARF472+544+4+4=1024, no observer in actual SCHED0 objects, five single-pass loads and pure new IRQ helper. This is not a full SCHED0 link or whole-IRQ/WCET bound.
- [x] Add saved-only H protocol parser; reject malformed/mixed/foreign artifacts, transport/history unknown.
- [ ] Only then prepare a normal90s/60s GATT512/MTU517/3 physical transitions case using the current cf713 dev10 restoration authority. Preserve every failure and all gates.

Completion of one task is not audio stability or total-goal completion.

## Recorded evidence

Root's `build/dev11-host-credit-m7-20261026` build compiled509 source files/374 selected compiler inputs/170 library translation units. BIN30e425e75009397ace5bdd3377f383aeaf79180a26d93d9829ff1a96d1583363 is505860 bytes; ELFdbbaec9dba4c0507cb74172dcc7804a685bee513095e15bc7776d987c13f07c3. All six operation tables, seven enabled codec defaults, owned-host selection and stock HCD were checked. Compiler projection is a4fbfa1b557439f1f8b5ab91bb42ff1cb2787de70ee2ee797c1b43d36b9368a4.

`build/dev11-host-credit-witness-checks-20261026/final-3bef89bc88a94567ac2ebd166fb7eb7d/receipt.json` records71 Host/queue/phase scalar assertions in both ARM modes and16 synthetic parser tests. The prior saved parser fails three newly added cases; the corrected parser passes. Parser and its tests were fixed after the frozen firmware build; this progress update is also documentation-only. None of those three post-build changes alters the374 compiler inputs.

Independent source, runtime, parser and bounded linked reviews are under the corresponding `build/dev11-host-credit-*-review-20261026` folders in the parent workspace. The first-empty function's own frame is440 bytes; whole-IRQ stack, WCET and observer effects remain unknown. No diagnostic firmware upload, live audio test or stability qualification has occurred. The previously deployed published dev10 remains the restoration baseline.
