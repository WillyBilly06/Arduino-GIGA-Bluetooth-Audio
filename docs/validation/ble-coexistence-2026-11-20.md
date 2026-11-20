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

- `BleConfig::audio_coexistence_reserve_ms` (default 50): automatic reserve for
  >=88.2 kHz streams grows from 80/90 to 130/140 ms (LHDC 192 kHz, +77 KB PCM)
  and from 50/60 to 100/110 ms (LDAC 96 kHz) while BLE is enabled. Explicit
  buffer settings are unchanged; 0 disables.
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
