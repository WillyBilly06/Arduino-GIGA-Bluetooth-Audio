# Current source and physical profile logging

The private551428-byte M7 image, SHA-256
`84bbfd319cb7b7e2bf87ebb49ab900c3234ca7c22b1fdca3dc296aab4035515b`,
was installed and read back byte-for-byte on the identified GIGA. The original
786432-byte R4 application backup was read and verified before writing, SHA-256
`079c1df4c7276c425397a4725a2265dde78523dde0a136ab93b7107c6295a09f`.
Only the M7 application region was written. Bootloader/M4/QSPI and Windows
drivers were not changed. Earlier USB enumeration/status failures are preserved;
their cause is not established by the later successful ordinary DFU operations.

Normal exact-target pairing and the owner's Android confirmation produced an
actual bonded/A2DP-connected phone. The bounded helper first observed an
unbonded failure before the later normal UI completion; that failed receipt is
retained rather than relabelled as a pass. A separate fresh framework read and
actual GIGA negotiation prove the later connection.

## Observed on the installed image

At08:10:52UTC, the current report contained:

| Behavior | Evidence and scope |
|---|---|
| Reception/output | LDAC96kHz, advancing decoded PCM and DMA/output frames. Zero recorded decode, RTP, transport, queue, underrun or DMA faults in this finite window. |
| Playback state | Received AVRCP paused2 then playing1, separately from A2DP streaming and output-running fields. The phone's ordinary Play dispatch resumed the sole YouTube session. |
| Metadata |24 delivered attribute callbacks, including track/duration changes. Values are bounded escaped callback text; upstream truncation and snapshot masks matter. |
| Volume | Actual callbacks64 then55 on0..127. One normal volume-down key moved phone index75 to65 on0..150. The shell media-session adjustment returned without changing the index; it did not prove delivery. No volume increase was sent. |
| Ongoing LE | MTU517 and512-byte exact verified echoes.509 fresh summaries during roughly8.5min of music retained one generation, no open gap and unchanged gap totals, with advancing completions. The client remains running without a duration or transaction cap. |

The client had already recorded four real maintenance/pairing gaps and four
abandoned in-flight exchanges over its complete lifetime. Those are preserved;
the music interval above is a separate scoped observation. A callback, host
queue count, command acceptance and peer receipt remain distinct facts.

The installed logger's existing console-drop count remained visible. Earlier
queue pressure could lose a snapshot after it was remembered, motivating the
source successor below. Position is the last actually observed AVRCP value,
not a continuously extrapolated clock. No acoustics, PCM fidelity, electrical
I2S waveform, total latency, all-codec stability or future continuity proof
follows from this report. HFP/SCO firmware/profile limits and the historical
clock failures remain open; this LDAC window does not clear other workloads.

Local raw/runtime evidence is retained under `build/dev339`, `dev345`, and
`dev346-current-image-playback/current-field-report.json` in the task workspace.
Those local paths are not bundled raw captures or portable installation paths.

## Integrated source successor, not deployed

The Receiver console still has a fixed4096-byte ring. It now reports complete
record admission and rejects overlong records rather than silently truncating
them. Snapshot state advances only after admission; rejected snapshots retry
the latest state. Intermediate changes can coalesce. One record is attempted
per250ms sampling turn across profile, metadata-state/value and LE groups.
Queue admission is not USB or peer delivery.

Copied metadata values carry the actual snapshot generation and upstream
truncation bit, separately from callback records. Copied C strings cannot
recover bytes following NUL. `cached_volume_in_range`, `applied_volume_in_range`
and callback `value_in_range` are numeric checks; initial cached127 is not a
remote volume observation. Log consumers must update the older `*_valid` names.

The exact five-file integration overlay passed a full native AddressSanitizer
run:147 compiler commands,103 runtime invocations, eight child suites; all258
exited zero. The distributed profile case has14 scenarios, including actual
console admission, escaping, truncation rejection, coalesced retries, metadata
invalidation, rollover and volume bounds. The USB stub is scoped to this test.
All426 production files and Receiver.ino remain unchanged. Both official CM7
build/static audits pass191 production TUs/223 compiler rows with all seven
codec switches and the stock HCD.

| Source build | BIN bytes | SHA-256 |
|---|---:|---|
| Public default UART2M |549792|`362df182680a1a5b5debeab055b529b223076a431d861dd89ce1d89281eb4615`|
| Private UART3M/100–110ms |552708|`e22aabecbddcb6580d4ac5995ddcbf16c2e8302ca455b5acb2838931608a7c1c`|

This successor has not been flashed. Its global storage increases by568 bytes;
compiler function frames are not measured runtime stack peaks. No on-device
drop-rate, timing or stability improvement is established yet. Public default
UART2M/automatic reserves remain distinct from the private field settings.
