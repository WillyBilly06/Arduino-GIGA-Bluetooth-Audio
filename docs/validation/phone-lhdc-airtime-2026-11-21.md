# RedMagic LHDC V5 900 kbps stutter: phone-side airtime limit (2026-11-21)

Source: RedMagic 11 Pro, LHDC V5 192 kHz / 24-bit, fixed "High Audio Quality (900kbps)",
YouTube music. Sink: GIGA R1 Receiver (owned host, 2 Mbaud UART), board indoors next to a
busy 2.4 GHz environment (home AP on channel 7 at -20 dBm at the phone, neighbour APs on
channels 1/3/6/11, another session's 3-node ESP32 WiFi party soak running).

## Symptom

Audible stutter. Receiver counters: RTP sequence continuous, RTP timestamp jumps of
~27 840 ticks (~140 ms, 29 packets) 0-22 times per minute, packets arriving on time
(arrival delta ~4 ms right at each jump). UART `rx_pauses=0`, `rx_overruns=0`,
`queue_full=0`, `underruns=0`. The phone discards encoded packets before transmission;
`LossConcealment` keeps playback timing but cannot recover audio that was never sent.

## Measurements (same images, minutes apart)

| Condition | Drops/min |
|---|---|
| 900 kbps, scan-off probe image, 14:2x | 22.1 |
| 900 kbps, GIGA AFH assessment off | 12.1 |
| 400 kbps, same image | 0.0 |
| 900 kbps, BLE off + no connected scan (A/B legs) | 3.4 |
| 900 kbps, BLE advertising on (A/B legs) | 0.0, 6.1 |

## Link probe (`-DGIGA_BREDR_LINK_PROBE=1`, `GBR_LINK` lines)

- AFH enabled; the GIGA controller's channel assessment is enabled (and turning it off
  changed nothing): the phone's map stays at the 20-channel minimum, band edges only
  (e.g. `fe07000000000000f03f`).
- Local and remote LMP features both support EDR 3 Mb/s with 3- and 5-slot packets.
- Media L2CAP MTU is already 2048; the phone sends one ~590-byte packet per 5 ms frame,
  which needs a 5-slot exchange (~3.75 ms of every 5.27 ms, ~71% of airtime).

## Conclusion

The 900 kbps stutter in this environment is an air-capacity limit at the phone, not a
GIGA receive stall: halving the bitrate removes it on the same firmware, and BLE
advertising and connected inquiry scan are within run-to-run noise. Mitigations are on
the source/environment side (LHDC Auto/ABR, less 2.4 GHz traffic, distance from the AP).
A same-place, same-time comparison against an ESP32 LHDC sink was not possible during
this test (the ESP32 boards were in use).
