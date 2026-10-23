# Develop evidence and remaining limitations

Version 0.1.0-dev.5; updated 2026-10-22. All257 selected decoder files and all eight
codec defaults remain enabled. The public source profile selects the owned
Classic/BLE host and excludes the historical reference host. Main remains a
separate stable-branch decision.

## Current all-codec output and BLE evidence

The dev.5 queue-headroom image `210791643c83b4de1f79ae45c55e741511d392cfd311ad74304956d161b0cf10`
was built from a fresh extracted Arduino source ZIP and all487944 application
bytes passed readback. All eight codec identities are enabled. Start with the
bundled **Receiver** example for the measured codec-local buffer settings.
Immutable decoder tables stay in flash; decoder and buffer allocations follow
the selected codec/configuration.

The new physical six-mode matrix passed ten exact512-byte GATT write/notification
echoes per mode at MTU517, with actual120ms BLE connection parameters. Each owned
30-second source completed naturally; the complete sampled windows and raw/chunk
receipts were retained. The active observed spans are:

| Phone codec | Sample rate (Hz) | Active observed span |
| --- | ---: | ---: |
| SBC/SBC-XQ | 44100 | 30.000 s |
| AAC-LC | 44100 | 30.000 s |
| aptX | 48000 | 30.001 s |
| aptX HD | 48000 | 30.007 s |
| LDAC | 96000 | 30.000 s |
| LHDC V5 | 192000 | 30.006 s |

Recorded decoder/drop, queue-full, PCM missing/recovery, DMA/transport and
applicable RTP/detail counters stayed zero. aptX uses raw transport here; its
zero RTP counters are not RTP coverage. The companion checks the complete
sampled source window; it does not prove every intervening instant or acoustics.
The continuous LHDC192/120 ms BLE observation also passed: 1801.937 host
seconds, 1802.520 receiver seconds and 902 complete diagnostic groups. Stream
and output epochs and the physical BLE owner stayed unchanged. All monitored
media/backend error counters stayed zero. This was passive established BLE;
the six GATT workloads above are separate finite tests.

Timing diagnostics retain four distinct sampled accepted updates with the
last-transaction wall-overrun flag set; this is not a cumulative event count.
The sampled transaction maximum was269038 cycles and cumulative maximum critical
interval257916 cycles. No tiny wall-time bound or specific preemption cause is
inferred. These accepted updates caused no recorded PCM/media fault in this
interval; acoustics remain unqualified.

The initial38.007-second LHDC192/GATT gate separately passed full readback,
MTU517 and ten512-byte write/notification echoes. It did not run a characteristic
read loop. The earlier private132 image has separately dated reconnect/read
and30-minute evidence; that evidence is not transferred between binary hashes.

All257 selected supplied decoder files remain included. The unchanged decoder
sources separately passed12 M7 vector cases/3440 timed calls and220 allocation
lifecycles. The phone cannot select Opus, HE-AAC or a distinct SBC-XQ mode;
those have separate decoder/native/M7 coverage. HE-AAC v1 is experimental;
v2/PS and xHE-AAC are unsupported. LHDC has no independent encoded-PCM oracle.
Apple-device interoperability, electrical I2S accuracy and acoustics remain
unqualified here. No finite counter test establishes permanent continuity.

[Clock API and limits](AUDIO_CLOCK.md) and [dated validation](validation/audio-clock-2026-10-22.md) record the implementation and evidence.

## Measured Classic comparison

Both private comparison images use the current decoder/output/clock path,
LHDC48/start26 profile and70/72/74/MCLK-1 pins, with BLE disabled in both.
Each ran the same45-second owned LHDC192/24-bit/nominal1000-kbps-selection source.
Both recorded zero media errors in clean40.018/40.008-second tracking windows.

| Matched metric | Project-owned host | Maintained BTstack reference |
| --- | ---: | ---: |
| Arduino reported program bytes | 483872 | 504856 |
| Arduino reported static globals | 54136 | 77120 |
| Active tracked requested allocation bytes | 245664 | 199584 |
| SDK sleep-accounted busy fraction | 89.149% | 88.232% |

Program/static totals and tracked requested allocations measure different things.
The SDK fraction is not exclusive CPU or thread utilization. This finite pair
shows lower program/static counts, higher tracked allocations and no observed
CPU advantage for the owned host. It does not establish total-RAM superiority,
combined-BLE superiority, statistical performance or better acoustic quality.
The original own-run checker failure is preserved; a separate corrected replay
validates startup fields only when their validity flag permits it. The reference
run waits for actual controller readiness before connecting. No historical EA/132
or unmatched old binary supplies these comparison numbers.

## Earlier correctness changes (historical)

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

PCM starvation recovery restores the configured reserve without changing queue
consumption, sample order or real-input counts. A short integer envelope now
softens starvation and resume transitions; uninterrupted PCM stays bit-identical.
Initial-start and stop behavior are unchanged. Legacy `silent_frames` counts
absent input slots, including possible nonzero decay. The example separates
nonzero synthesized padding from literal zero padding; their sum equals
`starved + held`. Received frames processed by the resume envelope are reported
as `mixed_real`, overlapping real-input counts. There is no new PCM allocation.
See [implementation and tests](pcm-transition-ramp.md). The audible effect is
unqualified, and the ramp cannot reconstruct omitted encoded content.
LHDC per-call flags and cumulative counters expose right-channel concealment and
full-frame mute while preserving existing PCM/status behavior.

## Historical continuity failures retained

The retained earlier priority policy services Classic RX first, paces ordinary ATT data,
and makes foreground BLE data admission use try-locks. Security, required
control and real HCI credits are outside data pacing. One asynchronous preference
requests80..120ms intervals/latency2/5s; it never equates command admission or ACK
with applied settings. Bounded BLE-owned parameter retention cannot block the
Classic event queue, and optional CID5 work cannot unpace older ATT replies.

The private473560-byte image
`cbbf53035dfccf2381ff109256b9b5f75b86e91610f37a28dd33dc0c5042cdb8`
was flashed and fully readback-verified. Both historical95s LHDC192kHz/24-bit/
1000kbps captures completed30 exact512-byte GATT exchanges and observed120ms /
latency2 /5s in all45 retained parameter snapshots:

| Source | Timestamp / codec jumps | Missing PCM frames |
|---|---:|---:|
| Finite deterministic waveform |0 /0|0|
| YouTube |4 /4|103680|

The waveform source completed with0 AudioTrack underruns and24 exact-route
observations. Neither capture added local queue, decoder, DMA or transport
errors. The YouTube failure remains material. A clean finite waveform case is
not perfectly smooth audio, universal coexistence, a statistical ranking or
physical output proof. The current public transformed-source build is a
different artifact; private readback cannot qualify its binary. See
[bounded policy and receipt](ble-audio-priority.md).

The following continuity results predate the PCM transition ramp. Prepared-zero
frames were literal zero fill in those images, not the new missing-input total.

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

The corresponding maintained runtime with the ramp was readback-verified and
observed for 95.110 s with LHDC 192 kHz/24-bit/fixed 1000 kbps. All 30 GATT acknowledged
writes/exact echoes passed; all 45 retained snapshots were streaming with BLE
connected, without local error/drop increases. It still added 27 timestamp/inner
steps and 702336 missing-input slots: 61200 nonzero synthesized frames plus 641136
literal zero frames. Another 61440 received frames were mixed during recovery.
All 45 post-baseline priority samples reported UART7 priority 0, DMA1 stream7 priority 6 and
grouping 3. This permits UART preemption but does not measure interrupt latency.
These periodic snapshots do not establish a closed packet ledger, acoustics,
an encoded-omission cure or a controlled quality ranking. The independently
built public profile has not been flashed. See [details](pcm-transition-ramp.md).

## Earlier validation scope (historical)

`BUILD.json` records this isolated export's actual GIGA4.6.0/M7 link, exact
compiler input hashes and public synthetic checks. Public tests cover real owned
HCI/ACL/L2CAP/profile paths, honest credit replenishment, whole/251/27-byte ATT
fragmentation, app/TX pressure, Prepare/Execute, unsubscribe, callback lifetime,
UART wake, PCM recovery and starvation/resume interpolation. The transition
regression checks primed-tail seeding, cross-half state, early reversal, integer
extremes and unchanged queue/accounting behavior. Controlled LHDC channel tests cover flags/adapter/
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

An earlier pre-ramp55s LHDC192kHz/24-bit/fixed1000kbps default-pin test with ten512-byte GATT
ACK/echoes still added six timestamp/inner steps and152448 prepared-zero frames
(.794s), with outer loss0 and both concealment counters0. These workloads are
unmatched to earlier cases; no quality improvement is inferred.
