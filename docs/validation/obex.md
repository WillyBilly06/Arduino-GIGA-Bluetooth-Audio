# OBEX over RFCOMM: Object Push server and PBAP client (validation)

Status: **implemented, natively verified only.** No over-the-air test was run.
The board was not flashed and Bluetooth on the build VM was not used. Android
"Share via Bluetooth", Windows `fsquirt`, BlueZ `obexctl`, and phone PBAP PSE
interoperability is therefore **designed for but not demonstrated**.

## What was added

| Area | Change |
| --- | --- |
| Public API | `ObexConfig ReceiverConfig::obex` (`src/GigaObex.h`): `opp_enabled=false`, `opp_service_name="GIGA Object Push"`, `opp_channel=4`, `pbap_enabled=false`, `pbap_service_name="GIGA Phonebook"`, `event_buffer_bytes=4096` (2048..16384), `streaming_frame_interval_ms=4`, callbacks `on_object_begin(name,type,length)`, `on_object_data(ptr,len)`, `on_object_end(ok)`, `on_phonebook_data(ptr,len)`, `on_phonebook_end(const PbapResult&)`, `user`. `GigaBredr::pbapPullPhonebook(path="telecom/pb.vcf",max_count=0xFFFF,offset=0)`, `pbapPullVcardListing(folder="telecom/pb",max_count,offset)`, `pbapAbort()`, `obexStats(ObexStats&)`. |
| OBEX core | `src/owned/core/bth_obex.{c,h}` (portable C): request/response parse (CONNECT/DISCONNECT/PUT/GET/SETPATH/ABORT, final bit, response codes), checked header iteration for all four encodings (Name UTF-16BE, Type, Length, Target, Who, Connection ID, Body/End-of-Body, Application Parameters, ...), bounded writer with sticky overflow, strict UTF-8 to UTF-16BE (surrogate pairs) and UTF-16BE to UTF-8 (code-point-safe truncation), application-parameter TLVs, and a whole-packet assembler for RFCOMM fragments. SRM / GOEP 2.0 (OBEX over L2CAP) is not implemented and not advertised. |
| SDP client | `src/owned/core/bth_sdp_client.{c,h}` (portable C): one ServiceSearchAttributeRequest for a UUID16 (attributes 0x0004, 0x0009, 0x0200, 0x0314, 0x0317), continuation reassembly into caller storage (<=32 rounds), Error Response handling, and a parser that finds the RFCOMM channel in a ProtocolDescriptorList (UUID16 or UUID128, DES or DEA alternatives) plus profile version, GoepL2capPsm (reported, ignored), repositories and features. The host had no SDP client before. |
| Endpoints | `src/owned/ObexPort.h` (header-only, like `SppPort.h`): OPP server, PBAP client state machine, the PSE SDP lookup driver, SDP record builders and the actor-to-`loop()` event ring. Foreground methods never call the C core, so the facade links without it. |
| RFCOMM owner | `VoiceProfiles` (shared HFP/HSP/SPP mux) gains an `ObexPort* obex` endpoint: an OPP listener on `opp_channel` with a 7-credit window, an outgoing PBAP DLC (`scn*2 + (initiator?0:1)`), initiator mode (`channelOpened(...,initiator=true)` starts SABM on DLCI 0 for a GIGA-opened PSM 3 channel and retires that mux once idle), DLC-level close on OBEX errors (the mux and other DLCs stay up), and two more round-robin TX slots. |
| RFCOMM kernel | `BTH_RFCOMM_DLC_LIMIT` 3 to 5 (HF+HS+SPP+OPP listeners plus the PBAP DLC); `bth_rfcomm_open_window()` gives an outgoing DLC its own 1..7 credit window (`bth_rfcomm_open()` is unchanged and delegates to it). |
| Actor | OPP/PCE records installed at `begin()` (Classic SDP table 6 to 8 records), EIR UUIDs 0x1105 / 0x112E (EIR list 6 to 8), outgoing PSM 1 SDP-client channel routed to `ObexPort` (never to the SDP server), outgoing PSM 3 when no mux exists, streaming pacing = max(SPP, OBEX interval), a pull fails at once when no Classic ACL is attached. |
| Facade | `GigaBredr` allocates one `ObexSession` (port + RFCOMM config), the event ring, and, only when neither voice nor SPP exists, one dedicated `VoiceProfiles`; with voice or SPP the endpoints join that owner (one mux per peer). Callbacks are delivered from `poll()`. |
| Examples | `examples/ObexPush/ObexPush.ino` (name/type/length begin, streaming CRC-32, per-object result) and `examples/PhonebookPull/PhonebookPull.ino` (pull 5 s after connect or on 'p', count `BEGIN:VCARD`, print `FN`; 'a' aborts). Both A2DP + `GigaQspiBondStorage`. |

### SDP records (exact bytes checked by `test_obex_actor`)

OPP server, handle 0x00010007, channel 4, default name:

```
09 0000 0a 00010007                                   ServiceRecordHandle
09 0001 35 03 19 1105                                 ServiceClassIDList: OBEXObjectPush
09 0004 35 11 35 03 19 0100 35 05 19 0003 08 04 35 03 19 0008   L2CAP, RFCOMM(4), OBEX
09 0005 35 03 19 1002                                 BrowseGroupList: public
09 0006 35 09 09 656e 09 006a 09 0100                 LanguageBase
09 0009 35 08 35 06 19 1105 09 0101                   Profile: OPP 1.1 (no GoepL2capPsm: RFCOMM only)
09 0100 25 10 "GIGA Object Push"                      ServiceName
09 0303 35 02 08 ff                                   Supported Formats: any
```

PBAP client (PCE), handle 0x00010008: ServiceClassIDList 0x112E, public browse
group, LanguageBase, profile PBAP 0x1130 v1.1, ServiceName "GIGA Phonebook". A
PCE has no protocol descriptor. Version 1.1 is advertised deliberately: a 1.2
PCE must send PbapSupportedFeatures and could be offered GOEP 2.0 behaviour
that is not implemented.

## Design decisions

* **One RFCOMM mux per peer.** The existing L2CAP rule allows one PSM 3 channel
  per ACL, so OPP and PBAP are DLCs on the same `VoiceProfiles` owner as
  HFP/HSP/SPP. When the phone already has a mux (it connected HFP/SPP), the PBAP
  DLC is opened on it (non-initiator DLCI = 2*scn+1). Otherwise the actor opens
  its own PSM 3 channel, the owner starts the mux as initiator (DLCI = 2*scn) and
  closes it when nothing uses it. A peer-opened PSM 3 channel that is still
  configuring is waited for; a retired mux tombstone is authorized for reuse only
  when none of its frames remain in the Classic control FIFO (same rule as a peer
  reconnect).
* **No full-object buffering; backpressure is the OBEX response.** Each DLC
  reassembles one packet (advertised OBEX maximum 1024 bytes) and the packet is
  processed only when the event ring can hold every record it produces (data in
  240-byte records, plus begin/end reserves). Until then the response is not sent,
  and an OBEX peer may not send the next request before it: a slow `loop()`
  slows the sender, nothing is dropped. RFCOMM credits therefore return as soon as
  a frame is copied (no retained L2CAP input that could fence A2DP media). A
  packet larger than 1024 bytes or bytes beyond the current packet are protocol
  errors: the DLC is disconnected, the object/pull fails, the mux stays up.
* **Callbacks off the radio thread.** The actor only copies records into the
  ring; `GigaBredr::poll()` pops up to 16 records per call under `TryLock` and
  invokes the sketch callbacks with no lock held, then wakes the host so freed
  space can release the waiting response.
* **A2DP first.** OBEX data frames use the SPP bulk path: produced only when no
  other RFCOMM output is queued, one per host turn, admitted to the shared Classic
  control FIFO only when all three per-peer slots are free, and spaced by
  `streaming_frame_interval_ms` while A2DP streams. SDP-client requests and
  L2CAP connects use the same reservation discipline as the AVRCP controller.
* **OPP semantics.** CONNECT without Target is accepted (Target = another
  service: 0xD3); max packet < 255 is refused (0xC0). PUT streams `Name`/`Type`/
  `Length` into one begin, Body/End-of-Body into data, final PUT ends ok. A final
  PUT with no body header (OBEX delete) gets 0xC3; GET/SETPATH get 0xD1. ABORT,
  DISCONNECT, a malformed packet (0xC0), DLC loss or ACL loss end an open object
  with `ok=false`. One object at a time.
* **PBAP semantics.** CONNECT with the PBAP Target UUID; the Connection ID from
  its response is the first header of every new request (continuation GET packets
  are bare `83 00 03`); GET `x-bt/phonebook` with Name =
  path and application parameters MaxListCount, ListStartOffset, Format = vCard
  2.1; continuation GETs; PhonebookSize is reported. `pbapPullVcardListing`
  SETPATHs each folder segment then GETs `x-bt/vcard-listing` with an empty Name.
  Success/failure both send OBEX DISCONNECT, then DISC the DLC. `pbapAbort()`
  sends ABORT at the next OBEX turn (or tears down before OBEX). Deadlines: 20 s
  per setup stage, 60 s for the CONNECT response (phones prompt their user),
  30 s per later response. The PSE channel is cached per ACL owner.
* **Busy semantics.** Foreground calls use the radio mutex `TryLock` (`Busy`).
  A second pull while one is active is `Busy` until its end has been queued and
  its DLC closed; a pull without a connected phone fails with `response=0`.
* **Storage.** Nothing unless OPP or PBAP is enabled. Then (CM7, measured with
  the core's arm-none-eabi-g++ 7.2): `ObexPort` 4332 bytes (two 1024-byte packet
  assemblers, two 512-byte TX packets, 640-byte SDP attribute buffer, records),
  the event ring (`event_buffer_bytes`, default 4096) and, without voice/SPP,
  one `VoiceProfiles` (2924 bytes). `VoiceProfiles` grows 2868 to 2924 bytes and
  `bth_rfcomm_t` 92 to 136 bytes from the larger DLC limit (also when OBEX is
  off but voice/SPP is on). No per-packet allocation.

## Verified natively (clang 21, Linux VM)

| Suite | Coverage | Result |
| --- | --- | --- |
| `tests/test_obex_codec.cpp` (C core only) | exact CONNECT/PUT/GET encodings; one-byte/four-byte/text/byte-sequence headers; empty Name; wrong-encoding id, invalid/overlong/surrogate UTF-8 and capacity overflow rejected whole; UTF-16 round trip of U+00E9, U+20AC, U+1F600 (surrogate pair); code-point-safe truncation; unpaired surrogates; CONNECT/SETPATH/PUT/ABORT parse; malformed length, truncated CONNECT, header overrun, length<3, odd UTF-16, truncated 1/4-byte headers; CONNECT success vs failure response layouts; TLVs; reassembly of a 986-byte packet at frame sizes 1/2/3/127/500/whole; oversize, length<3, pipelined bytes and unconsumed packet; SDP client exact request bytes, real-SDP-server response, multi-round continuation, not-found, Error Response, wrong transaction, continuation >16, storage exhaustion, UUID128 in a DEA list, channel 0, truncated element; 40000 deterministic random/mutated packets through every parser | `OBEX_CODEC_SDP_CLIENT_PASS checks=575807` |
| `tests/test_obex_profiles.cpp` (real `VoiceProfiles`, real RFCOMM kernel both ends, real control FIFO, real SDP server) | OPP: CONNECT response bytes; 5000-byte object in 5 PUTs of <=1024 bytes split at N1 127 with UTF-8 name/type/length/data checked; vCard without Length; DISCONNECT and DLC close. Backpressure: 2048-byte ring not drained withholds CONTINUE, then resumes intact. Errors: Target CONNECT 0xD3, delete 0xC3, GET/SETPATH 0xD1, ABORT mid-object, malformed headers 0xC0, oversized packet closes only the DLC, peer DLC drop mid-object. PBAP on the phone's mux and as mux initiator: SDP lookup with continuation, CONNECT Target, GET headers/app-params bytes, multi-packet CONTINUE/continuation GET, PhonebookSize, DISCONNECT, DLC close, cached-PSE second pull without SDP, initiated mux retired when idle; CONNECT refused, ABORT flow, Not Found, silent PSE timeout, malformed response, abort before OBEX, PSE missing from SDP, vCard listing with SETPATH per segment and refused SETPATH; OPP and PBAP DLCs concurrently on one mux; channel/config validation | `OBEX_OPP_PBAP_PROFILES_PASS checks=2358` |
| `tests/test_obex_actor.cpp` (real `OwnedHost`, synthetic HCI, scripted phone) | disabled default: 2 records, EIR {0x110B}, 3 L2CAP services, PSM 3 refused, full-database SDP response byte-identical to the unmodified speaker records, no 0x1105/0x112E match, no outgoing channel over 5 s; enabled: exact OPP/PCE record bytes, 4 records, EIR {0x110B,0x1105,0x112E}, SDP database equals speaker + OPP + PCE, ServiceSearch 0x1105/0x112E; SPP+OBEX 5 records/4 UUIDs; SPP channel collision rejected; OPP push over a phone-opened PSM 3; pacing wiring; ACL loss mid-object; PBAP pull where the actor opens PSM 1 (SDP client, continuation, disconnect) then PSM 3 (initiator mux), full OBEX exchange, idle mux L2CAP disconnect; second pull reuses the cached PSE and re-admits PSM 3 after the retired-mux tombstone; encryption loss fails the pull; pull with no ACL fails immediately | `OBEX_ACTOR_SDP_EIR_OPP_PBAP_PASS checks=1158` |
| `tests/test_obex_facade.cpp` (real `GigaBredr` facade + real actor) | defaults; disabled is Unsupported with no OBEX storage; validation before any allocation (channel 0/31, SPP collision, ring 1024, null/empty names, BLE-only Unsupported); lazy session + dedicated owner; `Busy` while another thread holds the radio mutex; pull without phone ends `ok=false`; OPP push with callbacks invoked from `poll()` on the caller's thread; SPP+OBEX share the SPP owner; zero blocks after `end()` | `OBEX_FACADE_PASS checks=324` |
| The four suites above built with `-fsanitize=address,undefined -fno-sanitize-recover=all` | ASan + UBSan | pass |
| `python3 tools/run_owned_tests.py --sanitize --output ~/gb-tests/obex` (whole native suite, UBSan trap; includes the four OBEX suites and every existing RFCOMM/HFP/HSP/SPP/voice suite) | regression | `EXIT=0` |

Existing-test change: `test_spp_actor` asserted the six-record SDP table limit;
the table is now eight records (OPP + PCE), so it fills the two new slots and
then expects the same `InvalidArgument`. `tools/test_owned_runner_plan.py` gains
a check that the four OBEX suites are in the full runner (with `--asan`).

CM7 builds: see the table appended at the end of this file.

## Not verified / not implemented

* No over-the-air test of any kind (pairing, SDP browse, OPP push from Android,
  Windows or BlueZ, PBAP pull from Android/iOS PSEs, user authorization prompts).
* No throughput or A2DP-coexistence measurement on hardware. With N1 127, a
  7-credit window and one OBEX data frame per host turn, OPP/PBAP should stay well
  below the SPP bound (~31 KB/s while streaming); not measured.
* GOEP 2.0 / OBEX over L2CAP, SRM, OBEX authentication, PBAP 1.2 features
  (PbapSupportedFeatures, vCard 3.0 selection, PropertySelector, vCardSelector,
  database identifiers, `x-bt/vcard` single-entry pull), OPP business-card pull
  (GET) and exchange, PBAP PSE (server) role, MAP/FTP.
* Folded vCard continuation lines and QUOTED-PRINTABLE names are delivered as
  raw bytes; the example only prints `FN` lines.
* BTstack builds: OBEX returns `Unsupported`.

## Suggested over-the-air checks (when the radio is free)

```
# OPP, from Linux (obexd):  obexctl  ->  connect <addr> opp  ->  send /path/file
# OPP, from Android: Share -> Bluetooth -> "GIGA OBEX Push"
# PBAP: connect the phone to the PhonebookPull sketch; accept the contact-access
#       prompt; watch "PBAP complete contacts=..." and repeat during A2DP playback.
sdptool browse <giga-addr>   # expect "GIGA Object Push" (Channel 4) and "GIGA Phonebook"
```

## CM7 builds

`arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 6`
(core `arduino:mbed_giga@4.6.0`, arduino-cli 1.3.1), flash / static RAM in bytes:

| Sketch | Unmodified library | With OBEX | Delta |
| --- | --- | --- | --- |
| `examples/Receiver` (OBEX off) | 604944 / 55896 | 617784 / 55896 | +12840 / 0 |
| `examples/SppEcho` (OBEX off, SPP on) | 592728 / 50856 | 605640 / 50856 | +12912 / 0 |
| `examples/ObexPush` (OPP on) | n/a (new API); vs SppEcho 592728 / 50856 | 605624 / 50696 | +12896 / -160 |
| `examples/PhonebookPull` (PBAP on) | n/a (new API); vs SppEcho 592728 / 50856 | 606504 / 50768 | +13776 / -88 |

The flash cost is paid even with OBEX off because enabling is a runtime
`ReceiverConfig` choice (the OBEX/SDP-client code is reachable from the actor);
static RAM is unchanged. The OBEX heap (see Storage) is allocated only when
enabled.
