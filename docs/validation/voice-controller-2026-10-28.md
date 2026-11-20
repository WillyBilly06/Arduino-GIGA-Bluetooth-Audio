> **Workspace-only historical evidence:** Links in this page that point outside
> the library refer to local `build/dev...` artifacts. They are not part of a
> copied source package. The historical observations below are unchanged.

# Actual voice-controller setup result

The voice results below are retained historical observations. The later dev282
AVRCP connection attempt ended before any new voice/music/control procedure.
Its full original firmware restoration was verified, but the phone became
unbonded and its original bond/configuration is not restored. See
[the latest read-only closure](../../../dev289-pending-bond-readonly-closure-20261028/7e951afe93304783925f635c20ebf2d9/receipt.json).
Further physical procedures are waiting for the user's normal-pairing choice.

The dev266 private observer image was flashed and its actual bytes read back on
GIGA CM7. Explicit `b` requested HF, HS and wideband. The controller became ready,
but `voice.ready()`/backend availability remainedfalse with `FlowUnavailable`
(reason7), last HCI status0 and synchronous RX/submitted TX0. Actual attempt
`bfadbbe92c6141f6aa872bf000d8a873` classifies the exact setup branch:

| Actual setup observation | Result and limit |
|---|---|
| Read Local Supported Commands0x1002 | Status0,65-byte result, octet10=`ec`; bit84 (octet10 bit4, Write Synchronous Flow Control Enable) is clear |
| Read Synchronous Flow Control Enable0x0c2e | Status0, value0/off; last ticket step4/opcode0c2e |
| Write Synchronous Flow Control Enable0x0c2f | Not attempted: unsupported command bit stayed gated; no accepted-write/verify-off hypothesis is needed for this attempt |
| Controller synchronous buffers |64 bytes/one packet; capacities alone do not establish retirement timing or usable completion flow |
| Broadcom routing read0xfc1d | Five baseline fields0/2/0/1/1; the explicitly allowed known route changed only field0 to1, preserving2/0/1/1, and was read back/verified |
| Final policy result | Route present/written, no reset required, controller_flow0, rx_paced0, available0/reason7; no capability/admission gate lowered |

Two matching complete snapshots and exact setup fields are preserved in the
[dev266 independent review](../../../dev266-voice-flow-m7-20261028/adapter/physical-bfadbbe92c6141f6aa872bf000d8a873/independent-review.json)
and its [physical receipt](../../../dev266-voice-flow-m7-20261028/adapter/physical-bfadbbe92c6141f6aa872bf000d8a873/receipt.json).
This establishes that this controller/stock image advertises no standard flow
enable and reports flow off under the current policy. It does not establish
permanent chipset HFP incapability, a controller defect, or physical SCO audio.
Route configuration/readback success is separate from a working voice backend.

The source registers voice SDP records only after `voice.ready()`. Therefore
HF/HS advertisement and fresh phone discovery, RFCOMM/SLC/AT negotiation, call
procedures and local SCO routing were gated off. No local SCO helper was
installed/started; no call, assistant, microphone recording or idle HSP button
was invoked. Disconnected codec1/8000Hz fields are not negotiated CVSD evidence;
requested wideband is not mSBC evidence. HSP facade/native coverage is separate
from physical button behavior, which can redial/place a call when idle.

The receive-pacing [source audit and read-only observer](../../../dev241-voice-all-features-20261028/voice-rx-pacing-audit-20261028/README.md)
and [offline receipt](../../../dev241-voice-all-features-20261028/voice-rx-pacing-audit-20261028/host/receipt.json)
reproduce banked/stale byte grants and back-to-back submissions against a
single-slot geometry. The existing bound `rx_slots*rx_bytes+59` bounds byte
credit, not controller retirement time. The default-off observer distinguishes
normalized IRQ-read/parse, UART-copy/software-drain/TC-upper-bound observations
from actual accepted controller completion. No active timed pacer was delivered,
no RX-pacing/default/controller gate was enabled, and this candidate was not
M7-built or physically exercised. A validated controller-retirement/phase bound
is still missing; UART TC and nominal media duration cannot invent that bound.
This is a prerequisite study for a separately reviewed experimental transport,
not a voice fix or proof that HFP cannot work.

The initial phone probe's null Context failure occurred before any board write.
A corrected attributed shell Context passed focused host checks and an actual
state-only phone smoke, then passed the full experiment's preflight/restoration
reads. No permission grant or hidden-API policy change was used. The earlier
[context-fix attempt review](../../../dev241-voice-all-features-20261028/root-executable-voice-adapter-context-fix-20261028/physical-b85fa2d49db54f57a9d6d0ba8d70f601/independent-review.json)
remains historical evidence; the dev266 observer supplies the later exact branch.

The dev266 observer image is540132 bytes, SHA-256
`67f23ae19238ec0dc4aebdd47cd5277bd237954536abfe1fcfa2fcaa16064bae`;
see its [M7 build facts](../../../dev266-voice-flow-m7-20261028/VoiceQualification/build-facts.json).
The earlier538680-byte voice image SHA-256
`3cfec0fd79ff19184e6c1c342b9188d40b46a02b6092bbbc5290c3725c920523`
belongs to the preceding context-fix attempt, not this observer deployment.
The dev266 full786432-byte original application was restored and independently
compared with its fresh backup, SHA-256
`079c1df4c7276c425397a4725a2265dde78523dde0a136ab93b7107c6295a09f`.
Exact phone9 fields, original audio mode/communication route/headset state,
sourceNONE/owned sequence11, reader epoch41 and22 settled controls were verified;
all owned jobs were quiescent and the raw prefix preserved. These are closure
facts for this attempt, not an assertion about the current live device state.

CVSD/mSBC interoperability, actual synchronous geometry/completions, calls,
real microphone input, voice I2S output and acoustics remain unqualified. The
[feature matrix](all-feature-status-2026-10-28.md) keeps the separate failed or
unknown AVRCP, volume, format-selector and clock gates.
