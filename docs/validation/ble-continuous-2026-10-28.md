# Continuous bidirectional BLE with LHDC, 2026-10-28

The current physical GIGA completed a fixed 30-minute, callback-paced BLE
workload during LHDC V5 192 kHz/24-bit playback. The helper imposed no artificial
submission gap and allowed one outstanding transaction. Each transaction
required both a successful acknowledged write and a full byte-for-byte 512-byte
notification echo before the next sequence could start.

| Observation | Result |
|---|---:|
| Active window requested | 1800000 ms |
| Actual active elapsed time | 1800418 ms |
| Complete write/notification transactions | 1354 |
| Verified application bytes in each direction | 693248 |
| ATT MTU | 517 |
| Actual LE interval / latency / timeout | 120 ms / 2 / 5000 ms |
| Stream/output epoch throughout | 3 / 3 |
| Physical LE generation | 6 |
| Recorded decoder, packet-drop, DMA and transport faults | 0 |
| Recorded RTP gap, missing PCM, recovery and concealment faults | 0 |
| Capture/reader coverage errors | 0 |
| SDK heap current bytes / live allocation count | 389097 / 29 throughout |
| Tracked library requested bytes | 343700 throughout |

All 1354 sequences have separate submission, acknowledgement, full echo and
completion records. The receiver has 1354 corresponding 512-byte echo-admission
records. Independent review matched the saved raw slice to the live reader's
unchanged file, checked the pinned helper and remote DEX hashes, enumerated all
sequence events and checked every complete sampled diagnostic group.

The helper exited and its exact process was absent before cleanup. Two
single-byte controls then observed the real generation-6 disconnect and
advertising recovery; the owned remote helper file was removed. Native jobs
were quiescent. This distinguishes logical client closure from the separately
observed controller lifecycle. The existing audio source and firmware were
not changed by the BLE workload.

The image is
`ad1d2f5e9ff2743fcab45582027f31aa5180cee74abb9c13d5d09b7fd8c1242f`,
510728 application bytes, previously fully readback-verified. It is the private
old-host image with the LHDC 100/110-ms buffer profile and 3-Mbaud UART. It does
not contain the local AAC successor, new examples or the public dev.17 host
progress change. Its evidence must not be transferred to those builds.

The clock timing gate remains unqualified: cumulative overrun observations
rose from 109 to 178 during this window. Zero sampled media faults do not waive
that result. Startup `console_drops` was already 254 and remained 254; there were
no new observed console drops. The workload is finite, sequentially
callback-paced application traffic, not a saturation or maximum-throughput
measurement. Software counters do not establish every intervening instant,
physical acoustic continuity, independent LHDC fidelity or total phone-to-DAC
latency. No HFP call-audio qualification was performed by this test.

The earlier 12-minute window on the same image separately completed 497
transactions. Its stream/output epoch was 2; the later source transition to
epoch 3 was observed before this test and is not counted as uninterrupted
same-epoch playback across both windows.
