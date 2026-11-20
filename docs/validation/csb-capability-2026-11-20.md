# Connectionless Peripheral Broadcast (CSB/CPB) capability of the GIGA radio

Question: can the GIGA R1's Murata LBEE5KL1DX (Infineon CYW4343W, BCM43430A1
ROM, LMP subversion 0x2209, stock Arduino HCD patch) do Connectionless Slave
Broadcast (Bluetooth 4.1; renamed Connectionless Peripheral Broadcast in 5.3)?

## Measured on the actual board (2026-11-20)

Read with `GIGA_BREDR_CONNECT_TRACE=1` (`GBR_CAP` lines) after the normal
owned-host boot, i.e. ROM plus the patch the library loads:

| Query | Result |
|---|---|
| Read Local Supported Commands, octets 30/31/32 | `ff ff ff` |
| - octet 31 | Set CPB, Set CPB Receive, Start/Receive Synchronization Train, Set/Delete Reserved LT_ADDR, Set CPB Data, Read Synchronization Train Parameters |
| - octet 32 bit 0 | Write Synchronization Train Parameters |
| Read Local Extended Features page 2 | `7f 0b 00 00 00 00 00 00` |
| - bits 0-3 | CPB Central, CPB Peripheral, Synchronization Train, Synchronization Scan: all set |
| - other bits | Inquiry Response Notification, Generalized Interlaced Scan, Coarse Clock Adjustment, Secure Connections (controller), Ping, Train Nudging |
| Page 1 | `03` (host SSP and LE supported, as the host configures) |

So the controller advertises both CPB roles and both synchronization-train
roles. No RAM patch or ROM modification is needed to try CPB.

## CPB transmitter run on the board

Build with `-DGIGA_BREDR_CONNECT_TRACE=1 -DGIGA_BREDR_CPB_PROBE=1` (diagnostic
only). After boot, with no Classic peer attached, the host issued:

| Step | HCI command | Parameters | Status |
|---|---|---|---|
| 1 | Set Event Mask Page 2 (0x0C63) | sync train complete/received, CPB receive/timeout, peripheral page response timeout, CPB channel map change | 0x00 |
| 2 | Set Reserved LT_ADDR (0x0C74) | LT_ADDR 1 | 0x00 |
| 3 | Set CPB (0x0441) | enable, LT_ADDR 1, DM1/DH1, interval 0x50-0xA0 slots, supervision 0x2000 | 0x00 |
| 4 | Write Synchronization Train Parameters (0x0C78) | interval 0x80-0x100 slots, timeout 0x2A000 slots | 0x00 |
| 5 | Set CPB Data (0x0C76) | LT_ADDR 1, unfragmented, "GIGA!" | 0x00 |
| 6 | Start Synchronization Train (0x0443) | - | 0x00 (Command Status) |
| - | Synchronization Train Complete event (0x4F) | after the train timeout | 0x00 |

So the module transmits Connectionless Peripheral Broadcast with the stock
patch. A receiving GIGA would use Receive Synchronization Train (0x0444) and
Set CPB Receive (0x0442); that side is not yet exercised.

## External corroboration

A Raspberry Pi 3 (same BCM43430A1 ROM, subversion 0x2209) returned Success for
Set Reserved LT_ADDR, Read Synchronization Train Parameters, Set CSB and Set
CSB Data with BlueZ `tools/3dsp` (linux-bluetooth, June 2017:
https://www.spinics.net/lists/linux-bluetooth/msg70762.html). BlueZ's 3dsp tool
also contains a Broadcom-specific workaround for the Synchronization Train
Received event layout (manufacturer 15), which a receiver implementation must
handle.

## Library implementation (2026-11-20)

`ReceiverConfig.cpb` (see `src/GigaCpb.h`) drives a portable host kernel,
`src/owned/core/bth_cpb.{c,h}`, through the owned-host actor
(`src/owned/CpbPort.h`). One CPB command is in flight at a time, and only while
at least three HCI command slots are free, so Classic and BLE control traffic
keeps priority.

| Role | HCI sequence |
|---|---|
| Transmitter start | 0x0C63 page-2 mask (once per HCI Reset) -> 0x0C74 reserve LT_ADDR -> 0x0C78 train parameters -> 0x0441 enable -> 0x0C76 data fragment(s) -> 0x0443 train |
| Transmitter running | 0x0C76 on each `cpbSetData`; 0x0443 again after Sync Train Complete (0x4F) when `continuous_train`, and after CPB Channel Map Change (0x55) |
| Transmitter stop | 0x0441 disable -> 0x0C75 delete LT_ADDR |
| Receiver start | 0x0C63 -> 0x0444 receive train (Command Status, then event 0x50) -> 0x0442 enable with the train's offset/instant/interval/map |
| Receiver running | event 0x51 data (fragments reassembled, newest complete message kept); 0x52 timeout -> 0x0444 again |
| Receiver stop | 0x0442 disable (or wait for the pending train event; there is no cancel command) |

Handled details:
- **Train layout.** Event 0x50 is parsed in the standard order or the Broadcom
  order (Service_Data before LT_ADDR) on manufacturer 15. A configured LT_ADDR
  overrides the guess when only one order matches it.
- **Stop races.** An enable or LT_ADDR reservation that succeeds after `cpbStop()` is undone.
- **Recovery.** `cpbStart()` after a failure releases what was reserved before
  retrying. A loaded payload is resent on every start and after the host's HCI
  Reset.
- **Lost train event.** If the train event never arrives, a guard of scan
  timeout + 2 s rescans.

Native evidence (Linux VM, clang 21, UBSan/ASan):
- `tests/owned_core/test_cpb.c` — 499 checks. Covers:
  - exact parameter bytes of all nine commands
  - both train layouts and the LT_ADDR override
  - fragmentation: 27-byte chunks, and a restart when the payload changes mid-sequence
  - reassembly, bad packets, joining mid-message and overflow
  - scan failure and retry timing, the lost-event guard, sync-loss rescan
  - stop during reserve, enable and scan; failure, then stop and restart
  - controller reset
- `tests/test_cpb_actor.cpp` — 5989 checks. Runs the real `OwnedHost` actor and covers:
  - with CPB off, no CPB command is ever sent
  - both sequences after boot, the payload update and train restart through the actor
  - a Classic peer connecting and securing during broadcast and during reception
  - stop/start, the failed Set CPB recovery, and host restart (full sequence and payload resent)
  - `start_at_begin=false`
  - rejection in BLE-only mode

Both run in `tools/run_owned_tests.py`, which passes in full. `CpbBroadcast`
builds for `arduino:mbed_giga:giga`: 594,112 B flash, 49,640 B RAM.

## Not done / limits

- On the board only the transmitter sequence has run, as the earlier
  diagnostic probe. The library API itself has not yet been flashed.
- No reception has been observed over the air. A receiver needs a second
  CPB-capable device; another GIGA running `CpbBroadcast` with
  `CPB_TRANSMITTER 0` is the natural pair. The Linux VM's Realtek adapter
  capability is unknown.
- How the controller uses multi-fragment payloads over the air (one baseband
  packet per instant or the full set) is untested. Keep payloads within one
  packet of the chosen type (27 bytes for DH1) until that is measured.
- The Broadcom train layout comes from BlueZ `tools/3dsp.c`; this controller's
  actual 0x50 bytes have not been captured yet.
- CPB has no acknowledgements or retransmission; any audio use needs its own
  repetition/FEC and a codec frame budget per broadcast interval.
- The chip supports no Bluetooth 5.x optional features, so LE periodic
  advertising, LE Audio and Auracast broadcast are not available.
