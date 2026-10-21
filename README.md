# Arduino GIGA Bluetooth Audio

An Arduino GIGA R1 project for Bluetooth Classic A2DP reception, codec decoding
on the STM32H747 Cortex-M7, and PCM5102A I2S output. The development direction is
a project-owned dual-mode host with BLE limited to the GATT-server role.

**This main branch starts as a documentation scaffold. It is not yet an
Arduino-installable library or a qualified release.** Reviewed, buildable code
will be promoted from develop when its ownership, licensing, tests and target
checks are recorded. No release ZIP or binary is provided by this scaffold.

## Branches and releases

- `main`: reviewed public snapshots. The initial snapshot contains documentation
  and license/audit material only.
- `develop`: ongoing development. Features and tests may be incomplete; an
  unreviewed development branch is not a released firmware image.
- Release tags and library ZIPs will be produced from a reviewed, buildable
  main commit, with SHA-256 checksums and a support/evidence matrix.

## Evidence already obtained in the private development baseline

These observations concern the earlier receiver and isolated diagnostics. That
baseline used a third-party Classic host; they do not qualify the replacement
project-owned host or simultaneous Classic/BLE operation.

| Area | Bounded observations | Remaining limits |
| --- | --- | --- |
| Controller | Physical controller startup, identity and Classic capability diagnostics passed. | Controller readiness is not profile interoperability. |
| Decoder correctness | Independent vectors and host ASAN checks; 12 physical M7 vector cases passed, including SBC/XQ, AAC-LC ADTS/LATM, aptX/HD, Opus, LDAC and experimental HEv1. | LHDC has no independent encoded reference in this collection. Codec-only tests are not phone negotiation. |
| M7 timing | At 480 MHz, the finite HEv1 mono/stereo prefixes had observed maximum decode times of 1747/3383 microseconds, with zero measured deadline misses in that run. | Observed maxima for short fixtures are not bounds for every possible stream or playback soaks. |
| Memory | 220 configuration lifecycle checks completed, including zero tracked residual allocations and immediate allocation-failure cleanup. | Tracked payload counts exclude allocator overhead/system allocations; stack and fragmentation are separate. |
| Phone baseline | SBC 44.1 kHz and LDAC 96 kHz media reached receiver decoding and DMA in the historical host baseline. | Startup/jitter underruns and sustained playback remain separate work. Other sender profiles are unverified. |
| DAC output | Standalone PCM5102A tones were reported audible in the historical output diagnostic. | This does not establish continuous Bluetooth audio, measured analog quality or every channel/rate combination. |
| New owned host / BLE | Development and review are ongoing; native test results are tracked on develop. | Concurrent Classic audio plus BLE GATT-server behavior is not yet hardware-validated. No BLE central/client role is promised. |

The intended I2S wiring is D70 BCLK, D74 WS/LRCK and D72 data, with a common
ground and supply suitable for the actual PCM5102A breakout. Wiring and
installation instructions will accompany a buildable public release; no
firmware or controller patch is distributed in this scaffold.

## Codec and licensing status

The decoder work covers SBC/SBC-XQ, AAC-LC, aptX/HD, LDAC, LHDC V5, Opus and
experimental HE-AAC v1. This list describes development scope, not a promise
that all implementations or media can be redistributed under one license.
HEv2/parametric stereo and xHE-AAC are not implemented or claimed.

Project-owned contributions are under [Apache-2.0](LICENSE). Third-party code
retains its original terms. In particular, LGPL, Helix, Sony/LHDC provenance
questions and the historical host license are not converted to Apache or MIT.
The initial export contains no vendor code or reference media. See
[third-party status](THIRD_PARTY.md), the [public license audit](docs/validation/public-license-audit.md)
and [development notes](docs/DEVELOPMENT.md).

## Contributing

Use develop for proposed changes. A contribution should identify its ownership
and dependencies, preserve third-party notices, include relevant tests, and keep
build, decoder, negotiated-stream, electrical and listening evidence distinct.
Do not add controller firmware, private captures, pairing material, local HE
reference media or workstation-specific paths to public history.
