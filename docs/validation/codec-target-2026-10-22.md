# Current codec execution on the GIGA M7

All requested codec families remain enabled (`compiled_mask=0x000000ff`):
SBC/SBC-XQ, AAC-LC, aptX/HD, LDAC, LHDC V5, Opus and experimental HE-AAC v1.
This refresh uses the current maintained decoder bytes, including the reviewed
SBC geometry and Opus storage changes; it does not reuse older target passes.

The identity-checked GIGA passed all12 vector cases,20 passes per case and3440
timed decode calls. Every case passed its PCM reference or known-source tone,
output guards, packet/frame counts and same-target CRC repeatability checks.
There were zero observed decode-deadline misses, allocation failures, residual
bytes/blocks, skipped cases or dropped diagnostic logs. DWT advanced at480 MHz.
LDAC uses known-source tone checks; LHDC has no independent encoded vector.

| Vector case | Rate / channels | Decode p50 / observed max us | Active tracked bytes |
| --- | --- | ---: | ---: |
| sbc | 48000 / 2 | 65 / 79 | 2540 |
| sbc-xq | 48000 / 2 | 72 / 80 | 2540 |
| aac-adts | 48000 / 2 | 651 / 676 | 26484 |
| aac-latm | 48000 / 2 | 662 / 687 | 26484 |
| aptx | 48000 / 2 | 415 / 420 | 4412 |
| aptx-hd | 48000 / 2 | 415 / 421 | 4412 |
| opus | 48000 / 2 | 1391 / 1410 | 29868 |
| ldac-stereo-hq | 96000 / 2 | 264 / 275 | 11036 |
| ldac-mono-hq | 96000 / 1 | 132 / 144 | 5708 |
| ldac-mono-mq | 96000 / 1 | 108 / 118 | 5708 |
| he-mono | 44100 / 1 | 1358 / 1753 | 59326 |
| he-stereo | 44100 / 2 | 3010 / 3395 | 89600 |

These are finite decoder-only measurements for the encoded sequences tested,
without radio/I2S/other audio workers. They are not worst-case bounds for every
stream or evidence of continuous analog output. HE mono/stereo use eight ADTS
frames per reopen at44.1 kHz; Opus uses48 kHz stereo20 ms packets. The HE media,
generated local headers and embedded diagnostic firmware stay outside releases.

The separate allocation probe passed220 configuration lifecycles across two
passes, with110 exact first-pass configuration tuples and reversed rate order
in the second pass. Immediate allocation-failure injection, successful real
open, nonzero selected allocation and complete cleanup passed in every case.
Opus covers six frame durations; LHDC covers16/24-bit configurations. This
probe performs no PCM decoding.

| Codec | Distinct configurations | Active tracked payload bytes |
| --- | ---: | ---: |
| SBC/SBC-XQ | 8 | 1340–2540 |
| AAC-LC | 12 | 15476–26484 |
| aptX | 2 | 4412–4412 |
| aptX HD | 2 | 4412–4412 |
| LDAC | 8 | 3148–11036 |
| LHDC V5 | 8 | 12024–42632 |
| Opus | 60 | 17348–37548 |
| HE-AAC v1 | 10 | 59326–89600 |

Static storage, heap metadata and stacks are excluded from those allocation
counts. The main stack is32768 bytes. Both diagnostics use the reviewed
software-only WWDG1 guard, requested/active1 with279620 us timeout; no deliberate
watchdog-expiration/reset-recovery test was performed. Complete latest retained
log replays were qualified; repeated GUARD banners are not additional runs.

The original receiver image
`e69d84fc7bb4edcc91502834ceb89bfeea6a12b003c13f7064e0353f728184ea`
was restored and all474828 application bytes readback-verified. The short
post-restore capture shows LHDC192 kHz streaming/output active with applied BLE
96/2/500 (120 ms), zero timestamp/inner jumps, missing PCM, decoder/drop/DMA or
transport errors. Its cumulative console-drop counter is33; this short check
is playback-restoration evidence, not the earlier zero-log-loss GATT trial.

The separate [six-codec phone/BLE retest](../STATUS.md#current-maintained-source-phone-and-ble-evidence) covers
SBC, AAC-LC, aptX, aptX HD, LDAC and LHDC with established-link GATT traffic.
The current phone cannot select Opus, HE-AAC or separate SBC-XQ, so those sender
interoperability tests remain open. Fresh physical BLE reconnect during an
already-active A2DP session also remains separate from established-link traffic.
HE-AAC v2/parametric stereo and xHE-AAC are not implemented.

Detailed measurements and evidence hashes: [JSON](codec-target-2026-10-22.json).
Private raw/source/readback receipts are under `build/codec-target-20261022/`.
No new receiver-runtime source change was necessary for this execution refresh.
The diagnostic images were removed from the GIGA after the finite tests.
