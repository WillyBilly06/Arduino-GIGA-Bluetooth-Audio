# GigaBredr â€” develop source

Experimental Arduino GIGA R1 WiFi Cortex-M7 library: project-owned Bluetooth
Classic A2DP sink, AVRCP control, concurrent BLE GATT server, bounded decoder
allocation and PCM5102A I2S output. **Development release; measured scope and limits are below.**

## Current all-codec results

The dev.5 queue-headroom image `210791643c83b4de1f79ae45c55e741511d392cfd311ad74304956d161b0cf10`
was built from a fresh extracted Arduino source ZIP and all487944 application
bytes passed readback. All eight codec identities are enabled. Start with the
bundled **Receiver** example for the measured codec-local buffer settings.
Immutable decoder tables stay in flash; decoder and buffer allocations follow
the selected codec/configuration.

The new physical six-mode matrix passed ten exact512-byte GATT write/notification
echoes per mode at MTU517, with actual120ms BLE connection parameters. Each owned
30-second source completed naturally; the complete sampled windows and raw/chunk
receipts were retained. The active observed spans are:

| Phone codec | Sample rate (Hz) | Active observed span |
| --- | ---: | ---: |
| SBC/SBC-XQ | 44100 | 30.000 s |
| AAC-LC | 44100 | 30.000 s |
| aptX | 48000 | 30.001 s |
| aptX HD | 48000 | 30.007 s |
| LDAC | 96000 | 30.000 s |
| LHDC V5 | 192000 | 30.006 s |

Recorded decoder/drop, queue-full, PCM missing/recovery, DMA/transport and
applicable RTP/detail counters stayed zero. aptX uses raw transport here; its
zero RTP counters are not RTP coverage. The companion checks the complete
sampled source window; it does not prove every intervening instant or acoustics.
The continuous LHDC192/120 ms BLE observation also passed: 1801.937 host
seconds, 1802.520 receiver seconds and 902 complete diagnostic groups. Stream
and output epochs and the physical BLE owner stayed unchanged. All monitored
media/backend error counters stayed zero. This was passive established BLE;
the six GATT workloads above are separate finite tests.

Timing diagnostics retain four distinct sampled accepted updates with the
last-transaction wall-overrun flag set; this is not a cumulative event count.
The sampled transaction maximum was269038 cycles and cumulative maximum critical
interval257916 cycles. No tiny wall-time bound or specific preemption cause is
inferred. These accepted updates caused no recorded PCM/media fault in this
interval; acoustics remain unqualified.

The initial38.007-second LHDC192/GATT gate separately passed full readback,
MTU517 and ten512-byte write/notification echoes. It did not run a characteristic
read loop. The earlier private132 image has separately dated reconnect/read
and30-minute evidence; that evidence is not transferred between binary hashes.

All257 selected supplied decoder files remain included. The unchanged decoder
sources separately passed12 M7 vector cases/3440 timed calls and220 allocation
lifecycles. The phone cannot select Opus, HE-AAC or a distinct SBC-XQ mode;
those have separate decoder/native/M7 coverage. HE-AAC v1 is experimental;
v2/PS and xHE-AAC are unsupported. LHDC has no independent encoded-PCM oracle.
Apple-device interoperability, electrical I2S accuracy and acoustics remain
unqualified here. No finite counter test establishes permanent continuity.

[Clock API and limits](docs/AUDIO_CLOCK.md) and [dated validation](docs/validation/audio-clock-2026-10-22.md) record the implementation and evidence.

## Measured Classic comparison

Both private comparison images use the current decoder/output/clock path,
LHDC48/start26 profile and70/72/74/MCLK-1 pins, with BLE disabled in both.
Each ran the same45-second owned LHDC192/24-bit/nominal1000-kbps-selection source.
Both recorded zero media errors in clean40.018/40.008-second tracking windows.

| Matched metric | Project-owned host | Maintained BTstack reference |
| --- | ---: | ---: |
| Arduino reported program bytes | 483872 | 504856 |
| Arduino reported static globals | 54136 | 77120 |
| Active tracked requested allocation bytes | 245664 | 199584 |
| SDK sleep-accounted busy fraction | 89.149% | 88.232% |

Program/static totals and tracked requested allocations measure different things.
The SDK fraction is not exclusive CPU or thread utilization. This finite pair
shows lower program/static counts, higher tracked allocations and no observed
CPU advantage for the owned host. It does not establish total-RAM superiority,
combined-BLE superiority, statistical performance or better acoustic quality.
The original own-run checker failure is preserved; a separate corrected replay
validates startup fields only when their validity flag permits it. The reference
run waits for actual controller readiness before connecting. No historical EA/132
or unmatched old binary supplies these comparison numbers.

## Historical development context

The following retained observations predate the current scheduling retest.

An earlier refresh included the Classic lifecycle/configuration fixes,
transactional HCI credits, one-command signaling SDUs, UART owner wake, PCM
reserve recovery, copied pending ATT requests and LHDC concealment counters.
The latest update also copies valid LE key/parameter requests under command
pressure without falsely stopping Classic; replies queue later in owner poll.
These correctness fixes do not establish a cure for the remaining audio gaps.
The following continuity observations predate the PCM transition ramp.
A recent live LHDC192kHz/24-bit/fixed1000kbps test completed ten512-byte GATT
ACK/exact echoes but added twelve timestamp/inner steps and1.605s prepared
silence. A later45s BLE-disconnected epoch still added three steps/.351s;
advertising remained enabled. Both measured LHDC concealment branches stayed0.
An earlier55s default-pin test completed ten512-byte GATT echoes yet added six
timestamp/inner steps and.794s prepared silence. These unmatched tests do not
establish improvement. See [measured status and limits](docs/STATUS.md).

## Full supplied codec source

SBC work/PCM storage follows selected geometry; Opus CNG history is compacted.
[Memory measurements and limits](docs/validation/codec-memory-geometry.md)
include paired on-board allocation checks and the remaining audio gaps.

All **257 selected supplied decoder files** remain included, with build defaults
enabled: SBC/SBC-XQ, AAC-LC, experimental HE-AAC v1, aptX, aptX HD, LDAC, LHDC V5
and Opus. HE-AAC v2/xHE-AAC and BLE audio are not implemented. The host negotiates
one A2DP codec at a time; compiling a decoder does not make a phone offer it.
[Source layout](src/codecs/README.md) and [third-party notices](THIRD_PARTY.md)
identify component terms and unresolved grants. Apache-2.0 covers original
project contributions, not blanket codec licensing or patent clearance.

Independent decoder/reference and Cortex-M7 tests exist for several codecs;
LHDC still lacks an independent PCM oracle. Finite phone exchanges are not
stable cross-device interoperability or acoustic qualification. New LHDC flags
report right-channel fallback/full-frame mute; zero flags do not prove numerical
PCM fidelity. `docs/BUILD.json` records this export's actual build and tests.

## PCM5102A wiring

| GIGA pin | PCM5102A |
|---|---|
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

Use the supply required by the actual breakout,3.3V digital signaling and
Philips I2S. Stereo slots are32-bit with24-bit samples left-aligned. MCLK is disabled by
default (`-1`); the PCM5102A can use its BCLK PLL. Runtime `config.i2s` selects
hardware-compatible SPI1/2/3 pins, validated against27 board signal routes. Only
MCLK accepts `-1`, which leaves that pad unused. Optional MCLK is256Fs; alternate
pads and MCLK electrical output remain physically unqualified. See
[valid routes and limits](docs/runtime-i2s-pins.md).
Do not infer clock accuracy or physical audio from compilation/counters.

## Build and API

For playback, start with the bundled **Receiver** example in the Arduino IDE. It supplies the measured codec-specific buffer profiles. The compact API sketch below demonstrates setup without that buffer tuning.

Install Arduino Mbed OS GIGA Boards4.6.0, select the M7 GIGA R1 WiFi target and
install this source folder as library `GigaBredr`:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga --jobs 4 --library . examples/Receiver
```

```cpp
#include <GigaBredr.h>
giga_bredr::GigaBredr receiver;
void setup() {
  giga_bredr::ReceiverConfig config;
  config.name = "GIGA Audio";
  config.i2s.bclk = 70;
  config.i2s.data = 72;
  config.i2s.lrck = 74;
  config.i2s.mclk = -1; // Disabled; use68 for the compatible I2S2 MCLK route.
  receiver.begin(config);
}
void loop() { receiver.poll(); delay(1); }
```

Check begin/status in the full Receiver example. `availableCodecs()` reports
compiled decoders; `config.codecs` limits negotiation. `CodecBuildConfig.h`
controls compilation. Codec workspaces are released on stop/reconfiguration.

Per-codec queue/startup/access-unit/PCM overrides allocate only for the selected
profile; zero fields inherit global settings. The Receiver uses LHDC
32/start26/max2048/PCM20ms, LDAC40/start16, SBC12/start4, AAC/HE16/start6,
aptX24/start10, aptX HD32/start15 and Opus16/start7. AAC/HE, aptX/HD and Opus
also select a maximum packet size of 2048 bytes. These measured example profiles
are not global defaults, a latency guarantee or a stable-playback recommendation.
PCM recovery preserves queued sample order and rebuilds its existing reserve
after starvation. A short integer ramp now softens the transition toward zero
and back to received PCM. Uninterrupted PCM is unchanged; initial playback and
stop behavior are unchanged. There is no additional PCM allocation.
Legacy `silent_frames` counts missing input slots, not literal zeros. The example
reports nonzero `synthesized` padding and literal `zero_padding` separately;
their sum equals `starved + held`. `mixed_real` counts received frames processed
by the resume envelope and overlaps real-input accounting. See
[transition details and tests](docs/pcm-transition-ramp.md). Audible improvement
remains unqualified; this cannot restore missing encoded periods or establish
a physical clock mismatch.

A historical 95 s test of an earlier maintained runtime completed all 30 GATT echoes
with no local error/drop increases, but retained 27 timestamp/inner steps and
702336 missing-input slots. The ramp is not a demonstrated cure or a quality
ranking. This separately built public profile has not been flashed.

BLE is a public unencrypted peripheral/GATT server. ATT starts at MTU23 and can
negotiate up to517. The independent512-byte value cap permits512-byte subscribed
updates at MTU517; the limit is
`min(512, negotiated_mtu-3)`. A blocked acknowledged request owns a fixed517-byte
copy before HCI storage retires. Later valid data commands are best effort,
with explicit drops while an older request or unread value waits. Generation-
scoped notification replies reject stale reconnect data. Pairing/encrypted BLE
and central/client roles remain unsupported. No BTstack protocol backend or
controller firmware is distributed; the installed Arduino core supplies firmware.

While secure A2DP is streaming, Classic receive work runs first and ordinary
ATT output defaults to27-byte HCI fragments at least10ms apart. Public BLE data
calls return immediately on radio-owner contention (`Busy` for notify, false
without changing the read output). Security, required control, real credits
and protocol expiry remain prompt. Host limits do not reserve RF airtime.
BLE transfers can take longer while audio is streaming.

The library default preference requests 80..120 ms; the Receiver example
requests a fixed 120 ms (96/96 in 1.25 ms units). One bounded asynchronous request uses
latency2 and a5s supervision timeout after the connection is at least5s old.
`bleStats().connection` and `GBR_BLE_CONN` distinguish requested/submitted
settings from actual controller observations. The phone can choose other
settings; no reassert loop is used. For example, before `begin()`:

```cpp
config.ble.audio_priority = true;
config.ble.audio_acl_fragment_bytes = 27;
config.ble.audio_data_interval_ms = 10;
auto& preference = config.ble.audio_connection_preference;
preference.interval_min = 96; // 1.25ms units: 120ms
preference.interval_max = 96; // 120ms
preference.latency = 2;
preference.timeout = 500;     // 10ms units: 5s
// preference.enabled = false; // Host pacing without the optional request.
```

A historical private readback-verified image observed120ms/latency2 in all45 retained
parameter snapshots per95s capture, each with30 exact512-byte GATT echoes. The deterministic waveform
case had0 timestamp/codec jumps and0 missing PCM; YouTube still had4 jumps and
103680 missing PCM frames. No acoustic cure is claimed. This independently
built public profile is a separate artifact from that deployed private image.
See [policy, tests and measured limits](docs/ble-audio-priority.md).

## Public checks and packaging

With clang/clang++ on PATH:

```sh
python tools/run_owned_tests.py --sanitize --asan
python tools/test_lhdc_concealment.py --sanitize
```

The runner includes the codec-memory and 20-case audio scheduling children.
The tests use synthetic protocol inputs and hardware boundaries; the LHDC test
uses controlled channel outcomes, not an encoded PCM oracle. They cannot prove
RF, DAC pins, acoustics, synchronization or stable coexistence.

See [status](docs/STATUS.md), [export policy](docs/EXPORT.md),
[exact source provenance](docs/source-provenance.json) and
[isolated build evidence](docs/BUILD.json). This develop update contains no raw
captures, device identities, media, private paths, firmware binaries or local
history. It does not update the separate main branch or existing release assets.

Audio clock configuration, coherent diagnostics, automatic small-ring eligibility and nonblocking LE lifecycle requests are documented in [Audio clock and LE control](docs/AUDIO_CLOCK.md). Native models and coefficient readback do not establish physical clock or acoustic qualification.
