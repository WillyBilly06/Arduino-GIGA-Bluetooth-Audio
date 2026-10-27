# Configured Clock Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Derive the running clock setpoint from the selected real-PCM reserve.

**Architecture:** Keep existing inventory and actuator ownership. Supply actual P-2H to the pure private controller; preserve legacy packet-startup learning.

**Tech Stack:** Arduino mbed GIGA4.6, C++14, official Cortex-M7 GCC, compile-time scalar checks.

**Spec:** docs/superpowers/specs/2026-10-26-configured-clock-target.md

## Global Constraints

No native Windows project-test execution. No hardware/IRQ/PLL timing change. Preserve all old failures and original44/full-active/20us/CUM criteria. No stable or physical latency claim.

## Review Focus

P includes two DMA halves; avoid double counting. Manual thin geometry must not silently learn an unintended target. Recovery must retain the configured target. Legacy callers must retain learning. Compiler-only models do not prove scheduling or audio quality.

### Task 1: Evaluate actual pure controller arithmetic at compile time

Files: src/detail/AudioClockControl.h; tests/test_configured_clock_target.cpp.

- [ ] Write legacy mean/ACK/direction/recovery compile-time checks; observe the existing non-constexpr boundary.
- [ ] Mark only pure methods constexpr; compile the actual production header with official ARM GCC; preserve legacy assertions.
- [ ] Commit the testability refactor after verification.

### Task 2: Select configured target and connect actual output geometry

Files: src/detail/AudioClockControl.h; src/GigaBredr.cpp; src/AudioClock.h; docs/audio-buffering.md; tests/test_configured_clock_target.cpp.

- [ ] Add a test adapter which passes T to the new seventh argument when present and otherwise uses the legacy call; verify the unchanged controller fails the configured target behavior assertion.
- [ ] Implement UINT32_MAX legacy selection, positive configured target validation and target retention across relearning.
- [ ] Read actual initialized P/H/R in serialized clockBegin; pass T for real-PCM mode and preserve legacy packet mode. Update eligibility documentation.
- [ ] Verify controller assertions and actual frontend/backend ARM objects, then obtain an independent whole-change review. Commit only verified source; full M7/live qualification remains required.
