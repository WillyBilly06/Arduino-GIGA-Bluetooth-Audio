# Classic audio priority with BLE GATT

During secure A2DP streaming, Classic receive work runs before retained ATT and
BLE receive work. The host favors three eligible Classic ACL submissions for
each eligible BLE submission. Ordinary ATT data defaults to 27-byte HCI payload
fragments, with at least 10 ms between successful submissions. These are
configurable host limits, not RF airtime guarantees.
BLE transfers can take longer while audio is streaming.

`bleNotify()` returns `Busy` immediately if the radio mutex is unavailable.
`bleReadWrite()` returns false and leaves its output unchanged in that case.
Retry these foreground data APIs from `loop()`. Lifecycle and statistics methods
keep their existing synchronization; these data APIs are not for interrupts.

`BleConfig.audio_connection_preference` requests 80..120 ms intervals, peripheral
latency 2 and a five-second supervision timeout. It makes one asynchronous
attempt per connection after at least five seconds, waiting for required peer
procedures. The standard feature/update procedure has a bounded CID5 fallback
when the LL procedure is known to be unsupported. Rejection, collision or
procedure expiry ends this optional attempt without stopping Classic audio.
Missing HCI command acknowledgements retain the shared watchdog.

`audio_priority=false` restores normal host scheduling and omits the audio
preference. Setting `audio_connection_preference.enabled=false` keeps host
pacing and omits the optional preference. Interval fields use 1.25 ms units,
timeout uses 10 ms units; values and the supervision-time inequality are checked
before startup. The central can select later settings outside the preference.

The shared-credit reserve leaves one Classic credit available for ordinary ATT
when the shared pool has more than one credit; single-credit and separate-pool
controllers still progress. HCI commands, real receive-credit returns, security,
required CID5/6 replies and protocol clocks remain prompt. Optional CID5 requests
wait until the actual eligible ACL transmit turn, with an empty LE FIFO and a
credit for the entire SDU. They cannot unpace older ATT data. LE parameter
requests have bounded BLE-owned storage; overflow requests LE-only closure.

`bleStats().connection` and `GBR_BLE_CONN` keep the configured preference,
accepted submission, command status, and actual connection observations
separate. Only valid Connection Complete/Enhanced Connection Complete/Update
Complete events update actual parameters. Reconnect clears per-connection
request/status metadata. Submission counters are cumulative for the host
lifetime, and do not prove RF delivery. `GBR_BLE_QOS` reports host pacing.

## Verification and remaining gaps

The full legacy actor, BLE wrapper/seven BLE suites, 48 portable core tests and
seven host suites pass. New checks cover 5,175 preference actor assertions,
6,069 priority actor assertions, 697 priority core assertions, 543 preference
core assertions, real mutex contention, and Classic media alongside exact
512-byte echoes. ASan and trapping UBSan cover the actor; legacy vptr checking
is excluded. A separate source review accepted the frozen production.

The GIGA M7 build passes and selects the project-owned host with zero forbidden
BTstack host symbols. Controller firmware is unchanged. The owned actor grows
from 34,116 to 34,412 bytes across both phases: 296 bytes total, with no new heap
buffers. The sent-command record and command queue remain 8 and 2,204 bytes.
The linked main stack is 32,768 bytes.

The private receiver image, SHA-256
`cbbf53035dfccf2381ff109256b9b5f75b86e91610f37a28dd33dc0c5042cdb8`,
is 473,560 bytes. It was flashed to the identity-checked GIGA and fully read
back. An independently transformed public-source build is a separate artifact;
this readback does not qualify a different public binary.

Each case below captures 95 seconds of LHDC V5 reception at 192 kHz, with the
phone configured for 24-bit/1000 kbps. Three GATT jobs exchange ten exact
512-byte echoes each at negotiated MTU 517.

| Image and source | Exact echoes | Timestamp / codec-sequence jumps | Missing PCM frames |
|---|---:|---:|---:|
| Earlier ramp image, deterministic waveform | 30 | 15 / 15 | 385,152 |
| Host pacing, deterministic waveform | 30 | 7 / 7 | 185,088 |
| Host pacing + connection preference, waveform | 30 | 0 / 0 | 0 |
| Host pacing + connection preference, YouTube | 30 | 4 / 4 | 103,680 |

Both connection-preference captures observed interval 96, latency 2, timeout
500 (120 ms / 2 / 5 s) in all 45 retained snapshots. The waveform source
completed its finite 110-second stream, with 24 exact-route observations and
zero phone AudioTrack source underruns. No new local queue, decoder, DMA,
output or transport errors occurred in these captures. Output-padding counters
were zero in the clean waveform capture; no ramp concealment produced that
result. The YouTube capture needed 9,435 synthesized and 94,245 zero-padding
frames, totaling its 103,680 missing PCM frames.

Each capture includes an eight-second pre-GATT baseline; reported audio deltas
cover the narrower periodic-snapshot bracket. The waveform cases use the same
installed source and pattern but differ in capture offsets and connection
history. YouTube content differs across runs. These finite cases do not
establish a statistical performance ranking or a hard RF duty-cycle cap.

**Audio continuity remains unresolved:** the clean waveform capture does not
cancel the YouTube discontinuities. No listening test, analog capture, I2S
clock measurement, or claim of perfectly smooth playback accompanies this
change. The bounded result and source/build hashes are in
[the validation receipt](ble-audio-priority.json).
