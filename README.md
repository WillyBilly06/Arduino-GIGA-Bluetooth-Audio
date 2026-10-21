# GigaBredr — development source snapshot

Arduino GIGA R1 WiFi Cortex-M7 library under development: a project-owned
Bluetooth Classic A2DP sink, AVRCP control, BLE GATT **server**, bounded decoder
allocation and PCM5102A I2S output. Project-owned code uses Apache-2.0;
third-party source retains its own licenses and notices.

This develop snapshot is experimental. It is not a qualified audio release.
The owned host's initial startup failure was corrected by a controller-reset
bootstrap change. A full local development image subsequently passed a real
phone GATT probe (MTU 247, ten acknowledged writes and ten exact echoes).
Continuous stable Classic audio with concurrent BLE has not passed validation.
The earlier baseline host's audio captures and the separate decoder Cortex-M7
benchmarks do not qualify this replacement host. See [status](docs/STATUS.md).

This audited source profile includes **SBC and Opus** decoders. AAC/HE-AAC,
aptX/aptX HD, LDAC and LHDC implementation files are excluded; their build
defaults are disabled in this snapshot. Public API codec enum values and
project-owned adapters remain for compatibility, but omitted codecs return
unsupported. Do not enable their build flags in this tree.

Install Arduino Mbed OS GIGA Boards **4.6.0**, copy this folder as the Arduino
library `GigaBredr`, and select GIGA R1 WiFi with the M7 core. Compile
`examples/Receiver/Receiver.ino` or use Arduino CLI:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga --jobs 4 --library . examples/Receiver
```

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

In the subsequent exact-route trial with a 90-second source, the full local
image received, decoded and submitted LHDC PCM to DMA, with queue drops and
underruns. The actor then stopped at 204972 ms on receive-window accounting.
A second GATT probe attempted after that stop timed out; it does not establish
Classic/BLE concurrency. LHDC is excluded from this public profile. These local
results do not qualify SBC/Opus streaming or continuous stable audio here.
The current source adds failure diagnostics and Host Number Of Completed
Packets counters, not a receive-window policy fix.

See [third-party notices](THIRD_PARTY.md), [export policy](docs/EXPORT.md), and
the per-file [source provenance](docs/source-provenance.json). The isolated
Receiver build and synthetic test results are recorded in [build evidence](docs/BUILD.json).
With clang and clang++ on PATH, run `python tools/run_owned_tests.py --sanitize`
to exercise the included synthetic owned-host tests. The exporter
emits an external SHA-256 receipt for the exact snapshot. No local history,
firmware images, private captures, codec media or phone packages are included.
