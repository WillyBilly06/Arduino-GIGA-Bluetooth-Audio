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
