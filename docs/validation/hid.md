# HID Device profile (keyboard / mouse / gamepad): validation

Status: **implemented, natively verified only.** No over-the-air test was run.
The board was not flashed and Bluetooth on the build VM was not used (the radio
was reserved for other measurements). Pairing, SDP browsing and typing against
Windows, Linux (BlueZ `input` plugin), Android and iOS are therefore **designed
for but not demonstrated**.

## What was added

| Area | Change |
| --- | --- |
| Public API | `HidConfig ReceiverConfig::hid` (`src/GigaHid.h`, `enabled=false`); `GigaBredr::hidSend(uint8_t report_id,const uint8_t*,size_t)`, `hidConnected()`, `hidStats(HidStats&)`; output-report callback `HidConfig::output_report` delivered from `poll()`. |
| Descriptors | `src/GigaHidDescriptors.h`: `hid_descriptors::BootKeyboard` (63 B, no report IDs), `Mouse` (buttons/x/y/wheel), `KeyboardMouseCombo` (keyboard ID 1, mouse ID 2, consumer control ID 3), report-ID/size/LED/modifier constants and a US-layout `asciiToKey()`. |
| Endpoint | `src/owned/HidDevice.h` (header-only, like `SppPort.h`): report-descriptor walk (sizes per type/ID), SDP HID + Device ID record builder, HIDP control/interrupt state machine, one control-reply slot, 8-report input queue, per-report value cache for GET_REPORT, one pending output report for the callback. No allocation, no clock. |
| Actor hooks | `OwnedHostConfig::hid` (caller-owned endpoint). `OwnedHost` installs the records, appends EIR UUID 0x1124 and the optional class of device at `begin()`; registers secure PSMs 0x11/0x13 at Classic attach; routes channel open/MTU/close and SDUs by PSM; polls HID after A2DP/AVRCP and voice/SPP; retires HID when the link loses security; handles virtual-cable unplug (bond removal + ACL close). |
| Records table | `ClassicProfiles::records_` 6 to 8 entries (`installHidRecords`); the voice/SPP records keep their previous six-record cap. |
| L2CAP kernel | `BTH_CHANNEL_LIMIT` 6 to 8 (SDP, AVDTP x2, AVRCP, RFCOMM, HID control + interrupt, one spare SDP). `BTH_SERVICE_LIMIT` stays 6 (SDP, AVDTP, AVRCP, RFCOMM, 0x11, 0x13). |
| Facade | `GigaBredr::Impl::hid` allocated only for `hid.enabled`; validation before any allocation; `radioBegin` plumbing through `RadioCallbacks::hid`. BTstack builds return `Unsupported`. |
| Example | `examples/HidKeyboard`: A2DP receiver + combo keyboard/mouse, types a line every 10 s or on a D2 button press, mirrors Caps Lock on the LED, optional mouse jiggle, `GigaQspiBondStorage` bonds. |

## SDP records

HID record handle 0x00010007; Device ID (PnP Information) 0x00010008 unless
`hid.device_id=false`. Exact bytes for `BootKeyboard` with defaults and
`boot_keyboard=true` (checked by `test_hid`):

```
09 0000 0a 00010007                                ServiceRecordHandle
09 0001 35 03 19 1124                              ServiceClassIDList: HID
09 0004 35 0d 35 06 19 0100 09 0011 35 03 19 0011  ProtocolDescriptorList: L2CAP(0x11), HIDP
09 0005 35 03 19 1002                              BrowseGroupList: PublicBrowseRoot
09 0006 35 09 09 656e 09 006a 09 0100              LanguageBase: en, UTF-8, 0x0100
09 0009 35 08 35 06 19 1124 09 0101                Profile: HID 1.1
09 000d 35 0f 35 0d 35 06 19 0100 09 0013 35 03 19 0011   AdditionalProtocolDescriptorLists: L2CAP(0x13), HIDP
09 0100 25 08 "GIGA HID"                           ServiceName
09 0101 25 08 "Keyboard"                           ServiceDescription (omitted when nullptr)
                                                   0x0102 ProviderName only when hid.provider is set
09 0201 09 0111                                    HIDParserVersion 1.11
09 0202 08 40                                      HIDDeviceSubclass (hid.subclass)
09 0203 08 00                                      HIDCountryCode
09 0204 28 01                                      HIDVirtualCable
09 0205 28 00                                      HIDReconnectInitiate
09 0206 35 45 35 43 08 22 25 3f <63 bytes>         HIDDescriptorList (0x22 = report descriptor)
09 0207 35 08 35 06 09 0409 09 0100                HIDLANGIDBaseList: en-US, 0x0100
09 020d 28 01                                      HIDNormallyConnectable
09 020e 28 01                                      HIDBootDevice (boot_keyboard || boot_mouse)
```

Descriptors of 256..512 bytes switch to 16-bit sequence/string lengths
(`36 hhll` / `26 hhll`); covered with a 300-byte descriptor. The deprecated
attributes (0x0200, 0x0208, 0x020B) and the optional ones (0x0209 battery,
0x020A remote wake, 0x020C supervision timeout, 0x020F/0x0210 sniff subrating)
are not published.

Device ID record: SpecificationID 0x0103, VendorID/ProductID/Version from
`vendor_id`/`product_id`/`version` (defaults 0x2341 / 0x0001 / 0x0100),
PrimaryRecord true, VendorIDSource `vendor_id_source` (default 2 = USB-IF).
The Bluetooth HID 1.1 profile expects a Device ID record; it is on by default
when HID is enabled and absent otherwise.

## Design decisions

* **Disabled means unchanged.** With `hid.enabled=false` nothing is allocated,
  the SDP database, EIR UUID list, class of device and L2CAP service table are
  byte-identical to before (checked in the actor and facade suites), and PSM
  0x11 is refused with "PSM not supported".
* **Class of device.** The default stays 0x240414 (audio / loudspeaker) so
  audio sources keep treating the GIGA as a speaker. `hid.class_of_device`
  overrides it; `HidClassOfDevice{Keyboard,Mouse,Combo,Gamepad}` give the
  peripheral major class 0x05 with the Audio and Rendering service bits kept.
  Some hosts (notably iOS and some Windows/Android pickers) only list a device
  as a keyboard or route it to the HID stack with a peripheral class of device;
  others decide from SDP alone. A single class of device cannot be both.
* **Host-initiated connections only.** The owned host has no HCI Create
  Connection path, so the device never pages the host and never opens
  0x11/0x13 itself. `reconnect_initiate` defaults to false (advertised only)
  and `normally_connectable` to true: the host is asked to reconnect. Hosts
  that wait for the device to reconnect (common for keyboards) need the user to
  reconnect from the host side after a GIGA reset. Device-initiated reconnect
  (page + L2CAP connect on an existing ACL) is not implemented.
* **Security.** 0x11 and 0x13 are registered as secure services: on an
  unencrypted link the L2CAP connection waits for authentication/encryption
  (existing `WAIT_SECURITY` path). The actor retires HID admission and closes
  the ACL in the same turn the link stops being secure (`bth_links_secure`),
  and any HID input arriving unsecured requests closure. Same gate as SPP/HFP.
* **Channels.** Control and interrupt may open in either order; the device is
  "connected" (new nonzero `generation`) only with both open. Closing the
  interrupt channel disconnects but keeps control; closing control makes the
  device retire the interrupt channel. A second channel on either PSM is
  refused. Queued reports are discarded at disconnect (`reports_dropped`).
* **A2DP first.** An input report enters the shared Classic control FIFO only
  when all three per-peer slots are free (`ReportReserve=3`, the SPP bulk rule),
  at most one per host turn; a control reply (HANDSHAKE / GET_REPORT data)
  needs two free slots. Signaling, AVDTP, AVRCP and SDP replies therefore never
  queue behind HID output. HID input SDUs are always consumed (never retained
  as L2CAP input) and are eligible for the copied Classic control FIFO, so
  they cannot fence later A2DP media. Reports are small (<=66 bytes) and are
  not additionally paced while streaming.
* **Nonblocking foreground.** `hidSend` uses the radio mutex `TryLock` (Busy,
  retry from `loop()`) like `sppWrite`/`bleNotify`; the queue holds 8 reports
  (`OutputFull` beyond). `hidConnected()` is a lock-free atomic; `hidStats()`
  takes the lock like `sppStats()`. `hidSend` checks the exact input-report
  length from the parsed descriptor and the report ID.
* **Output reports** (SET_REPORT output/feature, interrupt or legacy control
  DATA output) are validated against the descriptor, cached (so GET_REPORT
  returns them) and the latest one is delivered to `output_report` from
  `GigaBredr::poll()` outside the radio lock (never on the radio thread). A
  newer report before delivery replaces the older one (`output_overwritten`).
* **HIDP coverage.** HANDSHAKE from the host ignored; HID_CONTROL NOP, hard /
  soft reset (back to report protocol), SUSPEND / EXIT_SUSPEND (state only,
  input still allowed so the host can be woken), VIRTUAL_CABLE_UNPLUG (bond for
  that host removed, ACL closed); GET_REPORT with optional BufferSize (limit
  applies to report ID + data) answered from the latest sent/set value, zeros
  before the first; SET_REPORT output/feature; GET/SET_PROTOCOL; deprecated
  GET/SET_IDLE answered for old hosts; DATC and reserved types get
  ERR_UNSUPPORTED_REQUEST. Errors: ERR_INVALID_REPORT_ID, ERR_INVALID_PARAMETER.
  One control transaction at a time (a request while a reply is pending is
  counted in `ignored_requests`, as HIDP forbids it).
* **Boot protocol.** Only with `boot_keyboard`/`boot_mouse`, validated at
  `begin()`: keyboard = 8-byte input at report ID 1 (or 0 without report IDs),
  mouse = >=3-byte input at ID 2 (0 for a mouse-only descriptor). In boot
  protocol `hidSend` emits the boot form (ID 1 + 8 bytes, ID 2 + 3 bytes); other
  reports are accepted but not sent (`reports_filtered`). Queued reports are
  discarded on a protocol change. Every connection starts in report protocol.
* **Storage.** Lazy: one `HidDevice` block (2580 bytes on CM7: 800-byte record
  buffer, 80-byte Device ID buffer, 12 cached reports, 8-frame queue) only when
  enabled. In every configuration the heap-allocated actor grows by 112 bytes
  on CM7 (`OwnedHost` 35496 to 35608: two more L2CAP channel slots and their
  per-channel actor state, two more SDP/EIR table entries, the HID pointer).

## Merge notes (hook points)

New files: `src/GigaHid.h`, `src/GigaHidDescriptors.h`, `src/owned/HidDevice.h`,
`tests/test_hid.cpp`, `tests/test_hid_actor.cpp`, `tests/test_hid_facade.cpp`,
`examples/HidKeyboard/`, this file. Small hooks in existing files:
`bth_l2cap_sig.h` (channel limit), `ClassicProfiles.{h,cpp}` (8-entry table,
`installHidRecords`, voice cap kept at six), `OwnedHost.h` (include, config
pointer, `uuids_[8]`, `hidPsm`/`hidPoll`), `OwnedHost.cpp` (begin/end/attach/
detach/closePeer, `deliverImpl`, `safeClassicControl`, three routes in
`synchronizeChannelsImpl`, one `runtimePoll` line, `hidPoll`),
`GigaRadio.h`/`OwnedRadio.cpp` (`RadioCallbacks::hid`), `GigaBredr.{h,cpp}`
(config field, API, lazy session, `poll()` callback delivery),
`tools/run_owned_tests.py` (three suites). Another profile that needs more SDP
records or L2CAP services must budget against the 8-record SDP server limit
(`bth_sdp_init`) and the 6-service table (all six are used with voice/SPP + HID).

## Verified natively (clang 21, Linux VM)

| Suite | Coverage | Result |
| --- | --- | --- |
| `tests/test_hid.cpp` (endpoint + real SDP server + real control FIFO) | descriptor layouts for all three ready-made descriptors; malformed descriptors (truncated, unbalanced, reserved type, mixed numbered/unnumbered, oversized, output-only, ID 0, pop underflow), push/pop and long items; config validation (names, subclass, boot flags vs layout, 512-byte limit); exact HID record bytes, option variants (provider, country, VC, reconnect, connectable, no boot), exact Device ID bytes, 300-byte descriptor with 16-bit lengths; SDP ServiceSearch by 0x1124 / 0x1200 / HIDP 0x0011 and attribute 0x0206 round trip; channel order, owner/MTU/PSM refusal, exact lengths/IDs, 8-report queue + OutputFull, interrupt close/reopen generations, control close retiring interrupt, insecure input | `HID_ENDPOINT_RECORD_LAYOUT_STATE_PASS checks=86` |
| `tests/test_hid_actor.cpp` (real `OwnedHost` actor, synthetic HCI) | disabled: 2 records, EIR {0x110B}, CoD 0x240414, 3 services, PSM 0x11 refused, full-database SDP response byte-identical to the speaker records, no 0x1124/0x1200 match; enabled: 4 records, EIR {0x110B,0x1124} in the actual HCI Write EIR command, CoD override in the actual Write Class of Device command, 5 services, full SDP response equals speaker + HID + Device ID; begin refusals (disabled endpoint, BLE-only); PSM 0x11 waits on an unencrypted link and completes after encryption; encryption off retires HID and closes the ACL; every HIDP transaction listed above incl. BufferSize truncation, boot protocol mapping/filtering, report-ID-less keyboard, deprecated requests; queue order, one report per turn, report blocked with <3 free FIFO slots while replies flow with 2, reply blocked with 1; SDP + HID interleaving while streaming; interrupt/control close by the host, refused second control channel, ACL loss + reconnect (4 connections); virtual cable unplug removes the bond and closes the ACL; HID + SPP: 6 services, 5 records, 3 UUIDs, 7 simultaneously open channels; 8-record table with HF/HS/SPP/HID/DI | `HID_ACTOR_SDP_SECURITY_HIDP_QUEUE_LIFECYCLE_PASS checks=2013` |
| `tests/test_hid_facade.cpp` (real `GigaBredr` facade + real actor) | defaults; disabled is Unsupported with no HID storage and unchanged records/UUIDs/CoD; validation before allocation (BLE-only Unsupported, descriptor/name/subclass/boot errors InvalidArgument); exactly one lazy block when enabled; Busy while another thread holds the radio mutex; exact report frames; 8-report bound and OutputFull; output callback only from `poll()`, retained while the lock is contended, delivered once; disconnect; zero blocks/bytes after `end()` | `HID_PUBLIC_FACADE_REAL_ACTOR_TRYLOCK_LAZY_PASS checks=233` |
| The three suites built with `-fsanitize=undefined,address` | ASan + UBSan | pass |
| `python3 tools/run_owned_tests.py --sanitize --output ~/gb-tests/hid` (whole native suite, UBSan trap, includes the three HID suites and all existing SPP/RFCOMM/AVRCP/voice/BLE suites unchanged) | regression | `EXIT=0` |
| `python3 tools/run_owned_tests.py --sanitize --asan --output ~/gb-tests/hid-asan` | whole suite, ASan + UBSan (run before the final CoD/EIR assertions were added to `test_hid_actor`, which then passed standalone under ASan + UBSan with 2013 checks) | `EXIT=0` |

CM7 builds (`arduino:mbed_giga:giga:target_core=cm7`, core 4.6.0, arduino-cli
1.3.1), flash / static RAM:

| Sketch | Unmodified library | With HID | Delta |
| --- | --- | --- | --- |
| `examples/Receiver` (HID off) | 604944 / 55896 | 611480 / 55896 | +6536 / 0 |
| `examples/A2dpOnly` (HID off) | 567984 / 49624 | 574448 / 49624 | +6464 / 0 |
| `examples/SppEcho` (HID off) | 592728 / 50856 | 599264 / 50856 | +6536 / 0 |
| `examples/HidKeyboard` (HID on: A2DP + QSPI bonds + combo HID) | n/a (new API) | 600096 / 50608 | for scale: `SppEcho` (A2DP + QSPI bonds + SPP) on the same tree is 599264 / 50856 |

The HID code is linked whenever the library is (as SPP is); with HID enabled
the heap additionally holds the `HidDevice` block.

## Not verified

* No over-the-air connection of any kind: no pairing as a keyboard, SDP
  browse, HID channel setup, typing, LED feedback, boot protocol (BIOS/UEFI),
  virtual-cable unplug or reconnect against Windows, macOS, Linux, Android or
  iOS.
* Host behaviour with an audio class of device plus a HID record (whether the
  host offers the keyboard at all) and with the peripheral class-of-device
  override (whether audio sources still offer A2DP) is unknown.
* Reconnect after reset relies on the host reconnecting; hosts that expect a
  keyboard to page them will not reconnect by themselves.
* No airtime or latency measurement of reports during A2DP streaming.
* Reports already committed to the Classic FIFO when a channel closes may still
  be sent to the old remote CID (at most one report and one reply).
* No HID 1.1 qualification; QoS/flow specification options, sniff subrating
  attributes, battery/remote-wake attributes and DATC fragmentation of reports
  larger than the host MTU (replies are truncated to the MTU instead) are not
  implemented. Gamepad use needs a sketch-supplied descriptor (none is shipped).
* BTstack builds: `hid.enabled` returns `Unsupported`.

## Suggested over-the-air checks (when the radio is free)

```
bluetoothctl            # scan on; pair <addr>; trust <addr>; connect <addr>
sdptool browse <addr>   # expect "GIGA HID" (0x1124) and PnP Information (0x1200)
sudo evtest             # pick "GIGA HID Keyboard"; HidKeyboard types every 10 s
xset led named "Caps Lock"   # or press Caps Lock: the GIGA LED follows
```

Then repeat during A2DP playback (watch Receiver audio diagnostics for
underruns), after a GIGA reset (host reconnect with QSPI bonds), on Android
(Settings > Bluetooth, then a text field) and Windows (Add device > Bluetooth),
and with `hid.class_of_device = HidClassOfDeviceCombo` on hosts that do not
offer the keyboard with the default audio class of device.

## Over-the-air smoke test (2026-11-20)

A scratch sketch (HID combo descriptor, A2DP on, QSPI bonds) sent only
all-zero keyboard reports every 500 ms. An Ubuntu 26.04 VM (BlueZ 5.85,
Realtek RTL8922AU) paired and connected the HID profile: the kernel created
"GIGA Audio Keyboard" and "GIGA Audio Mouse" input devices; the GIGA reported
one control+interrupt connection, 103 reports queued and sent, 0 malformed.

Not tested over the air: real key/mouse input content, LED output reports,
boot protocol hosts, unplug, Android/iOS/Windows hosts, and HID alongside an
active A2DP stream.
