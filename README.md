# GigaBredr 0.1.0-dev.9 (experimental)

Bluetooth Classic A2DP music receiver and public BLE GATT server for the Arduino GIGA R1 Cortex-M7, with a project-owned host stack, the supplied codec decoders, and PCM5102A-compatible I2S playback.

**Clock-deadline failures remain. This version is experimental and not stable. Total phone-to-DAC latency below 200 ms, full numerical fidelity and acoustic continuity are unproved.** See [validation and known limits](#validation-and-known-limits) before relying on continuous audio.

## Install and start

1. Download the [v0.1.0-dev.9 source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/tags/v0.1.0-dev.9.zip).
2. In Arduino IDE, use **Sketch > Include Library > Add .ZIP Library** and select the ZIP. Keep one active GigaBredr installation.
3. Install **Arduino Mbed OS GIGA Boards 4.6.0**, then select **Arduino GIGA R1 / Cortex-M7**.
4. Open **File > Examples > GigaBredr > Receiver**, wire the output below, and upload the sketch. The example contains the codec-specific buffer settings used in the finite traffic checks.

For a local source install, place the folder containing `library.properties`, `src` and `examples` at `<sketchbook>/libraries/GigaBredr`. To compile the Receiver example with Arduino CLI:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

The defaults are `GIGA_BREDR_USE_BTSTACK=0` (project-owned host), `GIGA_BREDR_UART_BAUD=2000000`, and `GIGA_BREDR_I2S_WS_PIN=74`. These are the same values passed explicitly in the verified build; exact build inputs and artifact identities are in [BUILD.json](docs/BUILD.json).

| GIGA pin | PCM5102A signal |
|---|---|
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

Use the breakout's required supply and 3.3V logic. Output uses Philips I2S, stereo 32-bit slots with samples left-aligned; slot width is not a numerical precision measurement. **MCLK defaults to `-1` (disabled)**; PCM5102A can use its BCLK PLL. Set `ReceiverConfig.i2s` before `begin()` for a compatible route. Alternate pads and optional 256Fs MCLK remain electrically unqualified. See [pin routing](docs/runtime-i2s-pins.md).

Call `receiver.poll()` from `loop()` to deliver bounded diagnostic/configuration/volume/metadata callbacks; radio and decoder workers run independently. Check `begin()` and status results, keep callbacks bounded, and serialize `begin/end/poll`. `availableCodecs()` reports compiled decoders; `ReceiverConfig.codecs` limits negotiation. `end()` releases receiver-owned resources.

## Codecs, memory and unfinished features

All seven decoder defaults remain enabled: SBC/SBC-XQ, AAC-LC, experimental HE-AACv1, aptX/aptX HD, LDAC, LHDC V5 and Opus. One codec is negotiated at a time. Inclusion and M7 linkage do not make a phone offer it, prove a decoded reference, or qualify every configuration.

The six ECA4 phone-family windows exercised SBC44.1, AAC-LC44.1, aptX48, aptX HD48, LDAC96 and LHDC192. A separate SBC-XQ sender selection, Opus/HE-AAC phone operation and an Apple AAC sender remain unproved. HE-AACv2/parametric stereo, xHE-AAC and BLE Audio are not implemented. Android AAC-LC evidence is not Apple interoperability proof.

Immutable tables stay in flash. The selected decoder workspace, packet/access-unit queues and PCM storage allocate for its configuration and release on stop/reconfiguration; inactive codec overrides are data only. `setExternalAllocator()` provides an allocation hook; the application must initialize/supply external memory. This does not prove SDRAM placement, heap fragmentation, allocator overhead or stack high-water. Historical AAC-LC stereo selected payload saving was 1222 bytes (25262 vs26484), not system free-memory or CPU improvement.

Receiver buffer profiles (queue/start packets): LHDC48/26, LDAC40/16, SBC12/4, AAC/HE16/6, aptX24/10, aptX HD32/15, Opus16/7. LHDC/AAC/HE/aptX/HD/Opus select a 2048-byte packet limit; default access-unit capacity is 8192 and PCM buffer 20 ms. Zero overrides inherit global settings. These reserves are not end-to-end latency bounds. At 44.1 kHz, six 1024-frame AAC-LC AUs represent about 139 ms, while six 2048-frame HE-AAC output AUs represent about 279 ms, **conditionally assuming one AU per queued packet**. No profile/buffer is changed here to hide that risk.

AVRCP volume and bounded metadata/status handling are present, including callbacks. Full typed transport controls and complete AVRCP coverage remain unfinished. Full HFP/HSP call/profile operation, RFCOMM/SCO integration and a public pluggable microphone-input API remain unfinished. PCM5102A is playback output; no microphone/call runtime capability is claimed.

## BLE coexistence and diagnostics

BLE is a public **unencrypted peripheral/GATT server**. ATT MTU begins at 23 and may negotiate 517; value size is at most `min(512, negotiated_mtu-3)`. `bleReadWrite()` returns copied writes; `bleNotify()` admission is distinct from remote receipt. The Receiver example echoes one pending copied write and retains it on `Busy/OutputFull`. Generation-bound replies reject stale origins. Pairing/encrypted BLE and central/client roles remain unsupported.

The base audio preference is 80-120 ms; **Receiver requests 120 ms** (`96 x 1.25 ms`), latency 2 and 5 s supervision timeout. Existing connection-age eligibility and peer-reply cooldown remain 1 s. During secure Classic audio, host policy prioritizes Classic receive work and paces ordinary ATT data with 27-byte fragments/10 ms spacing. Valid in-band peer tuples are echoed exactly; out-of-band intervals select standard negative 2021/reason 3B. A central may retry, override or disconnect. Requested/submitted/controller-observed parameters remain distinct; no RF airtime reservation is implied.

`bleDisconnect(generation,false)` asynchronously holds advertising after the matching peer departs; `bleResumeAdvertising()` releases the hold through normal GAP processing. `Ok` is admission, not physical departure, advertising delivery or reconnection proof. Discard saved generations after `end/begin`. Serial `d/a` example controls are diagnostic lifecycle requests, not a qualification shortcut.

Clock SUM/WORST/RESTORE records retain cumulative overruns and bounded phase context. Inclusive spans can include instrumentation, interrupts and released work; they do not identify a preemptor or prove timing neutrality. **Every original 20 us clock bound and all 44 full/active fault gates remain mandatory.** A final last-result zero cannot erase a prior maximum or a nonzero cumulative count. The 4096-byte foreground console ring and bounded mailbox can drop records; missing/unknown/saturated data are not zeros or an all-results upper bound.

## Validation and known limits

The verified 475-file source ZIP was extracted and compiled with the official ARM toolchain for GIGA Cortex-M7/core 4.6.0: all 168 library translation units, six decoder-operation families and seven decoder defaults were included. The stock controller HCD patch was retained. The 493052-byte binary is byte-identical to the field-tested `f42f467f...` image; this establishes artifact parity, not another hardware test or a blanket test-suite pass.

That image's normal LHDC192 window completed a 90.066154810 s natural source, 43 verified 512-byte GATT transactions over 61.118 s at MTU517, and three physical BLE controls. All 43 non-clock gates were zero, including media/RTP/decoder/PCM/diagnostic/console faults. **The clock gate failed: four cumulative overruns in 91 backend results**, with no unknown clock results. The largest retained wait1 was 528.4354 configured microseconds. A finite traffic check does not prove that a clipped sample reached the corrected S32 conversion, bit-exact 24-bit fidelity, a pop cure, a timing improvement or stability.

Earlier finite phone-family windows on the `eca4bdd5...` image remain recorded:

| ECA4 family | Natural source(s) | Verified512B GATT | Active(s), MTU517 | Cumulative clock overruns |
|---|---:|---:|---:|---:|
| LHDC192 |90.062811320|48|61.149|9|
| SBC44.1 |90.096202934|78|60.737|1|
| AAC-LC44.1 |90.075535017|82|60.269|1|
| aptX48 |90.078970330|84|60.351|0|
| aptX HD48 |90.070886060|70|61.055|4|
| LDAC96 |90.060856008|60|60.607|5|

All 43 non-clock gates were zero in those windows. SBC's sampled 44 zeros do not erase its cumulative overrun; aptX's finite zero window is not continuous stability. Earlier failed windows, including the AAC logger-receipt attempt before GATT, remain failed and are not rescored by later runs.

The S32 correction defines finite saturation before conversion; it does not establish independent full 24-bit LHDC PCM fidelity, a pop cause, electrical I2S accuracy, acoustic quality or a clock-overrun cure. LDAC phone 32-bit input is separate from receiver 24-bit configuration; neither proves decoded numerical precision. Total phone-to-DAC 200 ms latency remains **unknown for every codec**. Queue, residence and local AudioTrack metrics are not an aligned end-to-end measurement.

See [dated validation](docs/validation/dev9-2026-10-26.md), [status](docs/STATUS.md), [BUILD.json](docs/BUILD.json) and [export provenance](docs/EXPORT.md) for exact artifacts, preserved audit history and evidence limits.

Original Apache-2.0 project terms and every supplied component notice/license are preserved. [THIRD_PARTY.md](THIRD_PARTY.md) remains authoritative for component terms and existing unresolved grants; this update creates no new grant or relicense.
