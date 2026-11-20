# Ongoing LE traffic and Receiver profile logging

This experimental source update keeps physical observations separate from new
code. It is not a stable release or universal scenario qualification.

## Actual ongoing workload

The owner approved normal GIGA pairing. A fresh ordinary Android framework
observation confirmed bond12, adapter12 and A2DP2 across three stable samples.
On October29 at07:12:35UTC, Root launched an ongoing GATT client on the NX809J
phone, targeting only this GIGA. It negotiates MTU517 and exchanges512-byte
values with one outstanding transaction. Each completed exchange requires both
the real write callback and an exact nonce/connection/sequence/CRC-checked echo.
The next value is submitted immediately after both receipts. There is no run
duration, transaction cap or artificial connected delay. Reconnection backoff
applies only after a real failure and is reported as a gap.

At07:16:22UTC,196 exchanges had completed, with zero reported gaps, corruption,
duplicates or abandoned transactions. The original R4 GIGA image was receiving
LHDC V5 at192kHz with advancing actual PCM/DMA counters and zero recorded PCM,
RTP, decoder or DMA faults. Clock deadline diagnostics remain failed. These
finite observations do not qualify acoustics, fidelity, electrical I2S signals,
latency, all codecs or future continuity. The worker remains running after this
snapshot; the number above is not its stopping condition.

Local live ownership/evidence:

- Worker token `134ea60c4ece412181b9b71c49ea88b8`, host PID50076, phone PID29832.
- `build/dev321-continuous-le-owned/runs/<token>/state.json` and rotating
  `worker.log` contain separate request/submission/callback/echo/completion
  counters, MTU, generation, gaps, abandoned requests and freshness.
- `build/dev326-ongoing-le-actual/<token>/observations/` retains Root snapshots
  with actual process identities and concurrent GIGA rows.
- Logger PID45960 and session
  `build/dev324-live-observation-recovery/sessions/bdd81697d6be40638237cd11f7fe903e/`
  replaced the stopped old logger. Older raw evidence is preserved.

These paths and PIDs are local evidence, not portable installation paths or a
guarantee that a process will remain alive. Root checks actual handles and
advancing fresh counters. The client has a scoped stop request and an owner
lease; loss of its owner requests closure. Missing closure evidence stays
unknown and cannot become clean success or trigger an automatic replay.

## New Receiver logging

All new formatting runs in the foreground using the existing bounded console.
No formatting, allocation or USB operation was added to a real-time callback.

| Record | Meaning |
|---|---|
| `GBR_METADATA` | Delivered title/artist/album/playing-time value, bounded and quoted; controls, quotes, backslashes and invalid UTF-8 bytes are escaped. Omitted and invalid byte counts are explicit. |
| `GBR_METADATA_STATE` | Actual copied metadata generation plus known/truncated attribute masks, including invalidation. |
| `GBR_VOLUME` | Delivered callback value and explicit0..127 range. |
| `GBR_PROFILE` | Received AVRCP playback value/name, unknown state, cached and applied volume, known position/length, plus separate Classic/A2DP/receiver/output snapshots. |
| `GBR_LE_TRAFFIC` | Two-second host write/admission counters and connection/subscription/generation edges. These are not phone-delivery receipts. |

Profile sampling is capped at four snapshots per second, with change records
and a ten-second heartbeat. Changes between snapshots can coalesce. Initial127
is a cached/default volume; a range-valid value does not prove a new phone
volume notification. AVRCP playing/paused is never inferred from command
acceptance or A2DP transport activity. Console drops remain visible.

The private qualification example retains its existing command/reply records.
The ordinary echo example aggregates successful traffic to avoid per-packet
log flooding. Native formatting tests cover maximum-size escaped lines,
UTF-8/control characters, missing text, known/unknown playback transitions,
metadata invalidation, volume boundaries, independent A2DP state, aggregate
counters and timer rollover.

## Normal pairing correctness

A direct comparison with pinned official BTstack behavior led to an
independently authored own-library correction: valid fresh remote SSP can
finish only after the current fresh key, actual encryption and current key-size
proof agree, without requiring a local Authentication Complete event that may
not be generated for the responder. A separate valid nonbonding late-completion
case was repaired. The complete-current-proof predicate and public security
gate remain unchanged; no credential, confirmation or permission bypass was
introduced. The earlier phone timeout's actual cause remains unproven.

The reviewed source passed102 project-runtime invocations and eight child
suites:256 commands with zero exits, plus independent normal-event integration
checks. The distributed logging regression is included in the expanded normal
runner. The final canonical run passed103 runtime invocations and eight child
suites, with all258 commands exiting zero. Both combined CM7 static audits
pass191 production translation units and223 compiler rows each, with all seven
codec switches enabled and the Cortex-M7 softfp ABI. Outputs are:

| Example | BIN bytes | SHA-256 |
|---|---:|---|
| Default UART2M Receiver |548520|`94a6e67d68ee4464f63ae34f54c35e56b7792d6e6a421d37be33e48c2b7be189`|
| Private UART3M/100–110ms Receiver |551428|`84bbfd319cb7b7e2bf87ebb49ab900c3234ca7c22b1fdca3dc296aab4035515b`|

Builds used the official Arduino Mbed GIGA4.6.0 CM7 toolchain and stock HCD.
All426 production files match the combined frozen source. Compilation is not
deployment or physical qualification. The ongoing hardware workload above is
on the earlier R4 image, not either new BIN. Default bonds remain RAM-only;
the optional application-owned store still chooses no persistent backend.
HFP/SCO firmware gates, missing physical codec peers and clock/I2S/acoustic
limits remain as listed in the [feature status](all-feature-status-2026-10-28.md).
