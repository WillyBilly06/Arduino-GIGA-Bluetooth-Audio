# dev.18 validation matrix (2026-11-20)

What each claim rests on. "Native" = host build with clang 21 ASan/UBSan on
Linux. "M7 build" = official Arduino Mbed GIGA 4.6.0 Cortex-M7 compile.
"On-board" = run on the physical GIGA R1 (UID 003C00393533510934353531).
"OTA" = real Bluetooth traffic from a sender.

## Codecs

| Codec | Native decode tests | M7 build | On-board OTA decode | Sender used | Not established |
|---|---|---|---|---|---|
| SBC / SBC-XQ | yes (memory, frontend) | yes | yes, 2026-11-20 (0 errors, 0 ms silence) | PipeWire; phone (earlier) | distinct SBC-XQ phone mode |
| AAC-LC | yes | yes | yes, 2026-11-20 (290 kbps, 0 errors) | PipeWire; phone (earlier) | |
| HE-AAC v1 (experimental) | yes | yes | no | - | sender interop; HE-AAC v2/PS not implemented |
| aptX / aptX HD | yes (+ resync after loss) | yes | yes, 2026-11-20 (0 errors; aptX HD fixed in 0.2.2) | PipeWire; phone (earlier) | aptX loss is undetectable (no RTP) |
| LDAC 96 kHz | yes | yes | yes (earlier 8.9 h field run; 2026-11-20 0 errors, 0 ms silence at 30% source loss) | phone, PipeWire | continuity |
| LHDC V5 up to 192 kHz/24 | yes (AOSP-derived fixtures) | yes | yes, 2026-11-20 | PipeWire 1000k | ARM numeric parity; continuity under VM source jitter |
| Opus | yes | yes | yes, 2026-11-20 (262 kbps, 0 errors) | PipeWire | phone interop (phone cannot select) |
| Samsung SSC 44.1/48 HiFi/96 UHQ | yes, bit-exact + malformed input | yes (+33 KB flash, 22,920 B state) | yes, all three modes, 0 errors | PipeWire SSC plugin | Galaxy phone interop, ABR under congestion, 886k frames |

Per-codec OTA run: [codec-matrix-2026-11-20.md](codec-matrix-2026-11-20.md).

M7 decode cost observed on-board: LHDC 192 kHz up to 10.6 ms per 20 ms packet;
SSC up to 3.2 ms per 9-18 ms packet.

## Host / profiles

| Feature | Native | On-board OTA | Not established |
|---|---|---|---|
| Linux/BlueZ connect (auth race fix) | yes (new core tests) | yes | other Linux adapters |
| Fast reconnect (page scan, bonds, QoS) | yes | 0.56-0.74 s from Linux | phone timing re-measure |
| QSPI bond persistence | stub off-target | survives re-flash (6/6) | QSPI write latency |
| SPP server | 5,100+ checks | echo 0 mismatches, ~50 kbit/s | Android apps; heavy SPP during LHDC 192k |
| BLE GATT echo during LHDC 192k | yes | yes (VM central) | phone central |
| BLE coexistence reserve | static_asserts | measured, see ble-coexistence doc | phone-source A/B |
| CSB/CPB | capability dump | transmitter sequence succeeds | reception; public API |
| HID device, OBEX (OPP/PBAP) | see their validation docs | not tested OTA | |
| HFP/HSP (experimental) | yes | earlier limited runs | full call flow |

## Firmware / radio

- Controller: CYW4343W ROM (LMP subversion 0x2209) + stock Arduino HCD patch,
  unchanged. Supports BR/EDR EDR, CPB, Secure Connections (controller); no
  Bluetooth 5.x optional features (no LE 2M, no DLE per Infineon, no LE Audio).
- Broadcom ACL priority VSC 0xFD1A accepted (status 0); its scheduling effect is
  not measured in isolation.

## Physical hardware

- I2S output pins D70/D72/D74 to a PCM5102A were not electrically or
  acoustically measured in this session; no listening tests or analog captures.
- Physical clock rate / jitter not measured; clock compensation reported +/-500
  ppm limits reached with the VM source.
- All OTA tests used one GIGA, one VMware Ubuntu guest with a Realtek
  RTL8922AU USB adapter (which itself resets under VMware's USB 1.1
  emulation), and Firefox/YouTube audio.
