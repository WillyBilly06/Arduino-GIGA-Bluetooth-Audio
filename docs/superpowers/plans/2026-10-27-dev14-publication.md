# Dev.14 Source Integration and Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare experimental dev.14 from the reviewed AAC allocation and optional voice-control inputs, with one matched CM7 build and versioned Arduino ZIP.

**Architecture:** Copy only the separately approved deltas onto the dev.13 parent. Freeze source/Receiver/library metadata before compilation, then add actual build facts through documentation-only edits. A local Git commit and per-member ZIP hashes bind the result.

**Tech Stack:** Arduino Mbed GIGA 4.6.0, official Cortex-M7 GCC/Arduino CLI, Python, Git and ZIP.

**Spec:** [Dev.14 validation and limits](../../validation/dev14-2026-10-27.md).

## Global Constraints

- Parent dev.13 is 950d8ded744d9de40e92ab7c0ac59bd573805166; preserve main and remote refs during preparation.
- Integrate only the frozen 15-file voice-control stage and eight-file AAC patch; preserve all component notices and unrelated source.
- Keep CM7/softfp/FPv5-D16, all seven codec defaults, project host, stock HCD, existing operation-mode examples and I2S/MCLK options.
- No private recorder/SDK heap logger, hardware/phone action, fabricated call support or audio-stutter-fix claim.

## Review Focus

- A stale component document must not describe an old component binary as the combined package build.
- Partial portable evidence must not become an MCU heap/PCM, acoustic or phone-interoperability claim.
- The AAC corrected initializer frame must not be confused with the rejected large-stack candidate.
- Voice control must remain optional/unadvertised without a working SCO/voice/microphone path.
- Later documentation updates must not alter the compiled source/Receiver/library metadata or archive payload silently.

### Task 1: Integrate only reviewed bytes

**Files:** Approved codec, voice-control, test, tool and component-document deltas; source provenance manifests.

- [x] Verify input archives/manifests, independent AAC review and Root's actual portable CPU receipts.
- [x] Create a separate branch/worktree; retain baseline and approved-file hashes outside the package.
- [x] Copy only approved deltas and verify all unrelated source/notices remain exact.

### Task 2: Freeze and build the combined library

**Files:** library.properties, README/CHANGELOG/STATUS/EXPORT, this plan, validation records.

- [x] Set version 0.1.0-dev.14 and accurate experimental/voice/audio limitations.
- [x] Freeze compiled source, Receiver and library metadata separately from later documentation.
- [x] Run one guarded official CM7/jobs4 Receiver build; persist actual terminal CLI status, stdout/stderr and zero-descendant result.
- [x] Audit all library TUs, all decoder Ops/defaults, stock HCD, architecture, compiled source and component geometry/ownership limits.

### Task 3: Commit and package for review

**Files:** Final validation/BUILD records and versioned ZIP/checksum sidecar.

- [x] Add actual build facts through documentation only and recheck compiler-input hashes.
- [ ] Create one clean local integration commit, preserving the approved source and all component license bytes.
- [ ] Archive that commit; verify every ZIP and extracted member against the final manifest and compiled projection.
- [ ] Return artifacts to Root for final review, with no push, tag or deployment.
