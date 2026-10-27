# GigaBredr

Bluetooth Classic A2DP receiver and BLE GATT server for the **Arduino GIGA R1 WiFi, Cortex-M7**, with PCM5102A-compatible I2S playback. GigaBredr owns the host stack; the radio controller uses Infineon ROM and the stock HCD patch, with Arduino Mbed/RTOS scheduling.

This is the **mainline** installation guide. Main contains the selected tested source baseline; [develop](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/tree/develop) is the channel for ongoing development. The current library identity remains `0.1.0-dev.12`: mainline promotion does not establish a stable release. Audio continuity, the complete codec/configuration matrix and call/microphone support remain unfinished. See [tested behavior and known limits](#tested-behavior-and-known-limits) and [release channels](docs/RELEASE_CHANNELS.md).

## Install and start

1. Download the [mainline source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/heads/main.zip).
2. In Arduino IDE choose **Sketch > Include Library > Add .ZIP Library**. Keep one active GigaBredr installation.
3. Install **Arduino Mbed OS GIGA Boards 4.6.0** and select **Arduino GIGA R1 / Cortex-M7**.
4. Wire the DAC below, open **File > Examples > GigaBredr > Receiver**, and upload.
5. Open the serial monitor at **921600**, then connect a Bluetooth audio source to the receiver name configured in the example.

The main ZIP follows the branch as it changes. For reproducible reports, retain the downloaded source and commit identifier; see [release channels](docs/RELEASE_CHANNELS.md) for pinned downloads and development snapshots.

For a local installation, place the folder containing `library.properties`, `src` and `examples` at `<sketchbook>/libraries/GigaBredr`. From the library folder, compile with:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

The Receiver example uses automatic buffers for the negotiated configuration. Alternatives are [A2dpOnly](examples/A2dpOnly/A2dpOnly.ino), [BleOnly](examples/BleOnly/BleOnly.ino) and [A2dpBleCoexist](examples/A2dpBleCoexist/A2dpBleCoexist.ino). See [operation modes](docs/operation-modes.md). Set `enable_a2dp=false` for BLE-only operation; disabling I2S alone leaves Classic enabled.

## DAC wiring

| GIGA pin | PCM5102A signal |
|---|---|
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

Use the breakout's specified supply and 3.3 V logic. Output uses Philips I2S, stereo 32-bit slots and left-aligned samples; slot width does not establish numerical precision. MCLK defaults to `-1` (disabled); PCM5102A can use its BCLK PLL. Configure `ReceiverConfig.i2s` before `begin()` for a compatible route. Alternate pads and optional 256Fs MCLK remain electrically unqualified; see [pin routing](docs/runtime-i2s-pins.md).

The PCM5102A analog L/R outputs are line outputs specified for loads of at least 1 kOhm. Connect lower-impedance headphones through a headphone amplifier's line input. Direct headphone loading can distort output even when digital counters show no faults. See the [TI PCM510xA datasheet](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf).

## Supported building blocks

| Area | Included behavior | Qualification boundary |
|---|---|---|
| Classic audio | A2DP receive, selected-codec decoding and I2S playback | Continuous, gapless playback across all senders/configurations is unqualified |
| Decoder set | SBC/SBC-XQ, AAC-LC, HE-AAC v1, aptX/aptX HD, LDAC, LHDC V5 and Opus | Compiled support does not establish phone interoperability or every rate/depth/transport |
| AVRCP | Volume and bounded metadata/status callbacks | Complete typed transport controls and full AVRCP coverage remain unfinished |
| BLE | Peripheral/GATT server, read/write/notify, negotiated MTU up to 517 | No central/client or encrypted BLE pairing implementation; BLE Audio is unsupported |
| Calls and microphone | Incomplete groundwork | Full HFP/HSP, RFCOMM/SCO call operation and a public pluggable microphone-input API remain unfinished |

One codec runs at a time. All six supplied decoder-operation families, eight Codec identities and seven enabled build defaults remain included; SBC-XQ shares the SBC identity. `availableCodecs()` reports compiled support. HE-AACv2/PS and xHE-AAC are not implemented. The PCM5102A path is playback output.

## API and configuration

Call `receiver.poll()` from `loop()` to deliver configuration, volume, metadata and diagnostic callbacks. Radio and decoder workers run independently. Check `begin()` and Status results, keep callbacks bounded, serialize `begin/end/poll`, and keep the receiver alive through callbacks. `end()` releases receiver-owned resources.

```cpp
#include <GigaBredr.h>
using namespace giga_bredr;
GigaBredr receiver;

void setup() {
  Serial.begin(921600);
  ReceiverConfig config;
  config.name = "GIGA Audio";
  config.codecs = 0; // Allow all compiled codecs; use a mask to restrict negotiation.
  config.i2s.bclk = 70;
  config.i2s.data = 72;
  config.i2s.lrck = 74;
  config.i2s.mclk = -1;
  config.ble.enabled = true;
  config.ble.maximum_mtu = 517;
  config.ble.audio_connection_preference.interval_min = 96;
  config.ble.audio_connection_preference.interval_max = 96; // Request 120 ms.
  Status status = receiver.begin(config);
  if (status != Status::Ok) Serial.println(statusName(status));
}

void loop() { receiver.poll(); }
```

Zero buffer fields inherit selected-codec defaults. Only the selected configuration allocates its packet/AU storage, decoded scratch and PCM ring. The automatic LHDC profile uses an 80 ms startup reserve and a 90 ms ring. Other codec profiles retain their existing defaults. Nonzero per-codec fields override globals; see [buffer configuration, formulas and compatibility](docs/audio-buffering.md) before selecting manual values. Buffer capacity is not measured phone-to-DAC latency.

Immutable tables remain in flash; the selected decoder and buffers release on reconfiguration/end. `setExternalAllocator()` supplies allocation/free hooks, but the application must initialize and provide that memory. Audio clock control requires exclusive PLL2 ownership; stock SDRAM initialization also uses PLL2R. External-memory clocks, placement, fragmentation and stack headroom require application validation.

### Controller UART and logging

Defaults are `GIGA_BREDR_USE_BTSTACK=0` (project host), `GIGA_BREDR_UART_BAUD=2000000`, `GIGA_BREDR_I2S_WS_PIN=74` and `GIGA_BREDR_PRIVATE_SCHED_WITNESS=0`.

The controller UART has an experimental **3 Mbaud** build option, used in the finite LHDC/BLE validation below. To opt in:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . --build-property "compiler.cpp.extra_flags=-DGIGA_BREDR_UART_BAUD=3000000" examples/Receiver
```

Append the define to any existing `compiler.cpp.extra_flags`. A sketch-local `#define` does not configure separately compiled library C++ files. The default remains 2 Mbaud; 4 Mbaud is rejected because its clock procedure and board operation are unqualified.

The serial monitor's 921600 setting is separate from the controller UART. Native USB CDC transfer rate is independent of that setting. Receiver queues diagnostic lines and pumps at most 64 bytes per foreground turn through the nonblocking USB API; skipped lines increment `console_drops`. Formatting and USB service still consume CPU time.

### BLE server and coexistence

ATT starts at MTU 23 and may negotiate 517; attribute values are bounded by `min(512, MTU-3)`. `bleReadWrite()` returns a copied write. `bleNotify(data,length)` admits a copied value; `bleNotify(origin)` rejects stale generations. Retain pending data on `Busy`/`OutputFull`, as the Receiver echo example does. An `Ok` result does not prove remote receipt.

The base connection preference is 80-120 ms; Receiver requests 120 ms, latency 2 and a 5 s supervision timeout. Actual applied parameters depend on the peer/controller. During secure Classic audio, the host prioritizes Classic receive work and paces ATT fragments. This does not reserve RF airtime.

`bleDisconnect(generation,false)` queues an owner-bound asynchronous LE disconnect and holds later advertising; `bleResumeAdvertising()` releases the hold through normal GAP processing. Observe actual disconnected/advertising/new-generation events. Discard saved origins/generations after `end()`/`begin()`.

## Tested behavior and known limits

Main retains the dev.12 source baseline and its dated evidence. The [validation report](docs/validation/dev12-2026-10-27.md) and [machine-readable record](docs/validation/dev12-2026-10-27.json) describe the exact configuration and artifacts:

| Check | Observed result | Scope |
|---|---|---|
| Full Cortex-M7 Receiver build | 106.891 s; 170 library translation units, all six codec-operation families/seven defaults, unchanged stock HCD audited | Public SCHED0, opt-in 3 Mbaud build |
| Firmware identity | BIN SHA-256 `68e0879498d369ed2f008e541cfdf34ec53e0c6e9559b295489dee610f85ae3d` | Byte-identical to the readback-verified field image; documentation edits do not create new field evidence |
| LHDC192/24 with fixed 1000 kbps requested and concurrent BLE | 600 s host window; three BLE cycles, 130 verified 512-byte exchanges at MTU 517; zero recorded audio/RTP faults in that window | Finite observation of that configuration |
| Clock update timing | 14 cumulative measurements exceeded the 20 us software budget | Timing gate remains failed; inclusive software time does not establish PLL unlock or I2S stop |
| Default 2 Mbaud | Both UART integration units compiled | No new full default-image field test in this update |

Longer listening has exposed a brief song-transition gap and additional underruns; continuity investigation remains open. The finite zero-fault window above does not establish continuous or gapless playback.

The complete codec/rate/depth/transport matrix is unfinished. Decoder-only benchmarks do not run radio or I2S; independent LHDC encoded golden evidence remains missing. Apple AAC sender, distinct SBC-XQ sender and Opus phone interoperability remain unproved. Low-bit fidelity, physical I2S waveform qualification, calibrated acoustics and the requested 200 ms total phone-to-DAC latency remain unproved.

Historical tests and failures remain in [STATUS](docs/STATUS.md), [BUILD](docs/BUILD.json) and the [validation reports](docs/validation). Dated packaging/export records describe their original development snapshots; [release channels](docs/RELEASE_CHANNELS.md) describes current main/develop distribution.

## License and component notices

Original Apache-2.0 project terms and all supplied component notices/licenses are preserved. [THIRD_PARTY.md](THIRD_PARTY.md) remains authoritative for component terms and existing unresolved grants. Mainline promotion creates no new grant or licensing clearance.
