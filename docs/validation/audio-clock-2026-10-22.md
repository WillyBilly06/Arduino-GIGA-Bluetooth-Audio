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

## Historical private132 receiver image

The maintained runtime and Receiver example matched, at that checkpoint, the private all-codec image
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

## Subsequent exact published-image execution

The exact image built from the published dev.4 ZIP was subsequently uploaded:
`ea46fd9890264334138a0c817215bd7dc0a2a374ce5af4237da834ee023457fe`.
All487816 application bytes passed readback. A finite LHDC192/24-bit/nominal1000
selection test then observed20 active status groups spanning38.011 seconds,
19 valid tracking observations, zero recorded audio/protocol errors, applied
120ms BLE parameters, MTU517 and ten exact512-byte acknowledged writes/notification
echoes. The helper closed its own client. Its raw SHA256 is
`d5dd220f5211c859dba8d0f61e400e36773fd0cfeae0d1bab7bfa1614c1b318f`.
This closes execution identity for that exact binary; it does not transfer the
separate132 six-mode or30-minute intervals to it.

Before that upload, the unchanged132 stream also crossed an actual32-bit
microsecond timer rollover. Coherent clock observations advanced from4294472036
to1509370, modular interval2004630us, while epochs13/13 stayed tracking and
recorded PCM/RTP/decoder/DMA/recovery counters remained clean. This is one finite
hardware rollover observation, not a general duration or acoustic guarantee.

## Portable release checksums

The dev.4 checksum artifact used CRLF line endings, which the tested GNU
sha256sum consumer treated as part of the filenames. The ZIP and hexadecimal
hashes were intact. The packaging correction uses `tools/release_checksums.py`
to write literal ASCII/LF bytes and refuse replacement of existing artifacts;
`tests/test_release_checksums.py` covers the exact format and filename guards.
Existing dev.4 assets remain preserved. Later release checksums require a real
GNU `sha256sum --check` pass before publication.

## Later local compressed-queue failure and candidate

The same exact EA public binary later recorded `dropped=2`, `queue_full=2`,
queue high32 and depth27..29 while ordinary LHDC192 playback continued. RTP
arrival sequence/timestamps/inner sequence, decoder, DMA/transport, PCM missing
frames and recovery counters stayed zero. Two960-frame encoded packets were
omitted locally; zero prepared silence did not establish lossless admission.
The first retained capture is `20261022-223151-PublicEA_BeforeSix.serial.bin`;
the90-second continuation retains the two historical drops without additional
increments. Its initial learned target was31196 frames before the drops; a
later target30311 was lower. No observed upward target ratchet is asserted.

An ordered-input actual-frontend model with synthetic decode/DMA timing
reproduces two losses at Q32: delayed35ms batches with4.5ms decode work, and
70ms batches with zero decode work. The exact single-header candidate prevents
both losses at Q32. Q48 with the old header also prevents both; combining them
at Q48 matches that result because its ceiling is inactive at those learned
targets. All27 default profile/skew runs, nine tiny-buffer equivalence pairs and
11 existing native clock cases pass. These are finite models, not reconstruction
of the field timing history or a guarantee against arbitrary bursts.

The candidate reserves ceil(Q/4) estimated packet inventory for queues Q>=4,
retains measured backlog when clipping the learned target, and reapplies the
ceiling after packetization contraction. Q1..3 retain their previous policy.
This estimate does not guarantee free physical slots: exact PCM fill and packet
sizes vary. LHDC's selected compressed queue grows32->48 while startup stays26;
16 additional2050-byte packet slots request32800 extra bytes. Other profiles,
decoders, PLL gains,500ppm limit and10ppm/s slew stay unchanged. Target gates
remain required before publication. All prior source/field receipts are preserved.

## Dev.5 exact public ZIP image and new six-mode execution

The fixed public projection is361 inputs, SHA256
`f6e998f0c25ae44bf4f6a1d46223726493bf41bc4c5541ebe5f0cdddbbf0f827`.
Its fresh extracted468-file source ZIP builds image
`210791643c83b4de1f79ae45c55e741511d392cfd311ad74304956d161b0cf10`.
All487944 bytes passed full exact-UID readback. The initial38.007s window has
20 complete active groups,19 tracking observations and ten exact512-byte GATT
write/notification echoes with MTU517/applied120ms. Raw SHA256:
`f411dd829fa19d6582e74339880ce1fefb2846fa01ca28bdbbbbd64dae744f98`.
That gate has no characteristic-read stage.

The new six-mode matrix and sampled-window companion pass on this exact image:

| Phone codec | Sample rate (Hz) | Active observed span |
| --- | ---: | ---: |
| SBC/SBC-XQ | 44100 | 30.000 s |
| AAC-LC | 44100 | 30.000 s |
| aptX | 48000 | 30.001 s |
| aptX HD | 48000 | 30.007 s |
| LDAC | 96000 | 30.000 s |
| LHDC V5 | 192000 | 30.006 s |

Each naturally completed30-second source has ten exact512-byte ACK writes and
notification echoes, negotiated MTU517 and actual120ms BLE. No observed decoder,
drop/full, PCM missing/recovery, transport/DMA or applicable RTP/detail errors
occurred. Every final active sample extends beyond the source completion poll,
not merely an accepted short prefix. Independent full-raw review also passes with original bytes unchanged. Source/client nonces, original hashes and chunk-time association
are retained. The source evidence does not establish acoustics or every PCM
instant. The new continuous30-minute passive LHDC192/120ms observation also passes, as detailed below.

The physical matrix receipt SHA256 is
`a33f74f8f7d4ea544363b4d86b8dd3ed8b907b94e3c9cb900596106d49cd99c6`;
the separate companion receipt is
`8a19821d747b6140dcb6a4724c1f06597ae66333e7763b7af8d38f82795c4305`.
All previous EA/132 successes and failures remain distinct and retained.

### Matched current Classic-only measurements

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


## Dev.5 exact public-image continuous30-minute observation

Image2107 remains deployed. The new finite interval spans1801.937 host seconds,
1802520 receiver milliseconds and902 complete diagnostic groups, all on stream9,
output9 and physical BLE owner2 with actual96/2/500 parameters. The single serial
capture closed after1807.734 seconds. Raw2616946 bytes/1214 hash-stamped chunks
have SHA256 `d9966a318f9eeafa6ea30502bcb4208f8f7869c6570a224020e516c405c05835`.
The first47 retained lines were excluded by the fresh-baseline rule, not erased.

All monitored media, drop/full, RTP/detail, PCM/recovery/concealment and backend
fault/refusal/invalid counters are zero. Accepted clock updates advance86->1886.
Historical console loss684 stays unchanged throughout; this is not a zero-log-loss
claim for earlier closed-port intervals. Tracked current memory stays245664 bytes.
The observed packet ledger receives360410 and works360409 while queue depth
increases by one, closing the sampled residual at zero. Both decoded sourceA
and real preparedP increase345992640 frames; source backlog is retained rather
than discarded. No new GATT data workload was generated during this passive run.

Four distinct sampled accepted updates report last-transaction wall_overruns=1.
Two-second sampling is not an exact total-overrun counter. The sampled transaction
duration maximum is269038 cycles; cumulative critical interval maximum257916.
At the configured480MHz these are approximately560.496us and537.325us wall
intervals, including any permitted higher-priority preemption. This capture does
not isolate its cause or measure physical clock/IRQ latency. No zero-overrun or
20us wall-time bound is claimed. Media/PCM and acceptance ledgers remain clean.

The original soak receipt SHA256 is
`8c779e81c37689b9978b9340bb506fb144915a2391e9715626217b22fb1d38a2`;
independent full replay/timing review is
`73042b0728ebe75dbccba86d27879e3821c34808aef097c878090e29aa9bc849`.
This qualifies the finite exact2107 interval only: no all-codec30-minute,
continuous-GATT, analog, wire-frequency or permanent-continuity guarantee follows.
