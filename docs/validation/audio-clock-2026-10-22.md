# Shared output clock and all-codec validation

The common output path previously had no continuous rate compensation. A lossless
source with a small rate offset reproduced late PCM starvation in the real
frontend/ring model; equal-rate input stayed clean. The unchanged receiver also
accumulated missing PCM after more than 22 minutes while RTP, LHDC numbering,
decoder, queue-full, DMA and transport counters stayed clean. Larger startup
reserves and transition ramps did not correct this balance defect.

The new bounded controller adjusts the exclusively owned PLL2 fractional
numerator. It accounts for actual decoded, discarded and prepared source frames,
missing slots and estimated compressed inventory. It does not modify decoded PCM
values, sample order or codec gains. Its default limit is 500 ppm, slew is at most
10 ppm per second, and decisions occur at most once per second. Requested and
accepted/readback coefficients are distinct from measured physical output rate.
The update masks the DMA/context-switch priorities while leaving priority-zero
HCI UART service available. Serial diagnostic transmission stays in the foreground.

All eight codec IDs remain enabled: SBC (including SBC-XQ geometry), AAC-LC,
aptX, aptX HD, LDAC, LHDC V5, Opus and experimental HE-AAC v1. They use this shared
output path and retain configuration-specific decoder/buffer allocations. Default
20 ms PCM rings meet the controller's four-rounded-DMA-half headroom requirement.
Deliberately smaller legal rings can be ineligible; the public statistics expose
that state and their ordinary output path remains available.

## Executed receiver image

The maintained runtime and Receiver example match the private all-codec image
`1322442572a145d7dd7e82c14f601479383eeeedc6bd882809aba1e08024f62c`.
All 487816 application bytes were readback-verified. The ELF selects the owned
Classic/BLE host, all six decoder adapter families, the original board firmware
and the 32768-byte main stack. All 614 maintained compiler inputs matched the
deployed source when it was promoted. Public-profile compilation and execution
are recorded separately; hardware results cannot be transferred between binary
hashes by renaming an artifact.

A fresh BLE reconnect during LHDC V5 192 kHz playback observed actual owner
2 -> 0 -> 3, initial short connection parameters and then applied 120 ms / latency
2 / 5 s timeout. MTU 517 and ten exact 512-byte writes, notifications and actual
characteristic reads passed. The finite lifecycle window spans 48.008 seconds;
its clock subwindow spans 46.007 seconds. Later active observations extend the
same source epoch to 148.039 seconds with zero new audio/protocol errors. The
owned source completed naturally after 150.068 seconds and released before
ordinary playback resumed. There were 148 accepted updates, no refusals, invalid
observations or clock faults, and no decoded discard or missing-frame increments.

The final six-codec matrix then passed on the same binary:

| Phone mode | Active observed span | Audio source / BLE work |
| --- | ---: | --- |
| SBC, 44.1 kHz | 30.000 s | Natural 30 s source; ten exact 512-byte echoes |
| AAC-LC, 44.1 kHz | 30.004 s | Same |
| aptX, 48 kHz | 30.002 s | Same |
| aptX HD, 48 kHz | 30.006 s | Same |
| LDAC, 96 kHz | 30.003 s | Same |
| LHDC V5, 192 kHz / 24 bit | 30.015 s | Same; nominal 1000 kbps selection |

Each case has 16 complete active diagnostic groups, actual 120 ms BLE parameters,
clock tracking and accepted updates. Recorded decoder/drop, queue-full, missing
PCM, recovery, DMA/transport and applicable RTP/detail error increments are zero.
aptX uses raw transport here; zero RTP counters are not claimed as RTP coverage
for that mode. Codec selectors are confirmed, but selected bitrate is not a
measured RF or encoded bitrate. Complete raw/chunk/source/client evidence is
retained privately, including two earlier checker failures and their separate
reanalysis; no original failed receipt was overwritten.

The continuous LHDC192 observation passed: 1801.937 host seconds, 1802510 receiver milliseconds and 902 complete diagnostic groups. Stream/output epochs and the physical BLE owner stayed unchanged, with actual120 ms parameters. Recorded audio/protocol/concealment/recovery error deltas are zero. This was a passive established BLE connection; the reconnect and GATT workloads above are separate finite tests. The raw SHA-256 is `4ea005534b7e9431e708dc1306dda1321d9e89f5a8176bb273b3186356e67036`. These observations qualify this finite interval on the maintained132 image, not permanent or acoustic quality and not execution of the distinct public-profile binary.

## Decoder and regression boundaries

All 270 decoder-related files match the retained physical-target diagnostic
inputs. The [M7 decoder measurements](codec-target-2026-10-22.md) cover 12 vector
cases, 3440 timed calls and 220 configuration/allocation lifecycles. The new
combined production-frontend model passes nine codec profiles at -100/0/+100 ppm
for 1800 seconds each without missing frames, recovery, drops or PCM value/order
mismatches. Its decoder and DMA timing are synthetic; it is not an encoded-PCM
fidelity or MCU-interrupt measurement. Deliberately tiny-ring ON/OFF cases retain
the ordinary-path behavior rather than hiding their baseline starvation.

The available phone cannot select Opus, HE-AAC or a distinct SBC-XQ mode. These
have decoder/native coverage, not current sender interoperability. LHDC has no
independent encoded PCM oracle. HE-AAC v1 remains experimental; HE-AAC v2,
parametric stereo and xHE-AAC are not implemented. No counter/readback result
establishes analog sound quality, electrical I2S accuracy or permanent absence
of every possible interruption.
