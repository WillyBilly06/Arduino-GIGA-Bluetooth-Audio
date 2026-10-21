# GigaBredr — development source snapshot

Arduino GIGA R1 WiFi Cortex-M7 library under development: a project-owned
Bluetooth Classic A2DP sink, AVRCP control, BLE GATT **server**, bounded decoder
allocation and PCM5102A I2S output. Project-owned code uses Apache-2.0;
third-party source retains its own licenses and notices.

This develop snapshot is experimental. It is not a qualified audio release.
The owned host's startup and burst-service failures were corrected. A local
development image passed real phone GATT negotiation at MTU 517 and ten exact
512-byte write/notification echoes while LHDC audio was active.
Continuous stable Classic audio with concurrent BLE has not passed validation.
The earlier baseline host's audio captures and the separate decoder Cortex-M7
benchmarks do not qualify this replacement host. See [status](docs/STATUS.md).

This development source profile includes the complete **257-file selected
supplied decoder set**: SBC/SBC-XQ, AAC-LC, experimental HE-AAC v1, aptX/aptX HD,
LDAC, LHDC V5 and Opus. All local decoder build defaults are preserved. The
implementation directories are listed in [the source layout](src/codecs/README.md).
Original project contributions use Apache-2.0. Decoder sources retain their
original notices and conditions, listed in [THIRD_PARTY.md](THIRD_PARTY.md).

## Codec support and verification

| Codec | Decoder evidence | Owned host phone tests |
| --- | --- | --- |
| SBC / SBC-XQ | Independent vectors, ASAN and M7 references | Pending; XQ uses the SBC decoder |
| AAC-LC | ADTS/LATM references, ASAN and M7 | Pending |
| HE-AAC v1 | Experimental mono/stereo reference cases on M7 | No compatible sender established |
| aptX / aptX HD | Independent references, ASAN and M7 | Pending |
| LDAC | Independent mono/stereo references, ASAN and M7 | Pending |
| LHDC V5 | ARM port, allocation checks and real received media; no independent PCM reference | 192 kHz/24-bit at true 1000 kbps; output starvation and pre-RTP discontinuities remain |
| Opus | Independent references, ASAN and M7 | RedMagic has no Opus sender |

The library receives one negotiated A2DP codec at a time. HE-AAC v2 and xHE-AAC
are outside the implemented set; adding an AAC decoder does not make a phone
select that profile.

## PCM5102A wiring

| GIGA Arduino pin | PCM5102A signal |
| --- | --- |
| D70 / PI1 | BCLK / BCK |
| D74 / PB12 | LRCK / WS / LCK |
| D72 / PI3 | DIN |
| GND | GND |

These signals share a J5 row with supply pins. Use the supply accepted by the
actual breakout, 3.3V digital signaling and Philips I2S mode. Output uses 32-bit
stereo slots, preserving 24-bit samples left-aligned. D68/MCLK is unused; the
PCM5102A can use its BCLK PLL. Standalone tones were reported audible; continuous
Bluetooth acoustics remain a separate check.

## Arduino installation and API

Install Arduino Mbed OS GIGA Boards **4.6.0**, copy this folder as the Arduino
library `GigaBredr`, and select GIGA R1 WiFi with the M7 core. Compile
`examples/Receiver/Receiver.ino` or use Arduino CLI:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga --jobs 4 --library . examples/Receiver
```

The versioned development ZIP installs with Arduino IDE's **Sketch > Include
Library > Add .ZIP Library**. Stable releases come from `main`; explicitly
marked development prereleases come from `develop`.

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

Check the return status and diagnostics in the full Receiver example.
`availableCodecs()` reports compiled decoders; `config.codecs` limits phone
negotiation. `CodecBuildConfig.h` controls compilation. Decoder workspaces are
allocated for the current configuration and released on stop/reconfiguration;
immutable tables remain in flash. External queue/assembly allocation can use a
caller-initialized SDRAM provider through `setExternalAllocator()`.

Each codec can override the compressed queue, startup reserve, assembly limit
and PCM ring duration. Zero override fields inherit the global settings; only
the selected profile allocates storage. For example, the capacity-only LHDC test
used these settings (this is not a stable-playback recommendation):

```cpp
auto& lhdc = config.codec_buffers[static_cast<size_t>(giga_bredr::Codec::LhdcV5)];
lhdc.maximum_packet_bytes = 2048;
lhdc.queue_packets = 32;
lhdc.startup_packets = 2;
lhdc.pcm_buffer_ms = 20;
```

`stats().buffers` reports the active resolved profile. PCM storage scales with
the negotiated sample rate, with internal DMA-compatible memory retained for
the output ring. Stop, reconfiguration and allocation failure release the active
storage. Inactive codec settings do not allocate their queues or PCM rings.

BLE defaults to a maximum ATT MTU of 517. The negotiated value is bounded by the
peer; characteristic values are independently limited to 512 bytes. Ordinary
writes and notifications are limited to `min(512, negotiated_mtu - 3)` bytes.
The service is a public, unencrypted GATT server for data/control.

`Receiver` includes bounded nonblocking USB diagnostics and a small BLE echo
diagnostic. `SbcReceiver` limits negotiation to SBC; `I2sTone` is an isolated
output diagnostic. Compilation proves integration only. It does not prove RF
delivery, GATT exchange, PCM pin output, acoustics, synchronization or latency.
Controller firmware is supplied by the installed Arduino core, not included
in this source snapshot. Its licensing remains the core distributor's concern.

The BLE role is a GATT server with a control service, write RX characteristic
and notify TX characteristic. This snapshot implements no BLE audio profile.
Review the example and pin configuration before connecting a DAC. Decoder
work and radio callbacks use bounded queues; allocation and timing diagnostics
do not establish end-to-end playback quality.

The current candidate uses interrupt-driven UART transmission and processes each
complete HCI frame before admitting the next frame from a received burst. This
matches a service-order difference found against the preserved BTstack baseline.
The four-packet receive-window guard remains intact. A finite true-1000-kbps
trial with a 32-packet queue had no local queue drops or transport/decode/DMA
errors and passed concurrent MTU-517/512-byte BLE echoes. Output starvation
remained: the received stream contained fourteen 28-packet inner-sequence and
timestamp discontinuities despite continuous RTP sequence numbers. The source
of those pre-RTP omissions remains under investigation. Earlier quality 0x8007
trials were nominal 900 kbps; 0x8008 selects 1000 kbps. See [status](docs/STATUS.md).

See [third-party notices](THIRD_PARTY.md), [export policy](docs/EXPORT.md), and
the per-file [source provenance](docs/source-provenance.json). The isolated
Receiver build and synthetic test results are recorded in [build evidence](docs/BUILD.json).
With clang and clang++ on PATH, run `python tools/run_owned_tests.py --sanitize`
to exercise the included synthetic owned-host tests. The exporter
emits an external SHA-256 receipt for the exact snapshot. No local history,
firmware images, private captures, codec media or phone packages are included.
