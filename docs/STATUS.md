# Dev.13 development status

Experimental; not stable. Typed AVRCP taps, copied metadata/status and scoped channel retirement are implemented. All28 final portable Android ARM64 CPU cases passed on the reviewed source; the dev.13 package build is recorded in [validation](validation/dev13-2026-10-27.md). CPU assertions and compilation do not establish actual GIGA/phone operation. HFP/HSP remain unadvertised standalone modules with no integrated RFCOMM owner, SCO/duplex audio or public microphone registration API. Existing codecs, LHDC80/90, I2S, HCD and BLE defaults are retained. Longer listening produced brief PCM shortages after earlier zero-fault finite tests; their initiating cause remains unresolved. Main is maintained separately.

## Historical dev.12 and earlier evidence

# Dev.12 current development evidence

The LHDC automatic profile is now80ms startup/90ms ring; other codec bases and the clock/radio algorithms are unchanged. The public-code SCHED0/opt-in3M600s LHDC192/BLE window completed130 verified512B exchanges over3cycles with zero recorded missingPCM/RTP/decode/transport faults, but14 clock overruns. The public60/70ms baseline lost59328PCMframes. Version12 builds to the exact field-tested BIN bytes; this does not establish acoustics, total200ms latency, all-codec interoperability or numeric precision. HFP/HSP/RFCOMM/SCO/pluggable microphone and complete typed AVRCP remain unfinished. See [current validation](validation/dev12-2026-10-27.md).

## Historical status snapshots

# Experimental develop status: 0.1.0-dev.10

NOT STABLE. The default SCHED0 Receiver build-basis archive has actually been extracted, compiled and statically audited. Publication/final documentation-archive parity remain separate; no default-image field deployment is claimed. Current code basis 0449e41630dae0ac1f02b717c18100687c1cc33d has 501 baseline files/373 Receiver input rows/170 library TUs. New release documentation is separate. All supplied decoder families/identities/defaults and original licenses remain.

The selected real-PCM policy sizes storage for the negotiated configuration and delays DMA startup/recovery until the watermark P is present. Its running inventory target is fixed T=P-2H, with R/P/H geometry checked. Estimated compressed work participates in inventory; this is not a PCM floor, measured total latency or a supply repair. Explicit legacy startup_packets retains learned-target behavior.

The actual private SCHED1 Receiver image e417e6ad... (503812 bytes, ELF b549d049...) has official full M7/static and fresh linked structural proof. Its LHDC192 source 90.072132986 s and 46 verified 512-byte GATT exchanges/60.005s/MTU517/three physical BLE controls completed, with 43 non-clock gates zero. Clock CUM FAILED 2/88, max 22.1229167 us against the unchanged 20 us bound; timing_unknown=0. Forty coherent target cohorts matched 11136 frames, zero mismatched; five remain UNKNOWN at 1 ms print boundaries. Supply witness UNEXERCISED/coverage unknown; prior starvation unresolved. No causal improvement or continuity claim.

Actual SCHED0 decoder-only benchmark image 3c7e880a... (638264 bytes, ELF 703800ff...) ran 12 configurations x 20 passes/3440 successful invocations. Complete retained results satisfy oracle/status/count/deadline/tracked-heap-close gates with deadline 0 / residual 0 / allocation-failure 0. Capture remains CAPTURE_FRAMING_DEGRADED: one partial HE-stereo row was interrupted by private console replay; later complete rows are valid separate evidence, not capture repair. No global PASS, physical-reset exclusion, A2DP/I2S or end-to-end qualification.

LHDC independent encoded golden remains MISSING. LDAC uses tone oracles, aptX HD PCM16 does not establish 24-bit precision, HE v1 covers SBR-only 44.1k mono/stereo, and Opus 48k stereo 20 ms fixture execution does not prove phone interoperability. Other configurations, Apple AAC sender, physical/electrical/acoustic continuity and total 200 ms phone-to-DAC latency remain unproved. Full HFP/HSP/RFCOMM/SCO/microphone and complete typed AVRCP remain unfinished. SDRAM is caller-initialized; audio/stock SDRAM PLL2 ownership must be resolved by the application.

The actual default build basis is commit c5db02928f7e4eaee3cf7f6557c8f0e58a9ad636, 503-member Git archive cffc98e8f260727c9c678f1f598703239717f2cb6a3428c1c84d38944eea1969. Official M7 compile 104.422 s, exit 0/quiescent and static audit passed: 170 library TUs, 373 unchanged source rows/projection 6d5ca487..., seven defaults, six Ops, four owned-host components, no selected BTstack, stock 35182-byte/139-command HCD, no RTX wraps and 32768-byte main stack. BIN cf7132ca0b9442c71e54bc026f18ee4942382fb46822f750fc1b2b3625ac337e (496552 bytes), ELF c5c15b7dd05ba9f045f72822ae7cb3ecb7fd320b592d8a2624608711b1291da9. This differs from private e417. The final documentation-only ZIP is not claimed compiled; Root verifies all 373 input bytes plus property-version/member parity before publication.

See [current dated validation](validation/dev10-2026-10-26.md) and BUILD.json current_dev10. Private Receiver, decoder benchmark and default ZIP have different build/evidence scopes. No prior failed result is rescored.

## Historical dev.9 and earlier snapshot (retained verbatim)

The status labels below describe their original preparation/build steps, not current dev.10 publication.

R3 code-input parity: Root verified/extracted the475-file R2 ZIP and built the official all-codec M7 source in106.797s (exit0/quiescent). The corrected static audit passed; its first failed attempt is retained. The binary is byte-identical to the field-tested f42 image, so that finite clock-failed observation applies by exact binary parity, not a new hardware test. R3 changes only noncompiler documentation; all365 ordered input hashes/projection remain2cb3a3a2.... The final archive self-hash belongs only in the external sidecar. No clock/stability/fidelity/E2E pass is created.

# Experimental develop status: 0.1.0-dev.9

NOT PUBLISHED / NOT STABLE. R2 preserves f42 production code, corrects the distributed vendor manifest, repairs two nonproduction fixtures and updates release documentation/metadata. The actual verified R2 ZIP build/static audit and new-ELF instruction review are now complete. R3 retains all compiler input bytes and updates documentation only.

ECA4 completed six finite phone-family normal windows (LHDC192,SBC44.1,AAC-LC44.1,aptX48,aptXHD48,LDAC96). All43 non-clock gates were zero, but cumulative clock overruns9/1/1/0/4/5 remain. Sampled/final zero does not clear cumulative failures. No matched before/after experiment or general stability is claimed.

The corrected f42 LHDC192 run completed source90.066154810s,GATT43 over61.118s at512B/MTU517,and three physical controls.43 non-clock gates were zero; clock wall-bound FAILED with4/91 cumulative overruns,unknown0,90started/full. Worst retained wait1 was528.4354 configured microseconds, dominated by inclusive pre-restore-to-post-exit span2; it does not identify an IRQ/thread/bus cause. Full8c readback/exact saved LHDC48 IDLE restoration and all releases completed. This traffic check does not prove a clipped sample occurred, bit-exact24-bit output, pop cure, timing improvement, acoustic continuity or stability. ECA4's earlier failures remain.

The one-leaf finite LHDC float-to-S32 correction is included. Clock phase/restoration diagnostics remain linked, with every original20us/44/CUM gate unchanged. Legacy lifecycle/control/service/acquisition/RX-age observers remain absent. No controller patch replacement, buffer/profile change or codec capability exclusion is introduced by this export.

Opus/HE/distinctXQ/Apple sender coverage, independent low24-bit fidelity, electrical/acoustic proof, longer qualification and200ms total phone-to-DAC latency remain unproved. Full HFP/HSP/RFCOMM/SCO, complete typed AVRCP and a public pluggable microphone-input API remain unfinished. All original project/vendor license and notice bytes are retained; no new grant is asserted.

See validation/dev9-2026-10-26.md/.json and BUILD.json for exact artifact/evidence scopes. Main/develop, remote tags and ZIP publication were not changed by this preparation. Historical records below retain their original verdicts and do not qualify f42.

## Historical dev.8 and earlier records (retained)

# Develop status: 0.1.0-dev.8

Measured selected raw-AAC storage reduction; general audio remains unqualified.
All supplied decoders/seven defaults and original component notices remain.

At AAC-LC44100/stereo16, compact requested decoder bytes25262 versus26484,
delta1222. Both use ring884 and unchanged buffer code. Compact90s and baseline45s
sources completed naturally; compact GATT118/512bytes/MTU517/60.246s completed.
No actual receiver fault or missing counter was reported. Original broad verdicts
remain NOT_QUALIFIED due initial Learning-clock acquisition. Epochs2/2 and1/1
are independently constant across separate boots. No stable/all-codec claim.

Fresh dev.8 M7 build/audit passes168 library units/seven defaults and produces
the identical exercised8c47 image. No decoder arithmetic/SBR/radio/clock/buffer
change. One PCE is allocated at open; generic APIs retain16program capacity.
The result is not free-system heap, SDRAM, stack, CPU, Apple/HE or full fidelity.

LHDC192/1000BLE supply failures remain unresolved. Typed controls, full AVRCP,
HFP/HSP/RFCOMM/SCO/pluggable input, other configurations and physical/long-soak
qualification remain required. No old failure is rescored. Main is unchanged.

## Historical dev.7 record (retained verbatim)

# Develop status: 0.1.0-dev.7

Experimental development snapshot, 2026-10-25. Stable=false; the general audio
supply problem remains unresolved. Original component notices and grants remain
unchanged. All supplied decoder sources and all seven build defaults remain.

## Current correction and validation

Only receiver/compiler source `src/owned/OwnedHost.cpp` changes from develop
03931ab. The actual applied Classic security predicate is now separate from
the empty-deferred-queue readiness predicate. Actual loss still stops audio;
new profile preparation, Classic poll and AVRCP remain readiness-gated. No
receive/FIFO/credit/owner/codec/buffer/radio policy is relaxed.

Earlier selected typed scalar checks recorded 20 checks per mode, parent five
expected failures and candidate zero failures across C++11/14 plain and
ASan/UBSan. Those selected checks do not exercise the full transport or all
profile branches and were not rerun as a new full native suite. A fresh dev.7
M7 build and static audit cover all 168 library units and seven decoder
defaults. Its binary matches the exercised reviewed image exactly.

One bounded normal BLE detach/attach and GATT60 completed 98 requests, acks and
512-byte echoes, MTU 517, actual 120 ms and 60.047 seconds of client activity.
Audio/output epochs stayed 2/2, but actual audio failed: 1136 short refills,
218112 missing-real PCM frames and eight timestamp/inner-sequence steps. No new
outer RTP sequence gap, decoder, drop, full queue, DMA or transport error was
reported. Missing-real output includes recovery withholding and is not a
literal zero-sample or acoustic-loss duration. Barrier activation was not
observed, so the field case cannot prove that the conditional branch executed.

Candidate and restoration full readbacks passed; ordinary output resumed.
Restored-image audio quality also failed and remains separately recorded.
No case is relabeled a stable or all-codec pass. See BUILD.json for source,
artifact and qualification scopes. Main is outside this development update.

## Remaining requirements

Resolve unchanged-epoch supply gaps and BLE coexistence; verify codec/fidelity,
ARM efficiency, lazy allocation/SDRAM and longer normal playback. HE/Opus and
distinct SBC-XQ sender interoperability remain unqualified. Full typed AVRCP,
RFCOMM/SCO HFP/HSP and a pluggable microphone API remain unfinished. The PCM5102A
has playback output only. Alternate I2S/MCLK electrical output, low-24-bit PCM
fidelity, RF delivery and acoustics are not established by software counters.

## Historical dev.6 record (retained verbatim below)

# Develop status: known issues retained

0.1.0-dev.6 developmental snapshot metadata,2026-10-24. Source fixed=false;
stable=false. All257 supplied decoder files and all eight codec identities
remain included under their original component terms and unresolved grants.

## Selected code and build

The frozen source baseline is the clean observer-free production code tested
as image `1b080275e7dd79b335dbc489b9f924092dfc82780e455789a526233085e49601`,
488004 application bytes. It contains the reviewed TX admission ordering,
one-second early preference policy and initial optional-notification gate.
The actual M7 build used dev.5 library metadata; this final source metadata
uses dev.6. Its470-file/363-row/168-TU source identity and actual M7 facts are
in [BUILD.json](BUILD.json). A fresh extracted final dev.6 ZIP build remains
required; old full-properties build reuse cannot satisfy it.

The rejected initial-age0 eligibility experiment, private compact AAC
allocation promotion and unqualified receive-clock/diagnostic changes are
not part of this baseline. A scalar experiment or an alternate image does
not inherit the production image's finite results.

## Current field verdict: NOT_QUALIFIED

Five ordinary45s codec software gates passed: SBC44100, AAC-LC44100,
aptX48000, aptX HD48000 and LDAC96000. The LHDC V5 192000Hz,24-bit,
nominal1000kbps-selection case failed; the six-case matrix stays
NOT_QUALIFIED. It is not repaired or waived for the developmental snapshot.

The helper connected late, about20.947s after its first phone log. MTU517
and subscription completed, but the existing22s deadline ended the client
without a completed ten-write/ten-echo exchange. A board-side handled value
is not proof of phone acknowledgement or notification delivery.

The retained receiver failure snapshots show63 then101 missing-input refills,
12096 then19392 missing-input frames, and repeated recovery entries while
the source had not yet been explicitly stopped. Most missing-input slots
were intentionally held during reserve recovery; these counters are not an
acoustic-loss measurement. RTP packet gaps stayed0 but timestamp/inner
discontinuities became1. First actual starvation preceded the first recorded
RTP step by130463 receiver microseconds, so that later logged step cannot be
treated as an earlier logged cause of the initial starvation.

Actual120ms connection parameters and zero decoder/drop/DMA/transport errors
do not cancel these positive shortages. Sampling does not establish the
precise physical update time, RF airtime or a unique cause. Own-source route
and AudioTrack progress did not show reported source underruns, but this
does not establish encoder/controller timing. The case source was cancelled
before natural45s completion. Hardware restoration and ordinary resume were
recorded separately; successful cleanup does not change the failed verdict.

A separate intended600s diagnostic-image run also remains NOT_QUALIFIED:
the fourth LDAC reconnect returned callback status133 before a qualified600s
completion; the intended LHDC600s case did not run. That observer-image
result is independent and is not production long-playback evidence.

## What is and is not established

The source retains all decoder defaults and configuration-sized ARM/codec
allocation, one-codec-at-a-time negotiation, the existing clock API and
recovery accounting. A fresh production static M7 audit passed with all
168 library TUs, the owned host, original controller patch and M7 softfp/FPv5
attributes. Static linking is not current all-codec playback qualification.
Prior component scalar and positive decoder/allocation evidence is retained
only under its original source/image identities; it is not a new full-source
native run for this production snapshot.

SDRAM placement, worker-stack high-water, full low24-bit fidelity, independent
LHDC PCM reference fidelity, HE/Opus sender interoperability, physical I2S
accuracy and acoustics remain open. A32768-byte linked main stack is not
a measured stack high-water result. The phone cannot be assumed to select
every supplied decoder. HE-AACv2/PS, xHE-AAC and BLE Audio remain unsupported.

Serial logging keeps the bounded main-thread console ring,64-byte nonblocking
USB pump and diagnostic drop counters. It does not wait for an absent USB
monitor during streaming. This existing logging behavior does not prove a
zero-cost console or explain the audio regression.

## Historical dev.5 results

The old dev.5 image
`210791643c83b4de1f79ae45c55e741511d392cfd311ad74304956d161b0cf10`
had a fresh source ZIP build/full application readback, six30s software gates
and a separate passive-BLE LHDC192 observation covering1802.520 receiver
seconds. Dated reports retain earlier reference/decoder/allocation evidence,
older continuity failures and their finite limitations. They are historical,
not new results for the selected dev.6 code or a stable-release claim.

LICENSE, NOTICE, THIRD_PARTY and vendor/original/imported source identities
are unchanged. No new redistribution, patent or table-data grant is claimed.
The development snapshot may document these known failures without declaring
a six-case PASS. Main and stable-release decisions remain separate.

Fixture repair scope: tools/test_audio_clock.py now extracts the production observer field block/counterEnabled into its model; tests/test_clock_priority.cpp adds read-only register/pending stubs and the seventh observe selector while retaining the original six checks/native flags. Root reproduced the stale parent missing-member compile failure and compiled the patched model to an official ARM object with exit0/quiescent. That smoke compile suppressed only the preexisting GCC7 misleading-indentation warning at unchanged I2sClockTrim.h211. It executed no native/runtime assertions; this is not an all-tests-pass result. The production source and ordered365 input set are unaffected by those two fixture leaves.
