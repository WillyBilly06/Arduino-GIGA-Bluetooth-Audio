# Configuration-driven audio reserve implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Root alone executes official tools, commits and device actions in this session.

**Goal:** Fill a configuration-sized real PCM reserve before fixed-rate playout, with bounded codec-local incoming storage and explicit manual overrides.

**Architecture:** Pure integer sizing selects capacities and the real-frame watermark. The existing PCM ring holds decoded audio until prime readiness; the same selected threshold governs recovery. DMA geometry and clock hardware policy stay unchanged.

**Tech Stack:** Arduino mbed_giga 4.6.0, Cortex-M7, C++14, official ARM GCC7 compile-only assertions; no native fixture execution.

**Spec:** `docs/superpowers/specs/2026-10-26-configured-audio-reserve.md`

## Global constraints

- Preserve every compiled codec, explicit manual overrides, lazy selected-config allocation, configurable pins/MCLK and shared BLE/A2DP ownership.
- Preserve approximately 1 ms/four-frame-aligned DMA halves, cache/IRQ/masks/PLL and original clock/transport/audio gates. No SDRAM/phone/controller action belongs to code implementation.
- Automatic targets include the selected decoder block duration; real stereo S32 frames use 8 bytes even for expanded mono input.
- No queue-full startup escape; fragments can decode before playback, but DMA needs real P frames. Require 2H <= P <= R.
- 200 ms total phone-to-DAC remains unmeasured. Unadvertised long HE modes may exceed it; do not drop the decoder or claim compliance.

## Review focus

- Aggregated/fragmented packets and small SBC blocks cannot bypass PCM readiness.
- HE/Opus long blocks, mono expansion and sample-rate switches must size scratch/reserve in output-frame units.
- Explicit ring/threshold contradictions fail before allocation; queue-byte target never silently changes an explicit slot count.
- Begin/end/reconfigure or output failure during backpressure invalidates saved views exactly as before.
- Empty decoder turns stop the burst; every return still wakes the clock host and final shutdown drain remains serialized.

### Task 1: Pure sizing and prime-threshold contracts

**Files:** create `src/detail/BufferSizing.h`, `tests/test_buffer_sizing.cpp`, `tools/check_buffer_sizing.py`; modify `src/platform/I2sBuffer.h` and `src/platform/PcmRecovery.h`.

**Interfaces:** `detail::BufferDefaults defaultBufferSizing(Codec,uint32_t rate,uint32_t decoded_block_frames,size_t maximum_packet=2048)`; `detail::i2sPrimeFrames(uint32_t capacity,uint32_t half,uint32_t requested=0)` returns 0 for invalid geometry. Existing prime/recovery calls with requested=0 retain legacy behavior.

- [ ] Write actual-header compile-only assertions first; Root records missing-header RED with both private scheduler layouts.
- [ ] Implement integer sizing, queue-byte accounting, 2..320-ms PCM geometry, explicit real-frame threshold and 12-kHz I2S support for the already accepted Opus rate.
- [ ] Run actual ARM constexpr checks for every codec/rate/block class, thresholds, stride cap, invalid geometry and old legacy behavior. No linked/native/target fixture.
- [ ] Independent source review; commit this coherent slice after actual checks pass.

### Task 2: Integrate automatic/manual policy and paced startup

**Files:** `src/GigaBredr.h`, `src/GigaBredr.cpp`, `src/Types.h`, `src/platform/GigaI2s.h`, `src/platform/GigaI2s.cpp`, `examples/Receiver/Receiver.ino`, README/buffering documentation.

- [ ] Add zero-to-auto global defaults and startup_buffer_ms; preserve per-codec/nonzero-global precedence and explicit startup_packets legacy mode.
- [ ] Resolve the negotiated configuration before allocating; set one selected real PCM P, ring R, one scratch block and bounded incoming queue.
- [ ] Pass P to producer prime and recovery; publish configured and actual initial queued/DMA frames. Preserve all old hardware failure and ownership checks.
- [ ] Return work/idle from processOne, break the 64-turn burst on idle after unconditional host notification, preserve work for dequeued fragments/errors and all epoch/backpressure checks.
- [ ] Use automatic policy in Receiver and report actual resolved geometry; document memory formulas, manual overrides, headphone load and measured limits.
- [ ] Root ARM frontend/full M7 checks and independent integration review; commit only reviewed sources.

### Task 3: Validate actual memory and normal coexistence

- [ ] Freeze only tracked source, compile all decoder defaults with official M7 tools and audit new artifacts. No parent image/result transfer.
- [ ] Review actual new linked startup/recovery/clock/caller/layout paths; preserve all original timing gates.
- [ ] Run fresh owned codec/BLE windows with real prefill/low-water, queue/decode/memory measurements, source natural completion, physical transitions, full readback/restore and resource closure.
- [ ] Report failures honestly; neither a finite zero-counter window nor buffer capacity proves acoustics, numerical fidelity, all configurations or total 200-ms latency.
