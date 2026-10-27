# First-starvation supply witness implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Root alone runs compilers, commits and hardware actions.

**Goal:** Locate the missing audio-supply stage at the first starvation without changing playout behavior.

**Architecture:** Short versioned32-bit publications before locks and after queue mutations; bounded first3 IRQ snapshots; foreground3-row rendering. Same failed buffer policy and all original gates remain.

**Tech Stack:** C++14/STM32H747/Arduino mbed_giga4.6.0, existing private scheduler flag, official ARM compile-only tests and fresh full M7/link review.

**Spec:** `docs/superpowers/specs/2026-10-26-first-starvation-supply-witness.md`

## Global constraints

- No priorities, IRQ masks, cache, DMA, clocks, codecs, reserve sizes or controller commands change.
- No IRQ printf/heap/locks/spin/FP/TCB dereference; read typed raw current/next addresses only.
- First3 records,24aligned32-bit words each; combined observerBSS<=1024B including existing552B.
- Odd/changed publication, epoch/saturation/timer ambiguity remains UNKNOWN. No replay or packet-gap inference from absent callback entry.
- Root native-test/AV225 stop remains; only approved official ARM compilation and later ordinary owned traffic.

### Task1: Private witness core and contracts

Create `src/platform/AudioSupplyWitness.h/.cpp`, `tests/test_audio_supply_witness.cpp`, `tools/check_audio_supply_witness.py`.

- [ ] Draft header-facing compile-only tests and runner; STOP before implementing the missing production header until Root observes real missing-header RED.
- [ ] Define explicit decoder phases, same-epoch queue consistency/flags and fixed24-word snapshot. Implement short lock-free publications and bounded records; default-disabled stubs.
- [ ] Root actual ARM compile assertions/source objects and independent code review. No concurrency/target-proof claim from scalar tests.

### Task2: Observation hooks and fresh build

- [ ] Publish receive entry before queue lock; accepted/taken counts only after actual queue mutations; decoder phases before waits/locks and around work. Never hold an odd version across blocking operations.
- [ ] First-transition IRQ hook reads only private coherent publications and typed raw RTOS pointers, not PacketQueue or64-bit counters. Epoch/config reset is generation-safe.
- [ ] Foreground outputs one S0/S1/S2 row per turn; old scheduler rows/counters keep their format.
- [ ] Root full M7 and fresh linked/IRQ graph/state-size review; explicit probe effects remain unproved until measured. Commit exact reviewed source.

### Task3: One fresh ordinary diagnostic case

- [ ] Fresh identity/normal history30/binary readback/source90/GATT60/physical transitions/saved closure, original44/full-active/20usCUM unchanged.
- [ ] Interpret the FIRST failure snapshot against accepted/dequeued/depth, receive-entry and decoder phase. Keep UNKNOWN and upstream ambiguity; no forced starvation or retry-to-green.
- [ ] Choose a single evidenced correction only after stage attribution. If no event occurs, report witness unexercised and continue other justified goal work without claiming the cause fixed.
