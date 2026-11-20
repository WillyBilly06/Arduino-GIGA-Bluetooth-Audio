# Classic connection setup and reconnect speed (2026-11-20)

## Problems found

1. **Linux/BlueZ could not connect A2DP.** On every accepted Classic link the
   GIGA immediately sent `HCI_Authentication_Requested`. The initiator (BlueZ)
   requested authentication at the same time; the Realtek controller refused
   the host's request with `Command Disallowed (0x0C)`, which Linux maps to
   `EBUSY` and treats as a failed connection (`br-connection-unknown`, AVDTP
   `Device or resource busy`). Android tolerates the race, so phones worked.
2. **Bonds were RAM-only in the examples.** `ClassicBondStorage` is
   application-supplied and `Receiver` supplied none, so every reset or sketch
   upload forced every phone to pair again (a full SSP exchange, ~0.5 s, plus
   the user-visible "couldn't connect" until the stale key was removed).
3. **Slow paging.** The controller default page scan (11.25 ms every 1.28 s)
   adds ~0.64 s on average and up to 1.28 s to every connect.

## Changes

- `bth_security_policy_t::acceptor_auth_delay_ms` (owned host: 300 ms): an
  accepted link waits for the initiator's own authentication. A remote Link Key
  Request / IO Capability Request or successful encryption cancels the local
  request; a profile channel already waiting for security
  (`BTH_CHANNEL_WAIT_SECURITY`) runs it at once
  (`bth_links_expedite_authentication`). Profile channels stay pending until the
  link is secure, so no security is relaxed.
- `bth_gap_config_t` fast-connectable scan: 40 ms page-scan interval, 11.25 ms
  window, interlaced page and inquiry scan. Scans remain off while a Classic
  peer is attached, so streaming airtime is unchanged.
- `GigaQspiBondStorage` (new, opt-in): Mbed `TDBStore` in the last 1 MiB of the
  16 MiB QSPI flash (0xF00000-0xFFFFFF), outside Arduino's `QSPIFormat`
  partitions. Explicit little-endian record format, lazy init, key material
  wiped from stack buffers. Off-target it compiles to a RAM-only stub. Used by
  the `Receiver` example.
- `bth_security_policy_t::qos_poll_latency_us` (owned host: 5000 us): after
  Connection Complete the slave asks the master for guaranteed-service QoS.
  The master may refuse; nothing depends on the result. No measurable gain was
  seen with the BlueZ/Realtek master.
- `GIGA_BREDR_CONNECT_TRACE` (build property, default 0): timestamped HCI
  security events, AVDTP signals and L2CAP channel opens for latency analysis.

## Measurements (GIGA R1 + Ubuntu 26.04 VM, Realtek RTL8922AU USB adapter)

`Device1.Connect()` wall time over D-Bus, reconnect with an existing bond:

| Firmware | Result |
|---|---|
| Before | Connect always failed (EBUSY / EACCES) |
| Auth-race fix only | 1.0-3.3 s |
| + fast page scan, stored bond (VM re-pairing every time) | 1.2-2.4 s |
| + working bond on both sides | 0.67-1.0 s |
| + QoS request | 0.56-0.74 s typical |

GIGA-side timeline of a 564 ms reconnect (from Connection Request): connection
complete +13 ms, AVDTP signaling channel +292 ms (link-key authentication,
encryption, key-size check, L2CAP), Discover/Open +300/+369 ms, media channel
+409 ms, SDP and AVRCP +426/+431 ms.

Bonds survived a full re-flash (6/6 reconnects without pairing).

## Limits

- One Linux host/adapter, which itself resets under VMware's USB 1.1 emulation
  (`command 0x0405 tx timeout`); those resets are excluded from the timings.
- BlueZ only keeps the link key when its adapter is bondable; after the VM
  adapter re-enumerated it had to be set `pairable on` again. Phones were not
  re-measured with this firmware.
- QSPI program/erase latency during pairing was not measured; it is paid on the
  host actor at pairing time, never per packet.
