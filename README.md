# GigaBredr 0.1.0-dev.10 (experimental)

Bluetooth Classic A2DP receiver and public BLE GATT server for Arduino GIGA R1 Cortex-M7, with all supplied codec decoders and PCM5102A-compatible I2S playback. The project owns the host stack; the radio controller uses Infineon ROM plus the stock HCD patch, and scheduling uses Arduino Mbed/RTOS.

**Not stable. Clock-deadline failures and earlier audio-supply gaps remain unresolved. Total phone-to-DAC latency below 200 ms, full numerical fidelity and acoustic continuity are unproved.** The 503-file dev.10 build-basis archive has been extracted, compiled for default Cortex-M7 and statically audited. This final documentation-only revision requires archive/input/property parity verification; no default-image field deployment is claimed.

## Install and start

1. When published, download the [v0.1.0-dev.10 source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/tags/v0.1.0-dev.10.zip). This is a prospective versioned link, not a stable release claim.
2. In Arduino IDE choose **Sketch > Include Library > Add .ZIP Library**. Keep one active GigaBredr installation.
3. Install **Arduino Mbed OS GIGA Boards 4.6.0** and select **Arduino GIGA R1 / Cortex-M7**.
4. Open **File > Examples > GigaBredr > Receiver**, wire the output below, and upload. The example uses automatic buffers sized for the negotiated configuration.

For local installation, place the folder containing `library.properties`, `src` and `examples` at `<sketchbook>/libraries/GigaBredr`. Compile the default Receiver with:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

Defaults are `GIGA_BREDR_USE_BTSTACK=0` (project host), `GIGA_BREDR_UART_BAUD=2000000`, `GIGA_BREDR_I2S_WS_PIN=74` and `GIGA_BREDR_PRIVATE_SCHED_WITNESS=0`. [BUILD.json](docs/BUILD.json) records the actual default build-basis compile separately from private Receiver/benchmark evidence.

| GIGA pin | PCM5102A signal |
|---|---|
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

Use the breakout supply and 3.3 V logic. Output is Philips I2S with stereo 32-bit slots and left-aligned samples; slot width is not measured numerical precision. MCLK defaults to -1 (disabled); PCM5102A can use its BCLK PLL. Configure `ReceiverConfig.i2s` before `begin()` for a compatible hardware route. Alternate pads/optional 256Fs MCLK remain electrically unqualified; see [pin routing](docs/runtime-i2s-pins.md).

The PCM5102A analog L/R outputs are line outputs specified for loads of at least 1 kOhm. For lower-impedance headphones, connect the DAC to a headphone amplifier's line input, then connect the headphones to the amplifier. See the [TI PCM510xA datasheet](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf). Direct headphone loading can distort the analog output even when the receiver reports no digital audio faults.

## API and configuration

Call `receiver.poll()` from `loop()` to deliver bounded configuration/volume/metadata/diagnostic callbacks. Radio and decoder workers run independently. Check `begin()` and Status results, keep callbacks bounded, serialize `begin/end/poll` and keep the receiver alive through callbacks. `end()` releases receiver-owned resources. `availableCodecs()` reports compiled support; `config.codecs=0` permits the compiled set, while a nonzero mask limits negotiation.

```cpp
#include <GigaBredr.h>
using namespace giga_bredr;
GigaBredr receiver;

void setup() {
  Serial.begin(115200);
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

Full HFP/HSP, RFCOMM/SCO call operation and a public pluggable microphone-input API remain unfinished. AVRCP volume and bounded metadata/status callbacks exist; complete typed transport controls and full AVRCP coverage remain unfinished. PCM5102A is playback output, not a microphone path. HE-AACv2/PS, xHE-AAC and BLE Audio are not implemented.

## BLE server and coexistence

BLE is an unencrypted peripheral/GATT server, not a central/client or encrypted pairing implementation. ATT starts at MTU 23 and may negotiate 517; values are bounded by min(512,MTU-3). `bleReadWrite()` returns a copied write. `bleNotify(data,length)` admits a copied value; `bleNotify(origin)` rejects stale generations. Keep a pending value on `Busy`/`OutputFull`, as the Receiver echo example does. Ok does not prove remote receipt.

The base preference is 80-120 ms; Receiver requests 120 ms (96 units), latency 2 and 5 s supervision timeout. Actual parameters remain peer/controller observed facts, separate from requests. Existing 1 s connection-age/cooldown rules remain. During secure Classic audio the host prioritizes Classic receive work and paces ATT fragments. This is not an RF airtime reservation.

`bleDisconnect(generation,false)` queues an owner-bound asynchronous LE disconnect and holds later advertising; `bleResumeAdvertising()` releases that hold through normal GAP processing. Observe actual disconnected/advertising/new-generation events; Ok is admission. Discard saved origins/generations after `end()`/`begin()`.

## Validation and known limits

| Evidence | Result | Limit |
|---|---|
| Actual private Receiver, SCHED1 | LHDC192 natural 90.072132986 s; 46 verified 512-byte GATT exchanges / 60.005 s / MTU 517; 3 physical BLE controls; 43 non-clock fault gates zero | Clock CUM FAILED 2/88, max 22.1229167 us against 20 us; supply unexercised; prior starvation unresolved |
| Actual M7 decoder-only benchmark, SCHED0 | 12 configurations x 20 passes, 3440 successful calls; reference/tone, status, count, deadline and allocation-close gates valid | Capture FRAMING_DEGRADED: 1 partial replay line; later complete retained rows valid; no global PASS |
| Default Receiver build-basis archive, SCHED0 | Official extracted 503-file archive compiled/audited in 104.422 s; 170 library TUs, all six Ops / seven defaults; BIN cf7132ca.../496552 bytes | Final documentation-only ZIP not separately compiled; no default-image field deployment or audio qualification |

Forty coherent printed tracking cohorts matched T=11136 frames, zero mismatched; five remain UNKNOWN across 1 ms print boundaries. All original 20 us / 44 / CUM gates remain mandatory; a sampled/final zero cannot erase cumulative failure. Inclusive timings do not identify a preemptor or prove instrumentation neutrality.

The benchmark covers SBC/SBC-XQ, AAC-LC ADTS/LATM, aptX/HD, Opus 48k stereo 20 ms, LDAC 96 mono/stereo and HE v1 SBR 44.1 mono/stereo. It does not run radio/I2S. See [per-case timing/heap metrics and oracles](docs/validation/dev10-2026-10-26.md). LHDC independent encoded golden remains MISSING; LDAC uses tone checks, aptX HD PCM16 does not establish 24-bit fidelity, HE PS/xHE/other configurations are unqualified, and Opus fixture evidence is not phone interoperability. Apple AAC sender and distinct SBC-XQ sender operation remain unproved.

Phone-family history and all earlier failures stay in [STATUS](docs/STATUS.md) and [BUILD](docs/BUILD.json). Finite zero non-clock faults do not prove RF delivery, physical I2S, low-bit accuracy, acoustics, general continuity or 200 ms total latency. [EXPORT](docs/EXPORT.md) distinguishes prospective develop-only distribution from actual build evidence.

Original Apache-2.0 project terms and every supplied component notice/license are preserved. [THIRD_PARTY.md](THIRD_PARTY.md) remains authoritative for component terms and existing unresolved grants; this version creates no new grant.

## Operation-mode examples

Use [A2dpOnly](examples/A2dpOnly/A2dpOnly.ino), [BleOnly](examples/BleOnly/BleOnly.ino), or [A2dpBleCoexist](examples/A2dpBleCoexist/A2dpBleCoexist.ino). Set enable_a2dp=false for real BLE-only operation; disabling I2S alone keeps Classic enabled. Existing defaults remain A2DP+BLE. See [mode selection, DAC wiring and nonblocking BLE echo](docs/operation-modes.md).
