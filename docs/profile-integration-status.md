# Profile integration capability and validation status

This worktree integrates typed AVRCP and copied metadata into the existing owned Classic actor. New HF/HS/RFCOMM files are standalone protocol modules. HFP/HSP remain disabled and unadvertised; there is no public call or microphone registration facade, host RFCOMM channel owner, or SCO audio graph.

| Capability | Source state | Evidence required before a runtime claim |
|---|---|---|
| AVRCP Play, Pause, Stop, Next, Previous | Public typed tap API, bounded paired press/release, generation/request tokens, cancellation, response outcomes | Official ARM build; portable real-core/actor/facade fixtures; approved real phone control interoperability |
| AVRCP metadata | Copied title, artist, album, playing time; valid/truncated masks; copied playback status and observed position | Portable metadata/track-reset regressions; target memory/stack review; real phone metadata interoperability |
| AVRCP volume | Existing owned target path retained; no PCM scaling or outbound SetAbsoluteVolume added | Existing regression suite and any target regression evidence remain separate |
| RFCOMM | Standalone owned C parser, mux/DLC state, PN/MSC, bounded credits/legacy flow; one incoming SCN and two DLC slots | Portable protocol fixtures; missing actual L2CAP admission, durable queue/RX reservation, SDP discovery and owner teardown integration |
| HFP HF | Standalone bounded AT/SLC state, discovered indicators, gains, baseline command encoding and conditional codec negotiation | Portable AT fixtures; missing public facade, RFCOMM stream owner, real AG interoperability, SCO and duplex audio |
| HSP HS | Standalone AT button/ring/gain state; button semantics remain AG dependent | Portable fixtures and missing RFCOMM/SCO/runtime integration |
| Pluggable microphone | Internal serialized voice-owner PCM provider contract; explicit None emits counted zero PCM; Provider starvation remains separately counted | Provider epoch/lifetime fixtures; missing voice worker, public registration lifecycle and actual uplink transport |
| CVSD/mSBC audio | Format/provider contract only; no synchronous stream or encoder/H2 pipeline | Exact stock-firmware route/capability evidence, controller buffers, voice handle/credit lifecycle, codec vectors, deadline/loss handling and actual duplex audio |

The integration does not introduce any HFP/HSP feature switch that could make the incomplete path discoverable. Source presence and successful AT negotiation cannot establish call audio. Codec negotiation does not create an SCO link. No controller VSC, HCD modification or pin remux is inferred from family datasheets.

## Boundary to a complete HF/HS integration

1. Add one authenticated/encrypted PSM3 owner per peer pair, fresh generations, crossed-open handling, bounded durable output and copied AT stream fragments. Reserve RX windows before accepting credits; return credits only when application storage is consumed. Make timeout and disconnect retire the actual L2CAP owner before admitting a fresh session.
2. Bind HF/HS controls to that serialized owner. Timeouts of untagged AT transactions must retire the session so a late OK cannot acknowledge a new transaction. Keep commands explicit; do not issue dial, redial, answer, hangup or HS button actions from reconnect logic.
3. Add synchronous request/completion/change/disconnect handling bound to the existing ACL peer and an independent audio generation. Implement actual SCO RX/TX dispatch, controller buffer/credit policy, packet-status loss handling and bounded deadlines. H4 framing alone already exists.
4. Establish supported stock-HCD voice routing using documented controller evidence. Do not advertise a host synchronous receive pool without actual owned storage. Validate CVSD format and mSBC/H2/codec paths separately.
5. Add the public input/sink lifetime facade and voice worker, serialized provider detach, fresh microphone epochs and explicit None policy. Arbitrate I2S with A2DP through a fresh output epoch before rate changes, then verify actual duplex audio under user-authorized conditions.

## Current verification boundary

Root owns portable device execution, Git changes, USB/ADB/serial and flashing. Root subsequently delegated guarded official compilation to this integration agent. The agent built Android ARM64 API35 CPU fixtures with NDK28.2 and compiled portable profile objects for Cortex-M7 in witness modes0/1; these are compile results. No project executable was run by this agent. Root executed all28 final portable CPU cases with return0, including expiry, metadata/control regressions, scoped retirement, media/HNC progress, stale/valid response IDs, full Classic/AVRCP, facade, RFCOMM/HFP and existing owner regressions. No phone interoperability, SCO delivery, microphone capture, acoustic output, firmware deployment or publication is asserted here. The last known hardware firmware is outside this worktree's verification scope.

The public source pins and validation scope are recorded in profile-integration-handoff.json; workstation-local preparation receipts are excluded. Existing codec, buffering 80/90, I2S, HCD, GATT and HCI-credit files must remain byte-identical to the integration baseline. Test fixtures use synthetic CPU data and have no radio, phone-call or media controls.

## Review findings and test-first corrections

Root executed the pinned-source regressions as Android ARM64 CPU fixtures and observed the expected failures for: late AVRCP replies processed before timeout service; stale GetPlayStatus replies overriding newer notification state; HF/HS reuse after an untagged AT timeout; RFCOMM restart after an uncertain response timeout; and rollback to an old microphone epoch. Local corrections are applied, and fresh official Android builds plus Cortex-M7 objects compile. Root executed all28 final portable CPU cases with return0. The official full CM7 Receiver link passed with immutable source/library.properties and SDK response files, all codec defaults, UART3M, SCHED0 and pins70/72/74/-1. Actual M7 runtime and phone interoperability remain unverified.

Further metadata response review: [AVRCP 1.6.3](https://files.bluetooth.com/wp-content/uploads/dlm_uploads/2024/10/AVRCP_v1.6.3.pdf), sections6.8.1/6.8.2 and AppendixD, separates original-PDU continuation data from an empty abort response. The pinned custom response parser checks STABLE for every successful custom response before its abort branch; the narrow abort-response regression failed on the pinned source. A separate overlap regression also failed when status rejection cleared metadata assembly belonging to a different transaction. Both local corrections are included and freshly compiled. These are portable protocol observations, not field interoperability observations.

## Committed-control transport behavior

Typed cancellation, timeout and a later channel generation cannot withdraw control SDUs already committed to the Classic-peer queue. The scoped retirement policy preserves those SDUs and any partial ACL fragment, then delays replacement admission until the old scoped queue and ordered adapter mailbox drain. A stalled barrier requests an actual Classic disconnect. It does not detach a live queue or fabricate released HCI credits.

The full linked image retains all six decoder-family ops and byte-identical stock HCD. Its four new public-to-radio AVRCP references match the exact OwnedRadio object definitions. The scoped retirement policy now passes the queued/partial and outer H4 same-CID/compound/mailbox/media/HNC/stall CPU cases, and rejects stale disconnect response IDs without resetting a live generation. This supports those bounded software behaviors; actual target/phone interoperability remains unverified.

## Next voice integration stage

Establish the stock-controller synchronous route and actual buffer capability using documented standard-controller evidence first; do not guess a VSC or replace the HCD. Add a separate synchronous owner/handle generation, H4 SCO dispatch and independent RX/TX pool/credit lifecycle; bind it to the admitted encrypted Classic peer. Then connect the real PSM3 owner and HF/HS control stream, implement CVSD and verified mSBC/H2 duplex paths, and expose serialized microphone/sink lifetime with explicit None and Provider starvation counters. Arbitrate A2DP/voice output with fresh epochs and preserve all existing codec buffers. Enable SDP/feature advertisement only after actual authorized duplex phone audio and correct teardown/reconnect behavior are demonstrated. The standalone AT/PCM classes do not establish that support.
