# Serial Port Profile (SPP) server: validation

Status: **implemented, natively verified only.** No over-the-air test was run.
The board was not flashed and Bluetooth on the build VM was not used (the radio
was reserved for other measurements). Linux `rfcomm`/`bluetoothctl`/Python
socket and Android "Serial Bluetooth Terminal" interoperability is therefore
**designed for but not demonstrated**.

## What was added

| Area | Change |
| --- | --- |
| Public API | `SppConfig ReceiverConfig::spp` (`enabled=false`, `service_name="GIGA Serial"`, `channel=3`, `rx_buffer_bytes=2048`, `tx_buffer_bytes=2048`, `streaming_frame_interval_ms=4`); `GigaBredr::sppWrite(const uint8_t*,size_t,size_t& accepted)`, `sppRead(uint8_t*,size_t)`, `sppConnected()`, `sppStats(SppStats&)` (`src/GigaSpp.h`, `src/GigaBredr.h`). |
| Endpoint | `src/owned/SppPort.h`: header-only SDP record builder plus fixed caller-reserved RX/TX byte rings and counters. No allocation, no protocol state. |
| RFCOMM owner | `VoiceProfiles` (the existing HFP/HSP RFCOMM mux owner) gains an optional third DLC slot for SPP. With voice disabled, the facade allocates a dedicated `VoiceProfiles` with no HF/HS listener. With voice enabled, SPP shares the voice mux (one PSM 3 channel per peer). |
| RFCOMM kernel | `BTH_RFCOMM_DLC_LIMIT` 2 to 3 (HF+HS+SPP listeners); per-listener credit window (`bth_rfcomm_set_listener_window`) so SPP gets up to 7 credits while HF/HS keep their 2-frame window. |
| Actor | SPP record installed into the Classic SDP table at `begin()` (table 5 to 6 records), EIR UUID 0x1101 appended; streaming pacing interval pushed into the RFCOMM owner each turn; RFCOMM/SPP admission retired immediately when the Classic link loses security. |

SDP record (handle 0x00010006, channel 3, default name), exact bytes checked by
`test_spp`:

```
09 0000 0a 00010006                         ServiceRecordHandle
09 0001 35 03 19 1101                       ServiceClassIDList: SerialPort
09 0004 35 0c 35 03 19 0100 35 05 19 0003 08 03   L2CAP, RFCOMM(ch 3)
09 0005 35 03 19 1002                       BrowseGroupList: PublicBrowseRoot
09 0006 35 09 09 656e 09 006a 09 0100       LanguageBase: en, UTF-8, 0x0100
09 0009 35 08 35 06 19 1101 09 0102         Profile: SerialPort v1.2
09 0100 25 0b "GIGA Serial"                 ServiceName
```

## Design decisions

* **One RFCOMM mux per peer.** RFCOMM is one L2CAP PSM 3 channel per ACL, and
  the existing signaling policy (`bth_rfcomm_link`) refuses a second one. SPP is
  therefore a DLC on the same owner HFP/HSP already use, not a second mux.
* **Security.** PSM 3 is registered as a secure service, `VoiceProfiles`
  closes on any unsecured input/poll, and the actor now retires the RFCOMM owner
  (port disconnected, writes rejected) in the same turn the Classic link stops
  being secure. Same gate as HFP/HSP (`bth_links_secure`).
* **Credits never over-promise.** Initial/maximum credits are
  `min(7, rx_buffer_bytes / 127)`. Credits are re-granted only for whole N1-byte
  frames that already fit in free ring space, batched once at most half the
  window is outstanding. A peer honoring credits can never overflow the ring.
  RFCOMM 1.0 peers without credit flow get MSC FC=1 at <2 frames free and FC=0
  at >=4 frames free; overflow beyond that is counted (`rx_dropped_bytes`), never
  retained as L2CAP input (which would fence later A2DP media).
* **N1 = 127.** The voice PSM 3 L2CAP service MTU is 136 bytes; SPP shares it,
  so the RFCOMM frame size is the RFCOMM default 127 (peer proposals are reduced
  by PN negotiation; Linux and Android accept this).
* **A2DP first.** An SPP data frame is produced only when no other RFCOMM output
  is queued, at most one per host turn, and it enters the shared Classic control
  FIFO only when all three per-peer slots are free (`BulkReserve=3`): signaling,
  AVDTP, AVRCP and SDP replies never queue behind SPP data, and RFCOMM credit,
  MSC and voice frames may pass a waiting SPP data frame. While A2DP streams,
  frames are spaced by `streaming_frame_interval_ms` (default 4 ms, about
  31 KB/s upper bound). SPP RX is a bounded copy into the ring; it never
  backpressures L2CAP.
* **Nonblocking foreground.** `sppWrite`/`sppRead` use the radio mutex
  `TryLock` (Busy / 0 bytes, retry from `loop()`), like `bleNotify` and
  `bleReadWrite`. `sppConnected()` is a lock-free atomic. `sppStats()` takes the
  lock like `bleStats()`.
* **Lifecycle.** Unsent TX is discarded at disconnect (`tx_dropped_bytes`). RX
  received before a disconnect stays readable until the next DLC opens; unread
  bytes are then discarded (`rx_dropped_bytes`). Each DLC gets a new nonzero
  `generation`.
* **Storage.** Nothing is allocated unless `spp.enabled`: then one session
  (endpoint, 224 B on CM7), the two rings in one block, and (voice disabled) one
  RFCOMM owner (2868 B on CM7). No per-packet allocation.

## Verified natively (clang 21, Linux VM)

| Suite | Coverage | Result |
| --- | --- | --- |
| `tests/test_spp.cpp` (RFCOMM owner + both-end real RFCOMM kernel + real control FIFO) | exact SDP bytes; SDP ServiceSearch by 0x1101, 0x1002 and 0x0003; ServiceSearchAttribute; name/channel/ring validation; PN N1 512 to 127; 7-credit SPP window vs 2-credit HS window; fill-without-read stops the peer at 16x127 bytes with zero drops; batched credit return and intact 4000-byte stream; oversized frame closes without a partial store; partial `write` (2048 of 3000, then `OutputFull`), ring wrap over 9000 bytes, every frame <=127 bytes and <=1 per turn; TX stops at peer credits and resumes; DISC/reconnect (TX dropped, RX kept then dropped on reconnect, new generation); L2CAP retirement and reopen; RFCOMM 1.0 peer with MSC flow control; control-FIFO reserve and credit return passing a waiting data frame; streaming spacing >=4 ms; SPP alongside HSP; channel collisions; insecure input; unregistered SCN refused | `SPP_OWNER_PROTOCOL_PASS checks=3517` |
| `tests/test_spp_actor.cpp` (real `OwnedHost` actor, synthetic HCI) | default config: 2 SDP records, EIR count 1, PSM 3 refused, full-database SDP response byte-identical to the unmodified speaker records, no 0x1101 match; enabled: 3 records, EIR {0x110B,0x1101}, PSM 3 admitted, SDP response equals speaker records + SPP record; voice-channel collision rejected at begin and owner reusable; six-record table limit; actor data path both directions; ACL loss + reconnect; encryption off retires SPP immediately; streaming pacing wiring | `SPP_ACTOR_SDP_EIR_SECURITY_DATA_PASS checks=1279` |
| `tests/test_spp_facade.cpp` (real `GigaBredr` facade + real actor) | defaults; disabled is Unsupported with no SPP storage; config validation before allocation; exactly three lazy blocks when enabled; Busy / 0 bytes while another thread holds the radio mutex; public partial write and ordered drain; disconnect; zero blocks/bytes after `end()` | `SPP_PUBLIC_FACADE_REAL_ACTOR_TRYLOCK_LAZY_PASS checks=348` |
| The three suites above built with `-fsanitize=undefined,address` | ASan + UBSan | pass |
| `python3 tools/run_owned_tests.py --sanitize --output ~/gb-tests/spp` (whole native suite, UBSan trap, includes the three SPP suites and the existing RFCOMM/HFP/HSP/voice suites unchanged) | regression | `EXIT=0` |

The unmodified source tree fails that runner with clang 21 at
`test_actor_service_changed` (`-Wunused-private-field` on
`OwnedHost::capability_query_sent_`, used only under
`GIGA_BREDR_CONNECT_TRACE`). The field is now `[[maybe_unused]]`; with only that
change the original tree also passes (`EXIT=0`).

CM7 builds (`arduino:mbed_giga:giga:target_core=cm7`, core 4.6.0, arduino-cli
1.3.1), flash / static RAM:

| Sketch | Unmodified library | With SPP | Delta |
| --- | --- | --- | --- |
| `examples/Receiver` (SPP off) | 602112 / 55896 | 604944 / 55896 | +2832 / 0 |
| `examples/A2dpOnly` (SPP off) | 565144 / 49624 | 567984 / 49624 | +2840 / 0 |
| `examples/SppEcho` (SPP on) | n/a (new API); vs A2dpOnly 565144 / 49624 | 569624 / 49888 | +4480 / +264 (sketch echo buffer) |

With SPP enabled the heap additionally holds the endpoint session, the two
rings (4096 bytes by default) and, without voice, one RFCOMM owner (2868 bytes);
with voice enabled the existing voice RFCOMM owner grows by 368 bytes
(third DLC slot and per-listener windows).

## Not verified

* No over-the-air connection of any kind: no pairing, SDP browse, RFCOMM
  connect, data transfer or disconnect against Linux BlueZ (`bluetoothctl`,
  `sdptool`, `rfcomm connect`, Python `AF_BLUETOOTH/BTPROTO_RFCOMM`) or Android
  apps.
* No throughput, latency or A2DP-coexistence measurement on hardware. The 4 ms
  streaming spacing and the one-frame-per-turn cap are reasoned bounds, not
  measured airtime.
* RPN replies come from the existing kernel (fixed 9600 8N1 virtual port, any
  proposal rejected); modem/line status (DTR/RTS/RLS) is not exposed.
* HFP+HSP+SPP all enabled at once is covered only for SPP+HSP DLCs together.
* BTstack builds: `spp.enabled` returns `Unsupported`.

## Suggested over-the-air checks (when the radio is free)

```
bluetoothctl        # pair, trust, connect the GIGA (A2DP), then:
sdptool browse <addr>                     # expect "GIGA Serial", Channel: 3
sudo rfcomm connect /dev/rfcomm0 <addr> 3 # then: screen /dev/rfcomm0
python3 -c "import socket;s=socket.socket(socket.AF_BLUETOOTH,socket.SOCK_STREAM,socket.BTPROTO_RFCOMM);s.connect(('<addr>',3));s.send(b'hello');print(s.recv(64))"
```

Then repeat during A2DP playback and watch `SppEcho` stats (`rx_drop`,
`tx_drop`, `credits`) and the Receiver audio diagnostics for underruns.
