# LHDC V5 192 kHz / BLE coexistence (2026-11-20)

## Rig

- GIGA R1 Receiver (owned host), LHDC V5 192 kHz / 24-bit, PipeWire LHDC
  encoder at quality "1000k" on an Ubuntu 26.04 VMware guest, audio from
  Firefox (YouTube) via PipeWire; Realtek RTL8922AU USB adapter.
- BLE load from the same adapter (LE central): 512-byte GATT write + identical
  512-byte notification echo, back to back (MTU 517), via BlueZ D-Bus.
- Counters from the GIGA serial log, analysed per stream epoch.

## Results

| Firmware | BLE load | Streaming | Starvation events | Missing PCM frames | RTP loss | Max arrival gap |
|---|---|---|---|---|---|---|
| 80/90 ms reserve | none | 104 s | 0 | 0 | 0 | 66 ms |
| 80/90 ms reserve | echo | 408 s | 2 | 28,800 | 0 | 79 ms |
| 130/140 ms reserve | echo | 596 s | 1 | 20,736 | 0 | 127 ms |
| 130/140 ms reserve | none | 696 s | 1-2 | 40,512 | 1 gap (3 pkts) | 134 ms |
| 80/90 ms reserve | 512-B write-without-response every 100 ms | 208 s | many | 17 M | 227 gaps | 5.9 s |

Observations:

- Failures are late delivery, not packet loss: the PCM ring drains to zero and
  recovers after refilling the startup reserve (audible as a ~50-110 ms gap).
- The wider reserve absorbed 100-130 ms delivery gaps that starve the 80 ms
  reserve (first 362 s epoch of the echo run was clean with a 111 ms gap).
- The no-BLE control on the same rig shows comparable starvation, so the
  remaining events in this rig are dominated by the virtualised source
  (guest scheduling stalls of PipeWire's sender), not by BLE.
- A BLE write flood (about 5 KB/s of write-without-response) starves BR/EDR
  for seconds. Here the VM's controller is central on both links and schedules
  both; the GIGA cannot bound the central's LE event length.

## Changes

- `BleConfig::audio_coexistence_reserve_ms`: automatic reserve for >=88.2 kHz
  streams while BLE is enabled. Measured above at 50 ms (130/140 ms for LHDC
  192 kHz). After those measurements the default was raised to 100 ms
  (180/190 ms for LHDC 192 kHz, +154 KB PCM; 150/160 ms for LDAC 96 kHz), which
  covers every gap observed (max 134 ms). If the wider ring cannot be
  allocated, the stream opens with the base reserve and
  `ReceiverStats::coexistence_reserve_fallbacks` counts it. The 100 ms default
  has not yet been measured on the board. Explicit settings are unchanged; 0
  disables.
- Existing host measures retained: Classic receive before ATT, 27-byte paced
  ATT fragments, 80-120 ms LE connection preference, Broadcom ACL priority VSC
  0xFD1A (accepted, status 0). Direction byte is now a build property
  (`GIGA_BREDR_ACL_PRIORITY_DIRECTION`, default 1) for A/B testing.

## Controller facts (from research, see csb-capability doc for measured bits)

- CYW4343W supports no Bluetooth 5.x optional features: no LE 2M PHY and,
  per Infineon staff, no LE Data Length Extension.
- A peripheral cannot cap the central's PDUs per connection event through
  standard HCI.

## Not established

- Phone-source (real-time clock) A/B with the new reserve was not run; the
  user's phone was left untouched. The earlier phone results remain in
  ble-audio-priority.md.
- No listening test or analog capture.
- Statistical confidence is low (single events per window).
- Heavy BLE throughput during 192 kHz LHDC remains unsupported; keep BLE
  traffic light (control/status) while streaming at 1000 kbps.

## Update: 0.2.1 with true 1000 kbps and interleaved BLE A/B

Earlier "1000k" runs above were really ~406 kbps: a WirePlumber rule matched
an old device address, so LHDC stayed at its 400k default. All results in this
section use a verified `LHDC_QUALITY_HIGH1` stream (~1892-byte packets, ~949
kbps received).

Root causes found and fixed in the receiver (0.2.0 / 0.2.1):

| Cause | Symptom | Fix |
|---|---|---|
| Lost RTP packets played without fill | each loss drained the reserve; ~190 ms silent holds every few seconds | `conceal_lost_packets` fills gaps up to 64 packets / 400 ms |
| BLE reserve never applied at 192 kHz (ring did not fit the heap) | reserve silently 80 ms | packed 24-bit ring in D2 SRAM: 184 ms reserve, 224 ms ring |
| Concealed gaps reset clock learning | controller never left Learning, held stale corrections | concealed gaps are not clock errors |
| Source timeline 1.4% slow at start, hundreds of ppm drift (PipeWire on an OS clock) | reserve rebuilt over minutes | `catch_up` resampling (+/-1% beyond 30 ms error) and a 20 ms early resume |
| UART7 FIFO off | controller paused per unread byte | FIFO on; RX pauses 0, overruns 0 |

All lost packets were dropped by the sender: PipeWire skips a packet when its
L2CAP socket is full. Hop counts showed everything handed to the VM adapter
arrived at the GIGA intact.

Interleaved A/B, 0.2.1 Receiver, 12 min continuous LHDC 192k/24/1000k, BLE
link held throughout, 512-byte echo load toggled every 180 s (raw LE ATT
client, 215/216 echoed):

| Phase | Time | BLE writes | Packet loss | Arrivals >50 ms | Silence |
|---|---|---|---|---|---|
| BLE load | 336 s | 183 | 17.6% | 63 | 129.8 ms |
| BLE idle | 378 s | 0 | 20.0% | 125 | 13.7 ms |

- BLE load did not worsen delivery: loss and late arrivals were lower under
  load.
- 123 ms of the load-phase silence is one source outage. In that 2 s window
  only 98 packets arrived and 39 were lost, emptying a full 224 ms ring. BLE
  sent one write in that window.
- The other three silences were 5-8 ms each, split across both phases.

Limits:
- One sender only: a VMware Ubuntu guest whose clock drifts and whose
  adapter sits on emulated USB 1.1.
- No phone-source run.
- A sender outage longer than the 224 ms ring still produces a short
  silence; no receiver can play audio that never arrived.

## Update: 0.2.2 and 0.2.3, 12 min interleaved BLE A/B

Same setup as above except the audio source: a generated 60 s broadband signal
looped with `pw-play` (Firefox/YouTube had stopped resuming over MPRIS). The
raw LE ATT echo client now waits for the VM adapter's slow LE connect (26-37
s) and reconnects after a reset; neither run had an LE link drop.

| Receiver | Phase | Time | BLE writes | Packet loss | Silence |
|---|---|---|---|---|---|
| 0.2.2 | BLE load | 342 s | 181 | 24.9% | 95.8 ms (2 windows) |
| 0.2.2 | BLE idle | 372 s | 0 | 23.1% | 211.5 ms (3 windows) |
| 0.2.3 | BLE load | 338 s | 182 | 10.5% | 17.4 ms (1 window) |
| 0.2.3 | BLE idle | 378 s | 0 | 15.8% | 0 ms |

Echo: 214/214 (0.2.2) and 209/209 (0.2.3) 512-byte writes echoed, 0 corrupt.

0.2.2 diagnosis: silence was not BLE-related; there was more of it with BLE
idle. In every silent window the audio timeline was complete: received plus
concealed packets covered the full 2 s, with no unfilled gaps. The PCM level
had fallen to ~70 ms after earlier sender stalls (arrival gaps up to 160 ms),
and the next stall emptied it. At 192 kHz/24 the PCM ring is capped at 224 ms
by D2 SRAM, and the target was 184 ms.

0.2.3 fix (`ReceiverConfig::queue_reserve`, on by default, >= 88.2 kHz with
`catch_up`): the level target also counts up to 100 ms held as compressed
packets in the existing packet queue (LHDC 192k: 28 slots, 1.6 KB / 11.5 ms
each, at most a third of the slots used). No extra memory is allocated. The
target was 54,528 frames (284 ms), and the queue peaked at 15/28 slots with
no overflow. The one 17 ms event came 2 minutes after the stream started,
while catch-up was still building the reserve. Latency at 192 kHz rises by
up to 100 ms.

High-rate OTA recheck on 0.2.3, with a non-silent signal:
- LDAC 96k/24: 812 kbps, 19.6% loss, 0 errors, 0 ms silence.
- SSC 96k/24: 597 kbps, 0 errors, 0 ms silence.
- LHDC 192k/24: 736 kbps, 26.9% loss, 0 errors, 0 ms silence.

Limits:
- Sender loss differed between the two runs (10-16% vs 23-25%), so the
  comparison is indicative, not controlled.
- One VM sender; no phone source.
- A sender outage longer than ~284 ms still produces silence.
