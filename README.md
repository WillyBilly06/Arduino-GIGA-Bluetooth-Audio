# GigaBredr dev.13 develop (experimental)

Bluetooth Classic A2DP receiver and public BLE GATT server for Arduino GIGA R1 Cortex-M7, with all supplied codec decoders and PCM5102A-compatible I2S playback. The project owns the host stack; the radio controller uses Infineon ROM plus the stock HCD patch, and scheduling uses Arduino Mbed/RTOS.

**Experimental; not stable.** Dev.13 adds typed AVRCP Play/Pause/Stop/Next/Previous taps, copied metadata/playback-status snapshots and scoped channel retirement. Standalone RFCOMM/HFP/HSP control modules and an internal microphone-provider contract are included as groundwork; HFP/HSP remain unadvertised and have no integrated SCO or duplex call audio. The final reviewed source passed 28 portable Android ARM64 CPU cases and an official CM7 Receiver build. Those checks do not establish target/phone interoperability. See [dev.13 validation](docs/validation/dev13-2026-10-27.md).

The existing LHDC automatic reserve remains 80 ms with a 90 ms ring. Earlier finite BLE/A2DP windows had zero recorded audio faults, but longer listening subsequently produced brief PCM shortages. The root cause and a complete continuity fix remain unresolved. No all-codec stability, numerical-fidelity, acoustic or 200 ms total phone-to-DAC latency claim is made.

## Install and start

1. Download the versioned [v0.1.0-dev.13 source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/tags/v0.1.0-dev.13.zip), or use the [develop source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/heads/develop.zip). Develop changes over time; retain the commit identifier when reporting results. Both are experimental development sources.
2. In Arduino IDE choose **Sketch > Include Library > Add .ZIP Library**. Keep one active GigaBredr installation.
3. Install **Arduino Mbed OS GIGA Boards 4.6.0** and select **Arduino GIGA R1 / Cortex-M7**.
4. Open **File > Examples > GigaBredr > Receiver**, wire the output below, and upload. The example uses automatic buffers sized for the negotiated configuration.

For local installation, place the folder containing `library.properties`, `src` and `examples` at `<sketchbook>/libraries/GigaBredr`. Compile the default Receiver with:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

Defaults are `GIGA_BREDR_USE_BTSTACK=0` (project host), `GIGA_BREDR_UART_BAUD=2000000`, `GIGA_BREDR_I2S_WS_PIN=74` and `GIGA_BREDR_PRIVATE_SCHED_WITNESS=0`. [Dev.13 build evidence](docs/validation/dev13-2026-10-27.json) identifies the package build separately from previous field images; [BUILD.json](docs/BUILD.json) retains historical dev.10 and earlier evidence.

Use **921600** in the serial monitor. GIGA's native USB CDC transfer rate is independent of this baud setting. The diagnostic Receiver queues log lines and pumps at most 64 bytes per foreground turn through the nonblocking USB API; skipped lines increment `console_drops`. Formatting and USB service still consume CPU time.

Choose [A2dpOnly](examples/A2dpOnly/A2dpOnly.ino), [BleOnly](examples/BleOnly/BleOnly.ino), or [A2dpBleCoexist](examples/A2dpBleCoexist/A2dpBleCoexist.ino); see [operation modes](docs/operation-modes.md) for their configuration and validation limits.

| GIGA pin | PCM5102A signal |
|---|---|
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

Use the breakout supply and 3.3 V logic. Output is Philips I2S with stereo 32-bit slots and left-aligned samples; slot width is not measured numerical precision. MCLK defaults to -1 (disabled); PCM5102A can use its BCLK PLL. Configure `ReceiverConfig.i2s` before `begin()` for a compatible hardware route. Alternate pads/optional 256Fs MCLK remain electrically unqualified; see [pin routing](docs/runtime-i2s-pins.md).

The PCM5102A analog L/R outputs are line outputs specified for loads of at least 1 kOhm. For lower-impedance headphones, connect the DAC to a headphone amplifier's line input, then connect the headphones to the amplifier. See the [TI PCM510xA datasheet](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf). Direct headphone loading can distort the analog output even when the receiver reports no digital audio faults.

### Optional 3 Mbaud controller UART

Dev.11 admits `GIGA_BREDR_UART_BAUD=3000000` as an experimental build-time option for the project-owned host. The default stays `2000000`; this is separate from the serial monitor's 921600 USB CDC setting. For a stock Arduino CLI build:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . --build-property "compiler.cpp.extra_flags=-DGIGA_BREDR_UART_BAUD=3000000" examples/Receiver
```

Add the define to any existing `compiler.cpp.extra_flags` instead of dropping other project flags. A sketch-local `#define` does not configure the separately compiled library C++ files. Use the same build setting for the example being uploaded.

With the dev.12 automatic LHDC 80/90-ms profile, a public SCHED0 / 3 Mbaud Receiver image completed three BLE reconnect/data cycles: 130 verified 512-byte exchanges at MTU 517, with zero recorded audio/RTP faults during a 600-second host window. Clock CUM still recorded 14 overruns. The separately compiled version-12 build produces exactly the same firmware BIN bytes as that readback-verified field image; final documentation-only packaging is checked against every source/Receiver input. Default 2 Mbaud was checked by compiling both UART integration units, not a new full default-image field test.

The earlier 60/70-ms public image failed its 3 Mbaud workload with 59,328 missing PCM frames despite zero RTP discontinuities. The new profile adds 30 KiB to the selected LHDC192 arena; other codec profiles are unchanged. Explicit manual overrides remain possible. A 70-ms LHDC ring with no explicit startup setting is now rejected because it cannot fit the inherited 80-ms watermark; see [buffer configuration and compatibility](docs/audio-buffering.md).

These are sequential finite observations, not a controlled RF/causal comparison. Historical private 3 Mbaud register observations remain in [dev.11 validation](docs/validation/dev11-2026-10-27.md): `kernel_hz=0` was UNKNOWN/unsupported, and nominal 3 Mbaud relied on the separate conditional clock model. Neither that observation nor the new digital counters measures physical clock frequency, acoustics or total latency.

The port keeps 4 Mbaud rejected: its clock procedure and board operation are not qualified. No extra controller clock command, HCD or buffer change is included. [Infineon's baud-change example](https://community.infineon.com/t5/Knowledge-Base-Articles/Using-Vendor-Specific-Commands-in-AnyCloud-Bluetooth-Applications/ta-p/267534) documents 3 Mbaud; that does not establish every host/board configuration.

## API and configuration

Call `receiver.poll()` from `loop()` to deliver bounded configuration/volume/metadata/diagnostic callbacks. Radio and decoder workers run independently. Check `begin()` and Status results, keep callbacks bounded, serialize `begin/end/poll` and keep the receiver alive through callbacks. `end()` releases receiver-owned resources. `availableCodecs()` reports compiled support; `config.codecs=0` permits the compiled set, while a nonzero mask limits negotiation.

```cpp
#include <GigaBredr.h>
using namespace giga_bredr;
GigaBredr receiver;

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name = "GIGA Audio";
  config.codecs = 0; // All compiled codecs remain available.
  config.i2s.bclk = 70; config.i2s.data = 72;
  config.i2s.lrck = 74; config.i2s.mclk = -1;
  config.ble.enabled = true; config.ble.maximum_mtu = 517;
  config.ble.audio_connection_preference.interval_min = 96;
  config.ble.audio_connection_preference.interval_max = 96; // 120 ms.
  Status status = receiver.begin(config);
  if (status != Status::Ok) Serial.println(statusName(status));
}

void loop() { receiver.poll(); }
```

Zero buffer fields inherit selected-codec defaults. Only the selected configuration allocates: packet/AU storage, decoded scratch and PCM ring. Let P be the real-PCM startup watermark, R ring capacity and H one DMA half. Startup/recovery waits for real decoded PCM; automatic geometry requires 2H <= P <= R. The running clock target is fixed at T = P - 2H in real-PCM mode, retained through learning/recovery; its inventory includes estimated compressed work, so T is not a guaranteed PCM floor or end-to-end latency.

Nonzero `config.codec_buffers[static_cast<size_t>(Codec::LhdcV5)]` fields override globals for that codec, for example `startup_buffer_ms=60`, `pcm_buffer_ms=70`. Nonzero `startup_packets` selects the explicit legacy compressed-packet startup/learned-target path; leave `startup_buffer_ms` zero in that same layer. Setting both startup modes together is rejected. A manual P=2H may prime audio but makes optional fixed-target tracking ineligible. See [buffering formulas and memory](docs/audio-buffering.md). Buffer capacity is not measured latency.

Immutable tables remain in flash; selected decoder/buffers release on reconfiguration/end. `setExternalAllocator()` supplies allocation/free hooks, but the application must provide and initialize that memory. Audio clock control requires exclusive PLL2 ownership; stock SDRAM initialization also uses PLL2R. The library does not automatically initialize SDRAM or establish compatible external-memory clocks, placement, fragmentation or stack headroom.

## Codecs and unfinished features

All six supplied decoder-operation families, eight Codec identities and seven enabled build defaults remain: SBC/SBC-XQ, AAC-LC, HE-AAC v1, aptX/aptX HD, LDAC, LHDC V5 and Opus. SBC-XQ shares the SBC identity. One selected codec runs at a time. Compiled support and valid-vector execution do not make a phone offer a mode or qualify every rate/depth/transport.

Full HFP/HSP, RFCOMM/SCO call operation and a public pluggable microphone-input API remain unfinished. Standalone RFCOMM/HF/HS protocol modules and an internal None/Provider microphone contract are present; they are unadvertised and have no integrated SCO or duplex voice path. The existing AVRCP volume and bounded metadata callback paths are retained. Typed Play/Pause/Stop/Next/Previous taps, copied metadata/playback-status snapshots and scoped channel retirement are implemented. The28-case portable CPU suite and full official CM7 Receiver link pass; actual target control/metadata interoperability remains unverified. See [AVRCP API and completion limits](docs/avrcp.md). AVRCP browsing, cover art and full profile coverage remain unfinished. PCM5102A is playback output, not a microphone path. HE-AACv2/PS, xHE-AAC and BLE Audio are not implemented.

## BLE server and coexistence

BLE is an unencrypted peripheral/GATT server, not a central/client or encrypted pairing implementation. ATT starts at MTU 23 and may negotiate 517; values are bounded by min(512,MTU-3). `bleReadWrite()` returns a copied write. `bleNotify(data,length)` admits a copied value; `bleNotify(origin)` rejects stale generations. Keep a pending value on `Busy`/`OutputFull`, as the Receiver echo example does. Ok does not prove remote receipt.

The base preference is 80-120 ms; Receiver requests 120 ms (96 units), latency 2 and 5 s supervision timeout. Actual parameters remain peer/controller observed facts, separate from requests. Existing 1 s connection-age/cooldown rules remain. During secure Classic audio the host prioritizes Classic receive work and paces ATT fragments. This is not an RF airtime reservation.

`bleDisconnect(generation,false)` queues an owner-bound asynchronous LE disconnect and holds later advertising; `bleResumeAdvertising()` releases that hold through normal GAP processing. Observe actual disconnected/advertising/new-generation events; Ok is admission. Discard saved origins/generations after `end()`/`begin()`.

## Validation and known limits

Longer playback after the finite dev.12 test produced brief PCM shortages. Historical zero-fault windows below retain their finite scope and do not establish continuous long-term playback. The private first-shortage diagnostic is outside this public package.

| Evidence | Result | Limit |
|---|---|---|
| Dev.13 profile source | 28 portable Android ARM64 CPU cases passed; full official CM7 compilation recorded separately | No dev.13 firmware deployment, phone control interoperability, integrated call audio or audio-cure claim |
| Dev.12 public-code SCHED0 / 3 Mbaud field image | LHDC192/24, fixed1000 request; 600 s host / 598.297 s printed; three BLE cycles / 130 verified512B exchanges; zero recorded audio/RTP faults | Clock CUM FAILED14; total latency, acoustics and all-codec operation unqualified |
| Dev.11 public-code SCHED0 / 3 Mbaud baseline | 60/70-ms profile; 135 verified512B exchanges; RTP/decoder/transport fault counters zero | FAILED59,328 missingPCM frames /309 underrun callbacks and clock CUM11 |
| Actual private Receiver, SCHED1 | LHDC192 natural 90.072132986 s; 46 verified 512-byte GATT exchanges / 60.005 s / MTU 517; 3 physical BLE controls; 43 non-clock fault gates zero | Clock CUM FAILED 2/88, max 22.1229167 us against 20 us; supply unexercised; prior starvation unresolved |
| Actual M7 decoder-only benchmark, SCHED0 | 12 configurations x 20 passes, 3440 successful calls; reference/tone, status, count, deadline and allocation-close gates valid | Capture FRAMING_DEGRADED: 1 partial replay line; later complete retained rows valid; no global PASS |
| Default Receiver build-basis archive, SCHED0 | Official extracted 503-file archive compiled/audited in 104.422 s; 170 library TUs, all six Ops / seven defaults; BIN cf7132ca.../496552 bytes | Final documentation-only ZIP not separately compiled; no default-image field deployment or audio qualification |

Forty coherent printed tracking cohorts matched T=11136 frames, zero mismatched; five remain UNKNOWN across 1 ms print boundaries. All original 20 us / 44 / CUM gates remain mandatory; a sampled/final zero cannot erase cumulative failure. Inclusive timings do not identify a preemptor or prove instrumentation neutrality.

The benchmark covers SBC/SBC-XQ, AAC-LC ADTS/LATM, aptX/HD, Opus 48k stereo 20 ms, LDAC 96 mono/stereo and HE v1 SBR 44.1 mono/stereo. It does not run radio/I2S. See [per-case timing/heap metrics and oracles](docs/validation/dev10-2026-10-26.md). LHDC independent encoded golden remains MISSING; LDAC uses tone checks, aptX HD PCM16 does not establish 24-bit fidelity, HE PS/xHE/other configurations are unqualified, and Opus fixture evidence is not phone interoperability. Apple AAC sender and distinct SBC-XQ sender operation remain unproved.

Phone-family history and all earlier failures stay in [STATUS](docs/STATUS.md) and [BUILD](docs/BUILD.json). Finite zero non-clock faults do not prove RF delivery, physical I2S, low-bit accuracy, acoustics, general continuity or 200 ms total latency. [EXPORT](docs/EXPORT.md) distinguishes prospective develop-only distribution from actual build evidence.

Original Apache-2.0 project terms and every supplied component notice/license are preserved. [THIRD_PARTY.md](THIRD_PARTY.md) remains authoritative for component terms and existing unresolved grants; this version creates no new grant.

## Operation-mode examples

Use [A2dpOnly](examples/A2dpOnly/A2dpOnly.ino), [BleOnly](examples/BleOnly/BleOnly.ino), or [A2dpBleCoexist](examples/A2dpBleCoexist/A2dpBleCoexist.ino). Set enable_a2dp=false for real BLE-only operation; disabling I2S alone keeps Classic enabled. Existing defaults remain A2DP+BLE. See [mode selection, DAC wiring and nonblocking BLE echo](docs/operation-modes.md).
