# Develop evidence and remaining limitations

Updated2026-10-22. Full supplied257-file decoder set and all eight codec build
defaults remain enabled. The selected runtime is the project-owned Classic/BLE
host; no historical reference-host source is distributed. This is developmental
source, not a stable audio release. Main/release qualification is separate.

## Current correctness changes

The snapshot includes reviewed Classic rejected-configuration transactionality,
Abort/channel retirement and copied-control FIFO ownership; one-command L2CAP
signaling SDUs; single-handle transactional HNC packets and fail-closed HNC error
recognition; passive-event admission fixes; per-frame/pre-RX credit service and
UART-TX-completion owner wake. Hardware/RTOS ownership stays serialized.

A real native mixed workload exposed another conditional bug: while an ACKed
write was unread, the next reliable request held input storage and legal later
commands could occupy all four shared RX credits. The fix copies one pending
ATT request into dedicated517-byte storage before real HCI retirement, retaining
all4096 bytes of LE reassembly. It preserves reliable request/prepared bytes,
CCCD transactions, lifecycle barriers and generations. Commands are validated
without a reply slot; newer data commands drop explicitly while older work waits.
Expiry requests LE disconnect through bounded admission; command capacity and
the existing controller watchdog still limit physical disconnect guarantees.
This is not a general all-CID flood or unlimited-delivery promise.

PCM starvation recovery restores the configured reserve without changing sample
order or hiding zero fill. Starved and intentional-held frames remain separate.
LHDC per-call flags and cumulative counters expose right-channel concealment and
full-frame mute while preserving existing PCM/status behavior.

## Measured continuity remains a failure

A readback-verified image with the ATT fix and LHDC counters received live
LHDC192kHz/24-bit/fixed1000kbps. One ordinary MTU517 GATT session completed ten
512-byte acknowledged writes and ten exact echoes. Around that finite session:

| Counter addition | Result |
|---|---:|
| Timestamp/inner discontinuities |12|
| Prepared-zero PCM frames |308160 (1.605s at192kHz)|
| Outer RTP missing sequences |0|
| Decoder/DMA/transport errors |0|
| LHDC right-channel/full-frame concealment |0 /0|

A subsequent fresh45s playback epoch with BLE disconnection verified at both
endpoints still added three timestamp/inner steps and67392 prepared-zero frames
(.351s). All22 retained BLE snapshots were disconnected; advertising stayed on.
Thus active GATT is not necessary for every remaining omission in that history.
These finite console brackets are not a controlled before/after quality ranking.
The first48kHz reconnection attempt was excluded from the192kHz comparison.

Earlier complete-layer receiver traces retained jumped timestamp/inner headers
already at H4 software completion. Encoded fingerprints matched through owned
copy/L2CAP/callback/queue/decoder, with960 decoded/admitted frames and0 dropped
for those packets. A qualified38.074s observation bounded completed owner work
at1.269ms and outside intervals at1.153ms, with conserved phase/credit ledgers.
This rules out a140ms owner stall in that measured epoch; it does not measure
RF, exact ISR-to-H4 residence, physical clocks or a unique discard cause.

The recurring shape is outer RTP+1 with inner+29 and timestamp+27840 instead of
+960 for one validated5ms LHDC frame. A larger reservoir cannot recreate those
missing periods. Decoder concealment0 excludes only the two measured fallback
branches, not every numerical/audio defect. Continuous stable playback,
coexistence/history-dependent failures and acoustic output remain unresolved.

## Validation scope

`BUILD.json` records this isolated export's actual GIGA4.6.0/M7 link, exact
compiler input hashes and public synthetic checks. Public tests cover real owned
HCI/ACL/L2CAP/profile paths, honest credit replenishment, whole/251/27-byte ATT
fragmentation, app/TX pressure, Prepare/Execute, unsubscribe, callback lifetime,
UART wake and PCM recovery. Controlled LHDC channel tests cover flags/adapter/
frontend counters; independent LHDC PCM fidelity is not established.

SBC/SBC-XQ, AAC-LC, experimental HE-AAC v1, aptX/HD, LDAC and Opus retain earlier
independent decoder/reference and M7 evidence. Those do not qualify every codec
on this host or every sender. Source grants and unresolved third-party table/
decoder conditions remain in THIRD_PARTY; source inclusion is not relicensing.

Default wiring is D70 BCLK, D74 WS and D72 data, with32-bit stereo slots.
`ReceiverConfig.i2s` selects compatible SPI1/2/3 routes; MCLK defaults to-1
(disabled), with optional256Fs output. Alternate pads and MCLK electrical
behavior remain physically unqualified; see the routing update below.
No source export/build action flashes hardware. Raw captures, addresses, user
identities, phone packages, media fixtures, controller assets and binaries are
excluded from this tree. No main-branch update or new release asset is implied.

## Runtime pins and LE request admission update

`ReceiverConfig.i2s.{bclk,data,lrck,mclk}` now selects a compatible SPI1/2/3
hardware route. Defaults remain70/72/74/-1; legacy WS69 builds remain supported.
The27 validated signal routes compose138 requested routes including disabled
MCLK. Optional MCLK is256Fs with unchanged kernel clocks. Native/M7 evidence
does not qualify physical alternate pins, MCLK frequency/duty cycle or a DAC.
The readback-verified default three-wire route runs on target.

Exact LE LTK Request13-byte and Remote Connection Parameter Request11-byte
events now copy into existing profile-owned pending fields without requiring
HCI command queue slots. Their real replies still queue later. A public-H4
regression reproduced the previous false Classic stop under valid command
pressure;5980 focused sanitizer checks preserve security/FIFO and real media
credit return. This conditional bug does not establish the stable-epoch gap cause.

A fresh55s LHDC192kHz/24-bit/fixed1000kbps default-pin test with ten512-byte GATT
ACK/echoes still added six timestamp/inner steps and152448 prepared-zero frames
(.794s), with outer loss0 and both concealment counters0. These workloads are
unmatched to earlier cases; no quality improvement is inferred.
