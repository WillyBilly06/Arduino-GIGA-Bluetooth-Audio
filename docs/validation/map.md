# MAP client (Message Access Profile, MCE role) (validation)

Status: **implemented, natively verified only.** No over-the-air test was run:
the board was not flashed, no phone was used and no Bluetooth adapter was
touched. Interoperability with Android or iOS Message Access Servers (MSE) is
therefore **designed for but not demonstrated**.

The MAP client reuses the OBEX over RFCOMM machinery of the PBAP client
([obex.md](obex.md)): the same SDP client, the same one client DLC, the same
OBEX CONNECT / SETPATH / GET / ABORT / DISCONNECT state machine, deadlines,
event ring, backpressure and `poll()` delivery. The PBAP client was generalized
(one `ObexPort` client link serving either profile), not duplicated.

## What was added

| Area | Change |
| --- | --- |
| Public API | `ObexConfig::map_enabled=false`, `on_map_data(ptr,len)`, `on_map_end(const MapResult&)` (`src/GigaObex.h`). `MapOperation {None, GetFolderListing, GetMessageListing, GetMessage}`, `MapState` (same stages as `PbapState`), `MapResult {operation, ok, aborted, response, listing_size (0xFFFF = not sent), new_message (0xFF = not sent), mse_time[24], bytes}`. `GigaBredr::mapGetFolderListing(folder="telecom/msg", max_count=1024, offset=0)`, `mapGetMessageListing(folder="telecom/msg/inbox", max_count=1024, offset=0, subject_length=0)`, `mapGetMessage(handle)`, `mapAbort()`. `ObexStats` gains `map_enabled`, `map_state`, `map_completed`, `map_failed`, `map_bytes`, `mas_channel`, `mas_version`, `mas_instance_id`, `mas_message_types`, `mas_features`, `mas_l2cap_psm`. |
| SDP client | `bth_sdp_client_set_attributes()` replaces the requested AttributeIDList (a caller-owned DES of at most 32 bytes, set before the first request). The parser also reports MASInstanceID (0x0315, `has_mas_instance`), SupportedMessageTypes (0x0316) and whether 0x0317 was present (`has_supported_features`). The PBAP request bytes are unchanged (default list). |
| Endpoints | `src/owned/ObexPort.h`: the client link carries a PBAP or a MAP operation, one at a time. MAS lookup (UUID16 0x1132, attributes 0x0004, 0x0009, 0x0200, 0x0315, 0x0316, 0x0317), MAS cache per ACL owner (separate from the PSE cache), MAP CONNECT/GET builders, response application parameters, `MapData`/`MapEnd` event records. |
| Facade | `GigaBredr` allocates the OBEX session when any of OPP/PBAP/MAP is enabled; MAP alone uses a dedicated RFCOMM owner exactly like PBAP alone. |
| Example | `examples/MessageListing/MessageListing.ino`: inbox listing 5 s after connect (10 entries, 40-character subjects), `msg` elements printed with handle/sender/date/subject; 'l' list again, 'f' folder listing of `telecom/msg`, 'm' get the first listed message (bMessage lines printed), 'a' abort. A2DP + `GigaQspiBondStorage`, `delay(1)` each loop. |

No change to `OwnedHost`, `VoiceProfiles`, the RFCOMM kernel or the OBEX codec
was needed: the host only sees the generic client-link interface.

## Protocol behaviour

* **SDP.** ServiceSearchAttributeRequest for 0x1132 with the attribute list
  `35 12 09 0004 09 0009 09 0200 09 0315 09 0316 09 0317` (exact bytes checked).
  The first record carrying an RFCOMM channel is used. GoepL2capPsm (0x0200) is
  reported in `ObexStats::mas_l2cap_psm` but not used: the transport is always
  RFCOMM (GOEP 1.1 framing), as for PBAP. The cache is per ACL owner and is
  dropped when the ACL goes away.
* **CONNECT.** Target `bb582b40-420c-11db-b0de-0800200c9a66`, OBEX 1.0, maximum
  packet 1024. When the MSE record publishes MapSupportedFeatures (0x0317, i.e.
  a MAP 1.2+ MSE) the CONNECT also carries the application parameter
  MapSupportedFeatures (0x29) = `0x00000004` (browsing only; no notification
  bits since there is no MNS). A MAP 1.0/1.1 MSE gets no application parameters.
  The Connection ID of the response is the first header of every new request
  (continuation GETs are bare `83 00 03`). 60 s CONNECT deadline (phones ask
  their user), then 30 s per response.
* **SETPATH.** One SETPATH (flags 0x02 "do not create", Name = segment) per
  folder segment from the MAS root, after every CONNECT; leading, trailing and
  repeated '/' are skipped; "" means the root (no SETPATH).
* **Get folder listing.** GET Type `x-obex/folder-listing`, no Name header
  (current folder), application parameters MaxListCount (0x01) and
  ListStartOffset (0x02). FolderListingSize (0x11) is reported as
  `listing_size`.
* **Get message listing.** GET Type `x-bt/MAP-msg-listing`, empty Name (current
  folder after SETPATH), MaxListCount (0x01), ListStartOffset (0x02) and, when
  `subject_length` is 1..255, SubjectLength (0x13). MessagesListingSize (0x12),
  NewMessage (0x0D) and MSETime (0x19) from any response packet are reported in
  `listing_size`, `new_message` and `mse_time` (printable ASCII, truncated to
  23 characters).
* **Get message.** GET Type `x-bt/message`, Name = handle (1..16 hex digits,
  validated at admission), Attachment (0x0A) = 0, Charset (0x14) = 1 (UTF-8). No
  SETPATH: handles are global within the MAS instance.
* **Body, end, errors.** Body / End-of-Body bytes stream to `on_map_data` in
  records of at most 240 bytes; CONTINUE responses get continuation GETs;
  exactly one `on_map_end` follows. Success and OBEX error responses
  (CONNECT refused excepted) send OBEX DISCONNECT, then the DLC is closed.
  `mapAbort()` sends ABORT at the next OBEX turn (or tears down before OBEX);
  the result has `aborted=true`. A malformed response is a protocol error
  (`ObexStats::protocol_errors`), the DLC is closed and the operation fails.
  DLC loss, ACL loss, a missing MAS record and expired deadlines fail it with
  `response=0`.
* **Admission.** `Ok` is admission only. `Busy` while the radio mutex is held
  by the actor or while a PBAP or MAP operation is active (they share one OBEX
  client link). `Unsupported` when MAP is not enabled for this `begin()` (also
  for a BTstack build). `InvalidArgument` for a null/too long (>= 96 bytes)
  folder or a bad handle; `mapAbort()` returns `InvalidArgument` when no MAP
  operation is active (`pbapAbort()` does not abort a MAP operation and vice
  versa).

## MAP off and SDP identity

* With `map_enabled=false` (the default) nothing changes: the existing
  disabled-default identity tests (`test_obex_actor`, `test_obex_facade`) still
  pass unchanged, and the new checks show that OPP+PBAP with MAP on and with MAP
  off produce byte-identical SDP records, EIR UUID lists and full-database SDP
  answers.
* **No MCE SDP record, no EIR UUID.** A MAP client advertises an SDP record only
  for its Message Notification Server (MNS, UUID 0x1133). MNS is not
  implemented, so MAP adds no record and no UUID even when enabled; with MAP
  alone the SDP database equals the default speaker records. The only footprint
  of MAP alone is the RFCOMM owner (PSM 3 service) needed for the outgoing DLC,
  the same as PBAP alone.
* **No push notifications.** Without MNS the MCE never sends
  SetNotificationRegistration and receives no SendEvent: new messages are
  found by calling `mapGetMessageListing()` again (e.g. on a timer, or after
  `new_message=1`).

## Verified natively (clang 21, Linux VM, `--sanitize` UBSan trap)

| Suite | MAP coverage | Result |
| --- | --- | --- |
| `tests/test_obex_codec.cpp` (`map_sdp_client`) | exact MAS request bytes with the custom attribute list; set only before the first request; real SDP server response parsed (channel, version 1.4, GOEP PSM, instance 2, types 0x1F, features present with value 0); multi-round continuation keeps the list; the PSE request still uses the default 30-byte request and returns no MAS attributes; list validation (tag, length, 32-byte bound, null, uninitialized client); wrong-width 0x0315/0x0316/0x0317 ignored | `MAP_SDP_CLIENT_PASS checks=38` |
| `tests/test_obex_profiles.cpp` (real `VoiceProfiles`, real RFCOMM kernel both ends, real SDP server, scripted MSE) | MAP-off identity of `ObexPort` (records/UUIDs equal, MAP alone has none); MapEnd record decoding guards; message listing on the phone's mux and as mux initiator: exact SDP request, MAS fields, exact CONNECT bytes with Target and MapSupportedFeatures, SETPATH telecom/msg/inbox with Connection ID, GET Name/Type/app-params bytes, several packets with CONTINUE and continuation GETs, MessagesListingSize/NewMessage/MSETime, DISCONNECT, DLC close, idle initiated mux retired; cached MAS folder listing (no Name, no SubjectLength, FolderListingSize); GetMessage (no SETPATH, handle Name, Attachment/Charset, 2-packet bMessage); CONNECT refused, refused SETPATH segment, GET 0xC6, ABORT with Connection ID, silent-MSE timeout, malformed response, DLC loss mid-transfer, abort before OBEX, input validation, MAS missing from SDP; MAP 1.0 MSE (no features parameter, MaxListCount 0, oversized MSETime); PBAP and MAP Busy on each other, per-profile abort and caches | `MAP_MCE_PROFILES_PASS checks=1829` (file total 4187) |
| `tests/test_obex_actor.cpp` (real `OwnedHost`, synthetic HCI, scripted phone with a MAS record) | OPP+PBAP SDP/EIR/services byte-identical with MAP on/off; MAP alone: 2 records, EIR {0x110B}, full SDP database = speaker records, no 0x1132/0x1133/0x1134/0x112E match, no outgoing channel while idle; message listing where the actor opens PSM 1 (SDP client) then PSM 3 (initiator mux), exact CONNECT layout, SETPATH x3, GET app params, CONTINUE, DISCONNECT, idle mux L2CAP disconnect; second operation reuses the cached MAS (no SDP channel), ACL loss mid-transfer fails it and drops the cache; request without an ACL fails at once | `MAP_MCE_ACTOR_PASS checks=1006` (file total 2168) |
| `tests/test_obex_facade.cpp` (real `GigaBredr` + actor) | defaults; Unsupported before begin and with MAP off; validation before allocation (ring 1024, BLE-only Unsupported); MAP-only session with no record/UUID; `Busy` for all four calls under radio-mutex contention; InvalidArgument (no active op, null/empty/non-hex/17-digit handle, null folder); Busy while active; one failed result from `poll()` on the caller's thread without a phone; abort before OBEX; MAP with OPP+PBAP keeps 4 records/3 UUIDs and makes PBAP Busy; zero blocks after `end()` | `MAP_MCE_FACADE_PASS checks=141` (file total 468) |
| `python3 tools/run_owned_tests.py --sanitize --output ~/gb-tests/map-owned` (whole native suite) | regression, includes every OBEX/RFCOMM/HFP/HSP/SPP/voice suite | `PASS public synthetic receiver, owned host, HCI/ATT lifecycle, PCM recovery and wake checks; no physical qualification`, exit 0 |

## CM7 builds

`arduino-cli compile --fqbn arduino:mbed_giga:giga` (core
`arduino:mbed_giga@4.6.0`, arduino-cli 1.3.1), flash / static RAM in bytes:

| Sketch | Before MAP (66d7cfc) | With MAP | Delta |
| --- | --- | --- | --- |
| `examples/Receiver` (OBEX off) | 629104 / 55896 | 630544 / 55896 | +1440 / 0 |
| `examples/PhonebookPull` (PBAP on) | 617816 / 50768 | 619512 / 50768 | +1696 / 0 |
| `examples/MessageListing` (MAP on) | n/a (new); vs PhonebookPull 617816 / 50768 | 620376 / 50952 | +2560 / +184 (sketch globals) |

`ObexPort` (heap, only when OPP/PBAP/MAP is enabled) grows from 4332 to 4424
bytes on CM7 (arm-none-eabi-g++ 7.2): the MAP result, MAS cache and operation
fields. No new allocation; the event ring and RFCOMM owner are unchanged.

## Limits and choices (not verified, or deliberately not implemented)

* No over-the-air test: no pairing, MAS SDP browse, user authorization prompt,
  listing or message pull from an Android or iOS phone, and no throughput or
  A2DP-coexistence measurement.
* **MNS not implemented** (no SetNotificationRegistration, no MNS record 0x1133,
  no event reports). New messages are found by polling the listing.
* **One MAS instance.** The first MAS record with an RFCOMM channel is used.
  Phones that publish several instances (for example SMS/MMS and e-mail) are
  reached only through the first one; MASInstanceID and SupportedMessageTypes
  are reported in `ObexStats` so a sketch can see which one it got.
* **Read-only browsing.** Not implemented: PushMessage, SetMessageStatus
  (read/deleted), UpdateInbox, GetMASInstanceInformation, conversation
  listings, owner status, message forwarding, filters (FilterMessageType,
  FilterPeriod*, FilterReadStatus, FilterRecipient/Originator, FilterPriority),
  ParameterMask, FractionRequest, attachments (always off), native charset
  (always UTF-8), GOEP 2.0 / OBEX over L2CAP and SRM, OBEX authentication.
* **Spec choices.** Folder listing sends no Name header (MAP lists the current
  folder); message listing sends an empty Name after SETPATH to the folder
  rather than a child folder name; each operation is its own OBEX session from
  the MAS root (CONNECT ... DISCONNECT, DLC closed), so SETPATH never navigates
  up. MaxListCount defaults to 1024 (the MAP default when the parameter is
  absent) and is always sent. MapSupportedFeatures is sent only to an MSE that
  publishes 0x0317, with the browsing bit only. A handle must be 1..16 hex
  digits (MAP's 64-bit handle); anything else is refused locally. MSETime is
  kept as sent (no time-zone parsing).
* XML and bMessage bytes are delivered raw; the example's attribute scanner does
  not decode XML entities (`&amp;`).
* BTstack builds: MAP returns `Unsupported` like the rest of OBEX.

## Suggested over-the-air checks (when the radio is free)

```
# Pair the phone, connect it to the MessageListing sketch, allow message access
# (Android: "Allow access to your messages"; iPhone: Bluetooth > (i) > Show
# Notifications). Watch "MAP complete op=2 entries=..." and the msg lines,
# type 'm' for the first message, 'f' for folders; repeat during A2DP playback.
sdptool search --bdaddr <phone> 0x1132   # the phone's MAS record(s)
```
