# GigaBredr 0.2.0 (experimental)

Bluetooth Classic A2DP receiver with board-pin I2S output, BLE GATT server and
additional Classic profiles for the Arduino GIGA R1 WiFi (Cortex-M7), using a
project-owned host stack on the board's Murata 1DX (CYW4343W) radio.

## Quick start

1. Install **Arduino Mbed OS GIGA Boards 4.6.0** and select **Arduino GIGA R1 / Cortex-M7**.
2. Install **GigaBredr**: Library Manager (search "GigaBredr"), or **Sketch > Include Library > Add .ZIP Library** with a [release ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/releases). Pinned and development downloads: [release channels](docs/RELEASE_CHANNELS.md).
3. Wire an I2S DAC such as a PCM5102A: **D70 → BCK, D74 → LCK, D72 → DIN**, 3.3 V and GND.
4. Upload this sketch, then pair your phone or PC with "GIGA Speaker":

```cpp
#include <GigaBredr.h>
using namespace giga_bredr;

GigaBredr receiver;

void setup() {
  ReceiverConfig config;
  config.name = "GIGA Speaker";
  receiver.begin(config);   // A2DP sink, AVRCP and BLE server; all compiled codecs
}

void loop() {}
```

Every codec negotiates automatically; buffers are allocated only for the one
in use. **File > Examples > GigaBredr** has ready sketches: `SbcReceiver`
(smallest), `CodecSelect`, `Receiver` (full diagnostics), `A2dpBleCoexist`,
`SppEcho`, `HfpReceiver`, `HidKeyboard`, `ObexPush`, `PhonebookPull`,
`MessageListing`, `AvrcpRemote` and `CpbBroadcast`.

### Choose which codecs are compiled in

Add one line at global scope in your sketch:

```cpp
GIGA_BREDR_CODECS(Sbc, Aac, LhdcV5);
```

Names: `Sbc` (SBC and SBC-XQ; always keep it), `Aac`, `AacHe`, `Aptx`, `AptxHd`,
`Ldac`, `LhdcV5`, `Opus`, `Ssc`. Unlisted decoders are left out of flash and are
not offered to phones. Without the line, every codec is included. The
`CodecSelect` example measured:

| Selection | Flash used |
|---|---|
| all codecs (no line) | 595,576 B |
| `Sbc, Aac, LhdcV5` | 458,544 B |
| `Sbc` | 342,344 B |

### Profiles

Each profile has a `ReceiverConfig` switch, set before `begin()`; off by default unless noted.

| Profile | Enable with | Status |
|---|---|---|
| A2DP sink (audio) | on by default | on-board with phone and Linux senders; see codec table |
| AVRCP (volume, metadata, play/pause/next) | on by default | volume/metadata on-board; controller interop limited |
| BLE GATT server | on by default (`config.ble.enabled`) | on-board echo during LHDC 192 kHz streaming |
| SPP serial port | `config.spp.enabled` | on-board echo, ~50 kbit/s |
| HFP / HSP hands-free (experimental) | `config.voice.hands_free` / `.headset` | limited runs; full call flow not established |
| HID keyboard/mouse/gamepad | `config.hid.enabled` | native tests; not yet tested over the air |
| OBEX Object Push, PBAP, MAP | `config.obex.opp_enabled` / `.pbap_enabled` / `.map_enabled` | native tests; not yet tested over the air |
| Connectionless Peripheral Broadcast | `config.cpb.role` | transmitter sequence on-board; reception untested |

What each claim rests on, codec by codec: [validation matrix](docs/validation/dev18-validation-matrix.md).

## What is new in dev.18 (2026-11-20)

- **Fast, Linux-compatible connects.** Accepted links no longer race the
  initiator's authentication (BlueZ previously failed with EBUSY). Idle page
  and inquiry scan are interlaced at 40 ms. Measured reconnect from Linux:
  0.56-0.74 s. [Details](docs/validation/connect-speed-2026-11-20.md).
- **Persistent pairing.** `GigaQspiBondStorage` keeps Classic link keys in the
  last MiB of QSPI flash, so phones reconnect after resets and sketch uploads.
  The Receiver and SppEcho examples use it.
- **Samsung SSC decoder** (44.1/48 kHz, Seamless HiFi 48/24, UHQ 96/24),
  lazily allocated (22,920 B), verified bit-exact natively and over the air in
  all three modes. [Details](docs/validation/ssc-integration.md).
- **SPP (Serial Port Profile) server**, off by default; `sppWrite`/`sppRead`
  and the `SppEcho` example. [Details](docs/validation/spp.md).
- **BLE coexistence:** `BleConfig::audio_coexistence_reserve_ms` (default 100)
  widens the automatic reserve of >=88.2 kHz streams (e.g. LHDC V5 192 kHz)
  while BLE is enabled; if the wider ring cannot be allocated the stream opens
  with the base reserve. [Measurements and limits](docs/validation/ble-coexistence-2026-11-20.md).
- **Connectionless Peripheral Broadcast (CSB/CPB)**, off by default: one GIGA
  broadcasts a small payload to any number of synchronized GIGA receivers,
  alongside A2DP (`config.cpb`, `cpbSetData`/`cpbRead`, the `CpbBroadcast`
  example). The radio supports both roles; the transmitter has run on the
  board, reception still needs a second board.
  [Details](docs/validation/csb-capability-2026-11-20.md).
- **MAP client (Message Access, MCE)**, off by default: folder listing,
  message listing and message pull from the connected phone
  (`mapGetMessageListing`, `mapGetMessage`, the `MessageListing` example).
  No notification server. Native synthetic checks only so far.
  [Details](docs/validation/map.md).

Codecs: SBC/SBC-XQ, AAC-LC (+ experimental HE-AAC), aptX, aptX HD, LDAC,
LHDC V5, Opus, Samsung SSC. Each is compiled in by `GBR_ENABLE_*` and its
memory is allocated only while that codec's stream is configured.

**Keep `loop()` yielding.** Arduino Mbed's RTOS does not time-slice threads
of equal priority. A `loop()` that calls `receiver.poll()` without ever
blocking starves the core's USB thread that performs the 1200-baud upload
reset, so uploads then need a double-press of RESET. The examples call
`delay(1)` each pass; do the same in your sketches.

## Earlier dev.17 notes


**Current unreleased local source, October 29:** this tree includes the combined
Service Changed, HF control, codec arithmetic and shared-clock relearning
candidates. The combined source passed native synthetic tests and a default
Cortex-M7 Receiver build/static audit; it has not been deployed or field
qualified. RX-derived SCO pacing is disabled, even if its legacy opt-in is set.
See [current local source scope and limits](docs/validation/current-local-source-2026-10-29.md).
The published dev.17 ZIP linked below is the earlier release and does not
contain this unreleased source; no stable release is implied.

**Evidence links:** historical validation pages may link to workspace-only
`build/dev...` receipts outside this library. Those local artifacts are not
included in a copied source package; their absence does not create a pass.

**Earlier admission-aware logging source, October 29:** profile snapshots now retry after rejected
console admission and recover current copied metadata values. Volume range
checks use explicit `*_in_range` names. The console rejects truncated records.
The exact source passes103 native runtime invocations, eight child suites and
both official CM7 builds/static audits; this admission-aware successor is not
yet flashed. See the [current source and hardware report](docs/validation/current-profile-logging-2026-10-29.md).

**Earlier deployed logging evidence:** the Receiver logs escaped metadata,
0–127 volume, named received AVRCP playback status and separate A2DP/output
state. LE counters are aggregated. Reviewed fresh-SSP responder completion
and late-authentication handling are integrated; security requirements remain
unchanged. Both combined CM7 builds/static audits pass, along with103 native
runtime invocations and eight child suites. Evidence is recorded in the
[ongoing LE and profile logging report](docs/validation/continuous-le-and-profile-logging-2026-10-29.md).
In that earlier test the private UART3M image was flashed and readback-verified. It has
received metadata, paused/playing status and volume64 then55; normal volume-down
moved the phone index75 to65. The recorded window included LDAC96k music and verified512-byte LE echoes
together. The client has no run/transaction cap; real firmware-restart and
pairing gaps are retained. These finite observations are not long-term or
all-codec qualification. That later admission-aware logging source was unflashed in this record.

**Previous local source:** bounded AVRCP status/metadata reconciliation and an
optional application-owned Classic bond store and normal setup-response validation
are now integrated. All99 native
runtime invocations/eight child suites pass, and the exact426-file production
snapshot passes the stock GIGA4.6.0 M7 build/static audit at the default UART2M.
These changes are not deployed or physically qualified. Default bonds remain
RAM-only; the storage interface chooses no NV backend or automatic flash writes.
See [the integration evidence and storage contract](docs/validation/bond-storage-avrcp-2026-10-28.md).
The [latest parser validation and build](docs/validation/sco-response-validation-2026-10-28.md)
preserves the controller capability and default-off gates.

**Earlier field evidence:** the private e1 probe, before those new changes,
completed30min of MUSIC plus1294 verified512-byte BLE exchanges in each direction
with zero recorded audio faults; its clock gate failed with52 overruns. Four
basic AVRCP metadata attributes were received. Transport controls and voice
remain under physical qualification. The original firmware is restored, but
the later normal pairing was approved and the phone is now bonded/connected.
The earlier logging workload is described above. See the
[feature-by-feature status](docs/validation/all-feature-status-2026-10-28.md).

**Local LHDC correction:** AOSP encoder comparisons exposed incorrect synthesis
windows and empirical output scaling. The corrected source passed eight
synthetic rate/depth configurations with level error below 0.011 dB. See the
[source comparison, tests and limits](docs/validation/lhdc-aosp-synthesis-2026-10-28.md).
An [actual M7 CPU test](docs/validation/lhdc-aosp-m7-2026-10-28.md) passed
512 calls with no observed deadline misses or decode-time allocation. The full
default Receiver settings remain untested on hardware. The private field probe
used UART3M and100/110ms reserve; ARM numerical parity and broader field
continuity remain open.

**Earlier local AAC successor:** this source also contains the reviewed AAC selected-element storage and transactional SBR initialization changes. The source passed native sanitizer checks and an official Cortex-M7 Receiver build/static audit. An earlier private feature probe using these decoder changes supplies the finite LHDC field evidence above; AAC peer qualification remains open. See [AAC allocation evidence and limits](docs/validation/aac-selected-storage-2026-10-28.md). The published dev.17 ZIP linked below retains its earlier source.

Subsequent [current-source M7 decoder execution](docs/validation/current-codec-m7-2026-10-28.md) passed 28 safety checks and 26 numerical comparisons, retaining two LHDC quality failures. It confirmed an 866-byte HE-mono saving and 38-byte stereo overhead. The prior field application and phone settings were restored afterward; the successor Receiver still awaits field qualification.

The local [AvrcpRemote](examples/AvrcpRemote/AvrcpRemote.ino) and experimental [HfpReceiver](examples/HfpReceiver/HfpReceiver.ino) examples also passed official GIGA builds and static audits. They expose playback controls/metadata and opt-in HF/HS readiness respectively; [their hardware and interoperability limits](docs/validation/profile-examples-2026-10-28.md) remain explicit.

Bluetooth Classic A2DP receiver and public BLE GATT server for Arduino GIGA R1 Cortex-M7, with all supplied codec decoders and PCM5102A-compatible I2S playback. The project owns the host stack; the radio controller uses Infineon ROM plus the stock HCD patch, and scheduling uses Arduino Mbed/RTOS.

**Experimental; not stable.** Dev.17 fixes a reproducible host-stack stall: accepted A2DP media can now continue in order while an eligible completed read-only Classic control waits for transmit/reply capacity. The host reuses bounded existing packet storage without adding per-frame heap allocation. All 84 exported progress/response/boundary CPU cases passed, along with the focused existing actor suites; Receiver and VoiceQualification passed fresh official Cortex-M7 builds. That release's validation was native/build evidence; the local successor's finite private field result is identified above. Neither establishes the cause or cure of the separate rare playback shortages. See [dev.17 validation](docs/validation/dev17-2026-10-28.md). Earlier AAC and voice evidence remains under [dev.16](docs/validation/dev16-2026-10-28.md) and [dev.15](docs/validation/dev15-2026-10-27.md).

The existing LHDC automatic reserve remains 80 ms with a 90 ms ring. Earlier finite BLE/A2DP windows had zero recorded audio faults, but longer listening subsequently produced brief PCM shortages. The root cause and a complete continuity fix remain unresolved. No all-codec stability, numerical-fidelity, acoustic or 200 ms total phone-to-DAC latency claim is made.

Media remains subject to normal ownership, security, reassembly, AVDTP state and credit checks. Older control replies keep their FIFO order; mutating signaling, voice traffic and unrecognized or stale responses retain their barriers. This is a scoped host fix, not a radio airtime reservation or a claim that all control traffic can bypass backpressure.

## Install and start

1. Download the [v0.1.0-dev.17 source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/tags/v0.1.0-dev.17.zip) for this version. The [develop source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/heads/develop.zip) follows the current branch HEAD and changes over time; retain its commit identifier when reporting results. These are experimental development sources.
2. In Arduino IDE choose **Sketch > Include Library > Add .ZIP Library**. Keep one active GigaBredr installation.
3. Install **Arduino Mbed OS GIGA Boards 4.6.0** and select **Arduino GIGA R1 / Cortex-M7**.
4. Open **File > Examples > GigaBredr > Receiver**, wire the output below, and upload. The example uses automatic buffers sized for the negotiated configuration.

For local installation, place the folder containing `library.properties`, `src` and `examples` at `<sketchbook>/libraries/GigaBredr`. Compile the default Receiver with:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

Defaults are `GIGA_BREDR_USE_BTSTACK=0` (project host), `GIGA_BREDR_UART_BAUD=2000000`, `GIGA_BREDR_I2S_WS_PIN=74` and `GIGA_BREDR_PRIVATE_SCHED_WITNESS=0`. [Dev.17 build evidence](docs/validation/dev17-2026-10-28.json) identifies the new source builds separately from previous field images. Final packaging changes documentation and the version/paragraph metadata only; the final ZIP is not separately compiled. [BUILD.json](docs/BUILD.json) retains historical dev.10 and earlier evidence.

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
Samsung SSC (`Codec::Ssc`, `GBR_ENABLE_SSC`, A2DP vendor 0x75/0x0103: 44.1/48 kHz, HiFi 48 kHz/24, UHQ v2 96 kHz/24) is an additional, host-verified decoder only; no phone, M7 timing or acoustic evidence. See [SSC integration](docs/validation/ssc-integration.md).

Experimental voice is off unless `ReceiverConfig.voice.hands_free` or `.headset` is enabled. `setVoiceInput` accepts a copied `VoicePcmProvider`; with no provider, the voice input is silence, not a physical microphone. `detachVoiceInput` returning Ok fences old callbacks; on Busy, retry before destroying the provider context. Conditional voice registration requires successful controller setup and an available worker. These source paths are not yet qualified on the GIGA/controller or with a phone call. Voice takes exclusive I2S ownership and may preempt media; see [voice source and limits](docs/voice-phase2-source-status.md).

The existing AVRCP volume and bounded metadata callback paths are retained. Typed Play/Pause/Stop/Next/Previous taps, copied metadata/playback-status snapshots and scoped channel retirement are implemented; target interoperability remains unverified. See [AVRCP API and completion limits](docs/avrcp.md). HSP's public button facade is implemented and passes native/M7 checks; controlled-call interoperability remains unqualified. Advanced HF procedures, AVRCP browsing and cover art remain unfinished. PCM5102A is playback output, not a microphone path. HE-AACv2/PS, xHE-AAC and BLE Audio are not implemented.

## BLE server and coexistence

BLE is an unencrypted peripheral/GATT server, not a central/client or encrypted pairing implementation. ATT starts at MTU 23 and may negotiate 517; values are bounded by min(512,MTU-3). `bleReadWrite()` returns a copied write. `bleNotify(data,length)` admits a copied value; `bleNotify(origin)` rejects stale generations. Keep a pending value on `Busy`/`OutputFull`, as the Receiver echo example does. Ok does not prove remote receipt.

The base preference is 80-120 ms; Receiver requests 120 ms (96 units), latency 2 and 5 s supervision timeout. Actual parameters remain peer/controller observed facts, separate from requests. Existing 1 s connection-age/cooldown rules remain. During secure Classic audio the host prioritizes Classic receive work and paces ATT fragments. This is not an RF airtime reservation.

`bleDisconnect(generation,false)` queues an owner-bound asynchronous LE disconnect and holds later advertising; `bleResumeAdvertising()` releases that hold through normal GAP processing. Observe actual disconnected/advertising/new-generation events; Ok is admission. Discard saved origins/generations after `end()`/`begin()`.

## Serial Port Profile (SPP) server

Off by default: `ReceiverConfig.spp.enabled=true` adds one SDP Serial Port record (UUID 0x1101, L2CAP+RFCOMM channel `spp.channel`, default 3, public browse group, profile 1.2, `spp.service_name`, default "GIGA Serial"), EIR UUID 0x1101 and an RFCOMM listener on the existing Classic RFCOMM owner. It requires `enable_a2dp=true` and an encrypted link; one SPP connection at a time. `sppWrite(data,length,accepted)` copies into a fixed TX ring and may accept fewer bytes (`OutputFull` when full, `Busy` when the radio owner holds the lock, `InvalidArgument` when disconnected); `sppRead()` drains a fixed RX ring and freed space returns RFCOMM credits; `sppConnected()` and `sppStats()` report state and byte/drop counters. Rings (512..16384 bytes each, default 2 KiB) are allocated only when enabled. Frames carry at most 127 bytes, use credit flow control (MSC flow control for RFCOMM 1.0 peers), enter the shared Classic control FIFO only when it is otherwise empty, at most one per host turn and spaced by `streaming_frame_interval_ms` (default 4 ms) while A2DP streams. See [SppEcho](examples/SppEcho/SppEcho.ino) and [SPP validation](docs/validation/spp.md): native synthetic checks only, no over-the-air test yet.

## OBEX: Object Push server and Phonebook Access client

Off by default. `ReceiverConfig.obex.opp_enabled=true` adds an OBEX Object Push SDP record (UUID 0x1105, RFCOMM channel `obex.opp_channel`, default 4, OPP 1.1, any format), EIR UUID 0x1105 and an RFCOMM listener on the shared Classic RFCOMM owner; received objects stream to `on_object_begin(name,type,length)`, `on_object_data(ptr,len)` and `on_object_end(ok)` without buffering the whole object. `obex.pbap_enabled=true` advertises a PBAP client record (0x112E) and enables `pbapPullPhonebook(path="telecom/pb.vcf",max_count,offset)` / `pbapPullVcardListing(folder,max_count,offset)` / `pbapAbort()`: the GIGA looks up the connected phone's PSE with its own SDP client, opens an RFCOMM DLC (on the phone's mux, or its own), runs OBEX CONNECT/GET/DISCONNECT and streams vCard 2.1 bytes to `on_phonebook_data`, then one `on_phonebook_end(PbapResult)`. Calls are nonblocking (`Busy` on radio contention or while a pull is active). Callbacks run from `poll()`; a slow `loop()` only delays the next OBEX response. Requires `enable_a2dp=true` and an encrypted link; data frames follow the SPP A2DP-first queue and pacing rules. See [ObexPush](examples/ObexPush/ObexPush.ino), [PhonebookPull](examples/PhonebookPull/PhonebookPull.ino) and [OBEX validation](docs/validation/obex.md): native synthetic checks only, no over-the-air test yet.

## MAP: Message Access client (MCE)

Off by default. `ReceiverConfig.obex.map_enabled=true` enables `mapGetFolderListing(folder="telecom/msg",max_count=1024,offset=0)`, `mapGetMessageListing(folder="telecom/msg/inbox",max_count=1024,offset=0,subject_length=0)`, `mapGetMessage(handle)` and `mapAbort()` on the connected phone's Message Access Server: the GIGA looks up the MAS with its own SDP client (UUID 0x1132; channel, version, MASInstanceID, SupportedMessageTypes and MapSupportedFeatures land in `obexStats()`), opens an RFCOMM DLC, runs OBEX CONNECT (MAS Target), one SETPATH per folder segment, GET (`x-obex/folder-listing`, `x-bt/MAP-msg-listing` with MaxListCount/ListStartOffset/SubjectLength, or `x-bt/message` by handle with attachments off and UTF-8) and DISCONNECT, and streams the XML or bMessage bytes to `on_map_data`, then one `on_map_end(MapResult)` with the response code and the FolderListingSize/MessagesListingSize, NewMessage and MSETime parameters. It shares the PBAP client link: one PBAP or MAP operation at a time (`Busy` otherwise, and on radio contention). No Message Notification Server: MAP adds no SDP record or EIR UUID, and new messages are not pushed (list again). RFCOMM only (a GOEP L2CAP PSM is reported, not used); read-only browsing. Same `enable_a2dp`, encryption, pacing and `poll()` rules as PBAP. See [MessageListing](examples/MessageListing/MessageListing.ino) and [MAP validation](docs/validation/map.md): native synthetic checks only, no over-the-air test with a phone yet.

## Connectionless Peripheral Broadcast (CPB, formerly CSB)

Off by default. `ReceiverConfig.cpb.role = CpbRole::Transmitter` makes the GIGA a Connectionless Peripheral Broadcast transmitter: after boot the owned host sets the page-2 event mask, reserves `cpb.lt_addr` (default 1), writes the synchronization-train parameters, enables CPB (`cpb.packet_types`, default DM1|DH1; interval `cpb.interval_min_slots`..`interval_max_slots`, default 50-100 ms), sends the payload and starts the synchronization train (restarted after each train and after a channel-map change while `continuous_train` is true). `cpbSetData(data,length)` replaces the payload the controller repeats at every CPB instant (up to `cpb.max_payload_bytes`, 1..1024, default 64; longer payloads go out as Set CPB Data fragments). `CpbRole::Receiver` with `cpb.transmitter_address` (as the transmitter prints `RadioStats::address`) scans for that train, enables reception with its clock offset, instant, interval and channel map, and rescans after a CPB timeout; `cpbRead(buffer,capacity,length,&clock)` returns the newest complete message not read yet. `cpbStart()`/`cpbStop()` start or release the role at run time (`start_at_begin=false` waits), and `cpbStats()` reports the state, granted interval, last failing HCI command and counters. Requires `enable_a2dp=true`; A2DP, AVRCP, BLE and the other profiles keep running and share the airtime. Delivery is latest-value: no acknowledgement, retransmission or encryption. Storage (payload once for a transmitter, twice for a receiver) is allocated only when the role is not Off. Synchronization Train Received is parsed in the standard order or the Broadcom order (Service_Data before LT_ADDR, manufacturer 15), and a configured LT_ADDR picks the matching order. See [CpbBroadcast](examples/CpbBroadcast/CpbBroadcast.ino) and [CSB/CPB validation](docs/validation/csb-capability-2026-11-20.md): native kernel and actor checks; on the board only the transmitter sequence has run so far.

## Validation and known limits

Longer playback after the finite dev.12 test produced brief PCM shortages. Historical zero-fault windows below retain their finite scope and do not establish continuous long-term playback. The private first-shortage diagnostic is outside this public package.

| Evidence | Result | Limit |
|---|---|---|
| Dev.17 Classic media progress | All 84 exported fake-wire CPU cases passed; existing expanded/scheduling/combined suites passed 5758/6256/861 checks. Fresh Receiver/VoiceQualification CM7 builds passed | Scoped read-only-control backpressure fix; no field deployment, radio/voice qualification, full-suite or sanitizer PASS |
| Dev.16 AAC retained suffix | Old-source CPU RED reproduced repeated prefix output; strengthened GREEN passed 11/11 frontend cases with first retained bytes=100/Q16=13212 and final=0/0 | Synthetic owned ADTS, real decoder/frontend, fake radio/I2S; not negotiated A2DP AAC or an LHDC continuity fix |
| Dev.16 combined source | Fresh Receiver and VoiceQualification official CM7 builds/static audits passed; 191 library TUs and 223 compile rows each | Final metadata-only package is not separately compiled; no fix-image deployment or voice/controller qualification |
| Separate earlier-source M7 decoder diagnostic | All 28 vector safety cases passed; 26 strict numerical matches, 2 LHDC nonzero mismatches; 8 zero/lifecycle cases passed and 328 provider allocations/frees balanced | Earlier public codec basis, no radio/I2S. LHDC remains QUALITY_UNQUALIFIED; no bits or expectations were relaxed |
| Dev.15 combined source | Receiver and VoiceQualification official CM7 builds and static link audits passed; 17 combined-source ARM64 voice fixtures and focused byte-pacing/HF admission cases passed | CPU fixtures use fake radio/I2S and single-thread reentry; no GIGA voice runtime, controller route, phone call, long-call, acoustic or latency qualification |
| Dev.14 source integration | Paired AAC CPU fixtures: 66 geometries/eight PCM streams; optional voice owner CPU protocol/retirement cases; separate CM7 build | No deployment, MCU heap saving, SCO/call audio, acoustic, latency or continuity-fix claim |
| Dev.13 profile source | 28 portable Android ARM64 CPU cases passed; full official CM7 compilation recorded separately | No dev.13 firmware deployment, phone control interoperability, integrated call audio or audio-cure claim |
| Dev.12 public-code SCHED0 / 3 Mbaud field image | LHDC192/24, fixed1000 request; 600 s host / 598.297 s printed; three BLE cycles / 130 verified512B exchanges; zero recorded audio/RTP faults | Clock CUM FAILED14; total latency, acoustics and all-codec operation unqualified |
| Dev.11 public-code SCHED0 / 3 Mbaud baseline | 60/70-ms profile; 135 verified512B exchanges; RTP/decoder/transport fault counters zero | FAILED59,328 missingPCM frames /309 underrun callbacks and clock CUM11 |
| Actual private Receiver, SCHED1 | LHDC192 natural 90.072132986 s; 46 verified 512-byte GATT exchanges / 60.005 s / MTU 517; 3 physical BLE controls; 43 non-clock fault gates zero | Clock CUM FAILED 2/88, max 22.1229167 us against 20 us; supply unexercised; prior starvation unresolved |
| Actual M7 decoder-only benchmark, SCHED0 | 12 configurations x 20 passes, 3440 successful calls; reference/tone, status, count, deadline and allocation-close gates valid | Capture FRAMING_DEGRADED: 1 partial replay line; later complete retained rows valid; no global PASS |
| Default Receiver build-basis archive, SCHED0 | Official extracted 503-file archive compiled/audited in 104.422 s; 170 library TUs, all six Ops / seven defaults; BIN cf7132ca.../496552 bytes | Final documentation-only ZIP not separately compiled; no default-image field deployment or audio qualification |

Forty coherent printed tracking cohorts matched T=11136 frames, zero mismatched; five remain UNKNOWN across 1 ms print boundaries. All original 20 us / 44 / CUM gates remain mandatory; a sampled/final zero cannot erase cumulative failure. Inclusive timings do not identify a preemptor or prove instrumentation neutrality.

The benchmark covers SBC/SBC-XQ, AAC-LC ADTS/LATM, aptX/HD, Opus 48k stereo 20 ms, LDAC 96 mono/stereo and HE v1 SBR 44.1 mono/stereo. It does not run radio/I2S. See [per-case timing/heap metrics and oracles](docs/validation/dev10-2026-10-26.md). LHDC independent encoded golden remains MISSING; LDAC uses tone checks, aptX HD PCM16 does not establish 24-bit fidelity, HE PS/xHE/other configurations are unqualified, and Opus fixture evidence is not phone interoperability. Apple AAC sender and distinct SBC-XQ sender operation remain unproved.

Phone-family history and all earlier failures stay in [STATUS](docs/STATUS.md) and [BUILD](docs/BUILD.json). Finite zero non-clock faults do not prove RF delivery, physical I2S, low-bit accuracy, acoustics, general continuity or 200 ms total latency. [EXPORT](docs/EXPORT.md) distinguishes versioned development-source packaging from build and runtime evidence.

Original Apache-2.0 project terms and every supplied component notice/license are preserved. [THIRD_PARTY.md](THIRD_PARTY.md) remains authoritative for component terms and existing unresolved grants; this version creates no new grant.

## Operation-mode examples

Use [A2dpOnly](examples/A2dpOnly/A2dpOnly.ino), [BleOnly](examples/BleOnly/BleOnly.ino), or [A2dpBleCoexist](examples/A2dpBleCoexist/A2dpBleCoexist.ino). Set enable_a2dp=false for real BLE-only operation; disabling I2S alone keeps Classic enabled. Existing defaults remain A2DP+BLE. See [mode selection, DAC wiring and nonblocking BLE echo](docs/operation-modes.md).
