> **Workspace-only historical evidence:** Links in this page that point outside
> the library refer to local `build/dev...` artifacts. They are not part of a
> copied source package. The historical observations below are unchanged.

# Current feature validation status

Latest device state: the subsequent dev282 AVRCP attempt stopped before music
or any test control. Its normal connect observed phone bond12 -> bond11 with
A2DP still CONNECTING. The runner incorrectly treated helper closure as a
settled failed link. The original full786432-byte R4 image was restored and
compared byte-for-byte, but the phone subsequently reported unbonded10.
[Root's read-only closure](../../../dev289-pending-bond-readonly-closure-20261028/7e951afe93304783925f635c20ebf2d9/receipt.json)
confirms two later adapter-ON/disconnected observations, no GIGA bond, sourceNONE
and a fresh reader. Phone bond/configuration restoration is **not complete**.
Further connection/pairing/flash tests await the user's normal-pairing choice;
older successful restoration receipts remain historical evidence.

The host intentionally stores Classic bonds only in RAM and erases them in
`OwnedHost::begin()`/`end()`. It cannot retain its key across a receiver reset.
This is a source-backed explanation for the key-lifetime mismatch; the exact
HCI/Android cause of the observed bond transition was not captured. Pending
state classification is fixed in the separate test runner. The optional
application-owned storage provider is now integrated in the local library;
it selects no NV backend and is not a deployed or durable-storage proof.

This local source includes the AAC selected-storage changes, AOSP-derived LHDC
synthesis correction, explicit lifetime-fenced HSP button API, bounded AVRCP
received-state reconciliation and optional typed Classic bond storage. The
latest combined default Receiver passes an official Arduino Mbed GIGA4.6.0 M7
build/static audit with all seven codec defaults enabled. Its426 production
files match the compiled snapshot. All99 native runtime invocations and eight
child suites pass. Earlier private AVRCP/voice probe builds are separate
historical evidence. See [the combined integration](bond-storage-avrcp-2026-10-28.md)
and [subsequent defensive parser validation](sco-response-validation-2026-10-28.md).
These counts do not constitute physical feature qualification. The e1 field
rows below refer to the earlier snapshot, before the latest storage/status
changes; no new complete-image field qualification is implied.

Expanded sanitizer tests also cover154 decoder configurations,1152 allocation
failure positions,26108 balanced decoder allocations/frees,416 accepted endpoint
configurations and3328 cross-endpoint rejects. I2S configuration tests execute
4167614 checks across routing, GPIO exclusions, rates and geometry. These are
CPU contracts; they do not qualify unmeasured pins or unavailable codec senders.

| Feature | Evidence now | Remaining physical evidence |
|---|---|---|
| BR/EDR A2DP + board-pin I2S | Current feature-probe completed a full30min LHDC192/24 stereo MUSIC window, advancing349546368 real PCM frames with zero recorded audio/RTP/decoder/transport/DMA faults; full original field restored | Clock timing remains failed; independent electrical/acoustic output, latency and other codecs |
| Continuous bidirectional BLE with A2DP | Current e1 feature-probe completed1800017ms,1294 verified512B transactions/662528 bytes each direction, MTU517, one outstanding and no artificial gaps; source/route/stream/output epochs persisted with1245 gap-free provider polls | Clock gate failed with52 cumulative overruns. This is finite traffic/counter evidence; no saturation, all-profile or acoustic qualification |
| Owned-source coexistence runner | dev253 independent review verifies actual `GBR_AV_REPLY` receipts and device/callback counts. Original analyzer expected `GBR_BLE_RX` and remains failed; full786432-byte image, phone9, idle source and fresh reader restored. An image-bound marker correction passes six negative/regression tests and all1294 saved actual receipts | Seal the correction into any future runner; retain original failures and separate failed clock gate |
| AVRCP metadata | Actual current feature-probe received exact title `Owned Music One`, artist `GIGA Qualification`, album `Owned AVRCP Music Suite` and duration2100000 from the owned phone source | More tracks, notification procedures, UTF8/truncation and reconnect cases |
| AVRCP Play/Pause/Stop/Next/Previous | dev271 actual Pause pair received09/09 and one owned pause callback/source-paused state, but received board playback stayed1 across16501ms: Pause qualification failed. The finite received-status/metadata refresh fix is now integrated and passes the combined native/M7 checks | dev277 and dev282 stopped at connection before source/taps; all15 requested action cases remain unexercised for the fix. Received playback/metadata, notification and reconnect gates remain |
| Remote volume | Numeric149 request and a separate one-step lower command were actually sent from phone baseline150 in0..150; phone stayed150 and receiver stayed127. Both volume parents verify exact baseline closure | No distinct phone/receiver decrease observed; relative/absolute AVRCP wire behavior, scaling and acoustics remain unqualified. Cause of unchanged phone index is not established |
| HFP/HSP control | Native RFCOMM/AT/owner/generation/security tests pass; HSP facade linked in M7. dev266 read command map successfully (octet10ec, bit84 clear) and read flow off; no unsupported WriteFlow attempted. Route read/write/readback succeeded, but voice.ready() stayedfalse/reason7 | HF/HS SDP advertisement/discovery, RFCOMM/SLC, calls and audio were gated off and remain physically unqualified; controller HFP incapability is not established |
| HSP button | Actor1294/facade29 targeted checks and complete native regression pass; no automatic command | Explicit controlled-call context. An idle button can redial/place a call and is not physically exercised |
| CVSD/mSBC voice | Native worker/pipeline/packet/ownership tests and M7 probe build pass. Actual controller reported SCO64bytes/one packet and standard flow off; RX/TX remained0 and no local SCO source started. Receive-pacing audit is source-only, with no active pacer | Validated controller-retirement/phase bound remains missing for the byte-only fallback; route setup alone does not qualify synchronous geometry, CVSD/mSBC, output or acoustics |
| Microphone/provider | Synthetic provider, copied lifetime token and detach fences pass CPU tests | Real microphone wiring and voice/acoustic path. None/silence is not a microphone test |
| SBC/SBC-XQ | Native/target evidence plus dev276 actual44.1kHz16-bit mono/stereo MCU windows and strict selector/10s-request hold evidence | Full-window continuity/fidelity and suitable XQ sender/bitpool geometry; XQ has no independent Android codec ID |
| AAC-LC/HE v1 | Decoder/allocation/transport/target-vector evidence plus dev276 actual AAC-LC44.1kHz16-bit stereo MCU windows and strict selector/10s-request hold evidence | Other profiles/rates and identified HE sender; generic AAC cannot prove SBR. Sampled windows do not prove full10s continuity/fidelity |
| LDAC | Native/target evidence plus dev276 actual44.1/48/88.2/96kHz stereo, source32-bit to receiver24-bit, strict selector/10s-request holds and sampled MCU progress | Full-window continuity and independent PCM oracle; quality1000 is a selector, not measured kbps. Clock failures/unknowns remain separate |
| LHDC V5 | Corrected M7 CPU test: eight configurations/two passes/512 calls, zero decode-time allocations/deadline misses, maximum1691us. dev276 actual MCU formats cover44.1/48/96/192kHz x16/24-bit stereo | Four44.1/48kHz mode8 requests normalized to mode7 and fail the original selector/hold gate;96/192kHz cases retain requested mode8. Continuity, clock, RF and fidelity remain unqualified |
| aptX / aptX HD | Native/target evidence plus dev276 actual44.1/48kHz stereo aptX16-bit and HD24-bit, strict selector/10s-request holds and sampled MCU progress. Standard aptX validly carries raw payload without RTP | Full-window continuity, clock and fidelity; standard aptX uses radio bytes/decoder/DMA/PCM evidence, while HD retains its RTP requirement |
| Opus | Decoder/vector/allocation and M7 build evidence | Compatible vendor/PipeWire sender; absence from the current phone's capability rows cannot be bypassed by a request |
| Configuration-driven lazy memory | Actual selected-decoder allocation/failure/closure tests; M7 LHDC rate-sized footprints and216 balanced allocations/frees | Full current-image multi-codec/reconfiguration/RF sequence |
| Classic bond storage | Typed optional application-owned callbacks integrated; actual-host/facade error-vs-miss, copied records, validation, re-entry, lifetime and no-new tests pass; default RAM cache remains | No durable backend, NV writes, lost-key recovery, MCU reset or power-cycle persistence qualification. Current phone is unbonded; new normal pairing requires the pending user choice |
| I2S routes/rates/lifecycle | Expanded native routing/GPIO/rate/geometry tests; actual current test uses D70/D72/D74, MCLK=-1 | Alternate pins/MCLK, measured clocks/waveforms, acoustics and end-to-end latency |

The latest combined default Receiver BIN is545360 bytes, SHA-256
`aac5edf0d680186b6078ec149bf1719e2a37eb6bbb0eee2dda241ca7596b220d`.
The preceding426-file build was545288 bytes/SHA-256
`ac7b7d13d2f05051e4983a2f936e9dc6b106972843b523aca29bff4be6a1bfac`.
The earlier424-file default build was541908 bytes/SHA-256
`c48380b3ee20024780bd6cf3da2e0cb5662f08c3eebb48395c843fa0f6dcc1a0`;
it is historical evidence. Private AVRCP/voice image hashes are recorded under
their separate builds. The latest dev282 full firmware readback matches the
original R4 bytes, while dev289/288 read-only closure confirms the phone is
unbonded. Firmware restoration is distinct from phone-baseline restoration.
The current source is not a stable release.

The [current MUSIC/BLE report](current-music-ble-2026-10-28.md) records the finite
physical result. Raw local evidence is retained at
[dev253 independent review](../../../dev253-owned-music-coexist-separate-clock-20261028/independent-review.json).
Its private UART3M and100/110ms reserve differ from public defaults UART2M and80/90ms.
Minimum observed PCM reserve was4032frames, about21ms at192kHz. Firmware timing
unknown remained0. A separate parser correction now distinguishes all results
from started/completed wait1 transitions; seven tests and saved capture pass.
Original failed receipts remain unchanged. This correction does not clear52
active-window overruns or qualify the physical clock.

The [actual voice-controller setup report](voice-controller-2026-10-28.md)
records the exact unavailable flow branch, skipped profile/audio stages and
verified dev266 closure. Disconnected codec fields are not negotiation proof.

The [dev276 independent format review](../../../dev276-codec-format-independent-review-20261028/independent-review.json)
identifies19 actual source/MCU formats from fresh capability-derived trials;
15 retain the strict requested selector plus10s-request hold gate. Four
LHDC44.1/48kHz16/24-bit requests selected32776/mode8 but returned32775/mode7;
their original failed gates remain failed. Standard aptX without RTP is valid
for this source and is checked through radio bytes, decoder, DMA and real
PCM/work progress; HD still requires RTP. All19 have sampled MCU digital windows,
but none proves full10s continuity, electrical output, acoustics, latency or
fidelity. Per-case clock failures/unknowns remain; intentional reconfiguration
interruptions are not continuous playback. SBC-XQ, identified HE and Opus sender
coverage remains unavailable.

The [dev271 actual Pause review](../../../dev271-avrcp-regular-file-parent-20261028/parent-runs/012a996b9c8140f592cbd8e9e54deb1c/independent-review.json)
separates09/09 tap delivery/source behavior from stale received status. The
[dev274 isolated fix](../../../dev274-avrcp-status-lifecycle-fix-20261028/REPORT.md)
and [dev275 M7 build facts](../../../dev275-avrcp-status-m7-20261028/Receiver/build-facts.json)
are preparation evidence. The [dev277 receipt](../../../dev277-avrcp-status-target-20261028/parent-runs/f1d7aba5137c438b80893da5a42ba722/receipt.json)
retains its reconnect-timeout/unknown outcome before source or taps. Separate
[dev278 recovery](../../../dev278-status-reconnect-recovery-20261028/90d34cad763c464bb5790be0c19e93b5/independent-review.json)
verifies exact full786432-byte R4/079c restoration, phone9, source19/NONE and
reader61/24 controls. Recovery does not validate the fix or erase dev277 failure;
the earlier FIFO-denial failure also remains.

The [numeric149 volume review](../../../volume-target-parent-20261028/e4bd4985a7624c71ba8406055230f951/independent-review.json)
and [one-lower-step review](../../../volume-platform-step-parent-20261028/9b129133b3a44c858d5bcfd42d6475df/independent-review.json)
retain failed distinct-transition gates, unchanged150/receiver127 observations
and exact phone9/full-M7/sourceNONE closures. Shell command success does not
prove AVRCP wire behavior or acoustics. The receive-pacing
[audit/observer receipt](../../../dev241-voice-all-features-20261028/voice-rx-pacing-audit-20261028/final-receipt.json)
is source-only; default completion failure remains, with no active pacer or
physical transport qualification.

The old103 phone requests are not103 accepted configurations. Fresh selectable
capability rows, exact current nine fields, actual receiver configuration and
sampled interval persistence must remain distinct. Unsupported HE-v2/PS, xHE,
aptX Adaptive/LL/Lossless and BLE Audio are not claimed. Historical RTP/shortage,
clock wall/CUM and numerical failures remain failed; no gate is waived here.

Owned local MUSIC playback and cleanup have a short physical smoke result.
The player app uses finite foreground/wake/focus ownership, low-level generated
music and read-only shell diagnostics; source/route/session loss invalidates a
continuous test. It does not establish physical acoustic quality or latency.
# Current follow-up, October29

The private SSP/logging image has since been installed/readback-verified and
has actual LDAC96k reception, metadata, received playback-state and volume
observations with ongoing LE traffic. A later admission-aware logging source
passes native and M7 checks but is not deployed. The [current report](current-profile-logging-2026-10-29.md)
supersedes the earlier pending/deployment wording below while retaining the
historical measurements and failed gates.

Normal pairing is approved and the phone is now bonded/A2DP-connected. An
ongoing verified LE duplex worker is running alongside LHDC192k playback on
R4. The later SSP/logging source changes pass combined CM7 compilation but
have not been flashed. See the [current evidence and limits](continuous-le-and-profile-logging-2026-10-29.md).
The dated table below retains the earlier measurements; pending-pairing
statements are historical and superseded by this follow-up.

