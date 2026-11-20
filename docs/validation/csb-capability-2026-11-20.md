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

## Not done / limits

- Only the transmitter path has run, as a diagnostic probe. There is no public
  CPB API yet (data fragmentation, channel-map handling, receiver procedures,
  timeout/recovery), and no reception has been observed over the air.
- A receiver needs a second CPB-capable device (another GIGA is the natural
  pair). The Linux VM's Realtek adapter capability is unknown.
- CPB has no acknowledgements or retransmission; any audio use needs its own
  repetition/FEC and a codec frame budget per broadcast interval.
- The chip supports no Bluetooth 5.x optional features, so LE periodic
  advertising, LE Audio and Auracast broadcast are not available.
