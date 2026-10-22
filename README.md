# GigaBredr — develop source

Experimental Arduino GIGA R1 WiFi Cortex-M7 library: project-owned Bluetooth
Classic A2DP sink, AVRCP control, concurrent BLE GATT server, bounded decoder
allocation and PCM5102A I2S output. **Continuous audio is not qualified.**

This refresh includes the current Classic lifecycle/configuration fixes,
transactional HCI credits, one-command signaling SDUs, UART owner wake, PCM
reserve recovery, copied pending ATT requests and LHDC concealment counters.
These correctness fixes do not establish a cure for the remaining audio gaps.
A recent live LHDC192kHz/24-bit/fixed1000kbps test completed ten512-byte GATT
ACK/exact echoes but added twelve timestamp/inner steps and1.605s prepared
silence. A later45s BLE-disconnected epoch still added three steps/.351s;
advertising remained enabled. Both measured LHDC concealment branches stayed0.
See [measured status and limits](docs/STATUS.md).

## Full supplied codec source

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
Philips I2S. Stereo slots are32-bit with24-bit samples left-aligned. D68/MCLK is
unused; the PCM5102A can use its BCLK PLL. **Runtime I2S pin selection and optional
MCLK output have been requested but are not implemented in this snapshot.**
Do not infer clock accuracy or physical audio from compilation/counters.

## Build and API

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
  receiver.begin(config);
}
void loop() { receiver.poll(); delay(1); }
```

Check begin/status in the full Receiver example. `availableCodecs()` reports
compiled decoders; `config.codecs` limits negotiation. `CodecBuildConfig.h`
controls compilation. Codec workspaces are released on stop/reconfiguration.

Per-codec queue/startup/access-unit/PCM overrides allocate only for the selected
profile; zero fields inherit global settings. The included Receiver uses LHDC
32/start26/max2048/PCM20ms, LDAC40/start16 and SBC12/start4. These measured profiles
are not global defaults, a latency guarantee or a stable-playback recommendation.
PCM recovery preserves queued sample order and emits explicitly counted silence
until its existing reserve returns after starvation. It cannot reconstruct
missing encoded periods or establish a physical clock mismatch.

BLE is a public unencrypted peripheral/GATT server. Default ATT MTU517 and the
independent512-byte value cap permit512-byte subscribed updates; the limit is
`min(512, negotiated_mtu-3)`. A blocked acknowledged request owns a fixed517-byte
copy before HCI storage retires. Later valid data commands are best effort,
with explicit drops while an older request or unread value waits. Generation-
scoped notification replies reject stale reconnect data. Pairing/encrypted BLE
and central/client roles remain unsupported. No BTstack protocol backend or
controller firmware is distributed; the installed Arduino core supplies firmware.

## Public checks and packaging

With clang/clang++ on PATH:

```sh
python tools/run_owned_tests.py --sanitize --asan
python tools/test_lhdc_concealment.py --sanitize
```

The tests use synthetic protocol inputs and hardware boundaries; the LHDC test
uses controlled channel outcomes, not an encoded PCM oracle. They cannot prove
RF, DAC pins, acoustics, synchronization or stable coexistence.

See [status](docs/STATUS.md), [export policy](docs/EXPORT.md),
[exact source provenance](docs/source-provenance.json) and
[isolated build evidence](docs/BUILD.json). This develop update contains no raw
captures, device identities, media, private paths, firmware binaries or local
history. It does not update the separate main branch or existing release assets.
