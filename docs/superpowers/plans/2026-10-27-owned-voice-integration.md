# Owned voice integration implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement each stage; Root independently executes the Android CPU fixtures and reviews the source before field testing.

**Goal:** Connect the owned HF/HS control path and actual synchronous audio, then expose microphone registration only where it is consumed.

**Architecture:** Serialized OwnedHost owns a caller-reserved optional VoiceProfiles module and its real PSM3 lifetime. A separate synchronous owner and voice worker will bind actual H4 SCO transport, codecs and PCM/output lifetime.

**Tech Stack:** Existing portable C11/C++17 core, official Android NDK ARM64 fixtures, official GIGA4.6.0 CM7 compiler, no BTstack runtime.

**Spec:** ../specs/2026-10-27-owned-voice-integration-design.md

## Global constraints

- Preserve all A2DP codec defaults, I2S, HCD, BLE priority and existing host queue/credit ownership.
- No ADB, USB, serial, flash, phone calls or Windows project EXE execution by this lane.
- No public voice advertisement before a real capable backend; no inert public microphone facade.
- Preserve every failed/partial test receipt. Default voice remains disabled.

## Review focus

- Queue pressure must not partially mutate protocol state, reissue AT rows, or strand Classic media behind replies.
- Late AT/RFCOMM replies cannot revive a new session; channel and DLC generations are distinct.
- Old-CID committed/partial output must drain before channel reuse; no live packet-credit refund.
- Received data cannot exceed the actual per-DLC reserved window; credit returns only after consumption.
- A voice route/format claim must come from the exact controller/backend, not a header or SDP flag.

### Stage1: Actual host control owner

- [x] Freeze dev13 baseline and prepare actual actor PSM3 absence fixture; Root observed expected assertion134.
- [x] Write real protocol tests for two listener channels, durable queue pressure, segmented AT SLC, receive-credit return, stale owner/timeout, default host refusal and enabled host admission.
- [x] Extend bth_rfcomm with bounded second listener registration.
- [x] Add VoiceProfiles.{h,cpp}: one mux, HF/HS controls, two reserved RX frames per DLC and durable RFCOMM output, one bounded poll step.
- [x] Connect optional module to OwnedHost attach/channel/receive/poll/teardown and security gates; preserve existing media/HNC ordering.
- [x] Build frozen Android ARM64 fixtures; Root executed actual GREEN736/339/861 plus retirement after the dedicated local-close RED. Final guardedCM7 and source/codec/HCD audit passed.
- [ ] Hand off stage1 with explicit unadvertised/no-SCO/no-public-mic limitations; full voice objective remains open.

### Stage2: Synchronous link and codec/PCM path

- [ ] Write owned synchronous request/completion/handle/status/credit/deadline tests against literal HCI layouts and retained failures.
- [ ] Add actual H4 SCO dispatcher and bounded synchronous RX/TX owner, linked to the admitted encrypted ACL peer and a fresh audio generation.
- [ ] Implement controller-decoded CVSD PCM handling only after exact route/voice-setting evidence. Implement verified mSBC/H2 RX and TX or retain wideband unsupported.
- [ ] Keep TX credit/deadline policy conditional on actual controller readback; no guessed routing VSC.

### Stage3: Voice worker and public lifetime

- [ ] Connect real voice PCM queues to an output worker with explicit A2DP arbitration and fresh output epochs.
- [ ] Add public None/Provider microphone registration consumed by that worker; serialized callback detach/epoch tests and missing-input counters.
- [ ] Register honest HF/HS SDP/features only after backend readiness is established; retain default-off gates until Root's target/phone validation.
