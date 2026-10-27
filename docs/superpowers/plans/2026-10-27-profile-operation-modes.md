# Profile Operation Modes Implementation Plan

> **For agentic workers:** Use the existing inline worker; Root alone executes tests/Git/build/device actions. No child agents.

**Goal:** Three honest Arduino examples: A2DP-only, real BLE-only and A2DP+BLE, with bounded generation-bound echo.

**Architecture:** Append ReceiverConfig.enable_a2dp=true for backward-compatible defaults. Append an internal RadioCallbacks flag instead of new radioBegin overloads; use one pure OperationModePolicy in frontend/OwnedHost so BLE-only suppresses Classic backend/UUIDs/reservation/request admission/inquiry/page and audio worker/I2S allocations. Retain all default audio/codec/clock/IRQ/host sequencing.

**Tech Stack:** C++14, current owned GIGA host, official Root ARM compile-only validation; no native executables here.

**Spec:** Root's explicit user request, isolated worktree codex/profile-operation-modes at47e253d; no radio-rx-clock worktree/service1801 edits.

## Constraints and review focus

- Root must first observe test_operation_modes.cpp RED on old API; no production edits before that receipt.
- Defaults remain A2DP+BLE/I2S; reject both disabled before allocation. BLE-only ignores unused audio configuration, has no decoder thread and cannot configure an audio codec/output.
- BLE-only scan bits must remain0 even if reservation state is stale; reject Classic Connection Request and unexpected successful Classic Connection Complete rather than accepting/provisioning peer0.
- A2DP-only keeps existing Classic behavior and does not advertise BLE. Default audio worker starts before host as before; BLE-only host-only start/end handles null decoder safely, including SCHED1 role registration.
- No UART/DMA/IRQ/controller/kernel/priority/credit/database policy edits; no new thread. Existing shared radio transport storage is still necessary for BLE; no claim of zero radio allocation.
- Pure tests exercise actual policy once present; source review must confirm actual host consumers use it. Controller/page-scan/advertising/RF behavior remains runtime-pending until Root's later evidence.

## Steps

1. Root compiles tests/test_operation_modes.cpp against current source, both private layouts as appropriate. Expected RED is missing BLE-only public selection (SFINAE fallback makes selection assertions fail), not a host-native executable.
2. Add src/detail/OperationModePolicy.h with constexpr valid/needsDecoder/classicScan/admitClassic/classicUuidCount and exactly the truth table in the fixture. Append enable_a2dp to public ReceiverConfig and internal RadioCallbacks, defaulttrue.
3. Frontend validates selected profiles; only audio mode validates codec/buffer/I2S/decoder-stack inputs, normalizes unused I2S off and creates decoder thread for audio. Guard configure/stream against disabled A2DP; preserve existing audio start/end paths otherwise.
4. OwnedRadio copies the flag; OwnedHost conditionally begins Classic, publishes no Classic UUIDs/class when disabled, does not reserve peer0, gates Classic request admission, rejects unsolicited Classic success and keeps scan0. BLE profile/advertising and shared RX/credit timing stay unchanged. No core database edits.
5. Add examples/A2dpOnly, BleOnly, A2dpBleCoexist and shared bounded BLE reply helper. PCM5102A: D70 BCLK/D72 DIN/D74 LRCK/MCLK-1. Compatible alternate GPIOs must map to the same hardware I2S peripheral. No blocking loop/diagnostic callback; bleNotify(origin) Busy/OutputFull retains exactly one copied origin for later loop retry; stale/error clears it; do not dequeue over an outstanding reply.
6. Root runs GREEN pure fixture and actual ARM compile of changed frontend/OwnedRadio/OwnedHost and all3 examples; reviews source Classic gates/worker lifetimes. Root alone owns commits and any future hardware. Report compile/source evidence separately from controller/RF/audio qualification.

Production ownership is limited to mode API/wiring/pure policy/new examples and their docs/tests. The separate1801 service/underrun investigation remains untouched.
