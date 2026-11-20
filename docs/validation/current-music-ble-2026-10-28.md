# Current-source MUSIC and BLE window

The private current-source AVRCP probe completed a finite30-minute MUSIC window
with continuous512-byte BLE request/reply transactions. Clock qualification
**failed**. This result does not establish all-feature stability or acoustics.

| Observation | Result |
|---|---|
| Actual active duration | 1800017ms for requested1800000ms |
| Verified transactions | 1294 each direction;662528 payload bytes each direction |
| GATT workload | MTU517, one outstanding, no artificial submission gap |
| Actual LE connection | 120ms interval, latency2, timeout5000ms |
| Music | Owned finite generated stereo MUSIC; LHDC V5,192kHz,24-bit receiver configuration |
| Source monitoring | 1245 read-only provider polls, no observed journal gaps; same owner, track and advancing playback |
| Receiver epochs | Stream2/output2 throughout admitted interval |
| Recorded audio faults | Zero RTP/decoder/transport/queue/PCM-underrun/DMA faults and zero console drops |
| Real PCM progress | 349546368 frames; minimum reserve4032frames, about21ms at192kHz |
| Clock gate | FAILED:52 cumulative overruns in saved active-window ledger; firmware timing-unknown0 |

The exact private image is545144 bytes, SHA-256
`e1fb058f1482c279d5ee3c497ac945928182c1ab942c601f1bcc1a1c8e49387b`.
It uses the current424 production source files plus a private AVRCP/GATT probe,
UART3M and explicit LHDC100/110ms startup/ring. Public defaults remain UART2M
and80/90ms; they were not this physical workload. The stock controller HCD was
unchanged. Full786432-byte application restoration matched SHA-256
`079c1df4c7276c425397a4725a2265dde78523dde0a136ab93b7107c6295a09f`.
Phone codec fields, idle source baseline and live serial reader were restored.

Two analyzer defects are retained separately from firmware failures:

- The original final analyzer expected public-example `GBR_BLE_RX`. The deployed
  probe emits `GBR_AV_REPLY`. Independent review verified all1294 actual receipts,
  device write/notification deltas and phone callback integrity. The original
  failed receipt remains unchanged. An image-bound correction passes six tests
  and the complete saved receipt set.
- The clock parser compared wait1 coverage to every clock result. Firmware
  `results` includes `Unchanged`; coverage counts only started/completed wait1
  transitions. A separate source-derived correction passes seven tests and the
  saved capture. It removes a false coverage UNKNOWN while preserving the failed
  clock outcome. The full receiver capture includes teardown and reaches53
  overruns; that scope is distinct from the active-window ledger's52.

Digital counters do not independently measure RF saturation, physical I2S
waveforms, acoustic quality, numerical fidelity or phone-to-DAC latency. This
workload did not exercise other codecs, AVRCP transport controls or voice.
Those retain separate entries in [the full feature matrix](all-feature-status-2026-10-28.md).

Local raw receipts are retained under `build/dev253-owned-music-coexist-separate-clock-20261028`;
the marker correction is under `build/dev256-probe-echo-contract-20261028` and the
coverage correction under `build/dev261-clock-coverage-counter-semantics-20261028`.
