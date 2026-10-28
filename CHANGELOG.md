# Changelog

## 0.1.0-dev.17 - experimental development source, 2026-10-28

- Allow ordered accepted A2DP media to progress while an eligible completed read-only SDP/AVRCP control waits for transmit/reply capacity. Park the pending control in an existing bounded spare Packet buffer before releasing its borrowed receive lease; add no per-frame heap allocation or IRQ work.
- Preserve reply FIFO, owner/CID lifetime, genuine receive credits, canonical L2CAP reassembly, security and active AVDTP checks. Mutating signaling, voice/RFCOMM, unknown/stale responses and fragmented AVCTP continuations retain conservative barriers. Revalidate matched read responses against live owner, label, kind and deadline without consuming their effects early.
- Export the faithful 69 progress/read/barrier cases, 13 matched-controller-response cases and two boundary regressions; all 84 passed on the actual exported CPU fixtures. The same production revision also passed focused existing actor suites (5758/6256/861 checks) and eight offline runner-plan checks. These are fake-wire CPU tests, not a full-suite or sanitizer result.
- Record fresh official Receiver and VoiceQualification CM7 builds/static audits: 191 library translation units and 223 compile rows each, all seven codec defaults/six decoder operation families, default 2 Mbaud UART and unchanged stock HCD. Final version/paragraph-only metadata is reconciled against the compiled source; the final ZIP is not separately compiled.
- Retain codec arithmetic, selected buffers, the LHDC 80/90 ms profile, clock policy, BLE behavior and all component terms. The host defect is real but is not established as the cause of the separate recorded field reserve depletion. This update has not been flashed; rare shortages, LHDC numerical qualification, voice interoperability, acoustics and the 200 ms total latency target remain unresolved or unqualified.

## 0.1.0-dev.16 - experimental development source, 2026-10-28

- Retain only the undecoded AAC suffix after a successful prefix followed by NeedMoreData, preserving assembly ownership and remaining compressed-work credit. The bounded helper moves existing bytes without adding an allocation; decoder arithmetic and other codec paths are unchanged.
- Reproduce the old diagnostic ADTS prefix twice in a real-decoder/frontend CPU RED, then pass all 11 strengthened cases. Intermediate retained bytes/Q16 are 100/13212 and clear to 0/0 after completion; gap/reset/overflow and ownership controls remain strict. These are synthetic ADTS tests with fake radio/I2S, not demonstrated negotiated A2DP AAC behavior.
- Record fresh official Receiver and VoiceQualification CM7 builds/static audits, each with 191 library translation units, 223 compile rows, all seven enabled codec defaults, six decoder operation families, default 2 Mbaud UART and stock HCD. The fix has not been flashed; final documentation/version metadata are reconciled against the compiled source rather than described as a new ZIP compilation.
- Preserve the separate earlier-source M7 numeric diagnostic failure: 28 safety cases passed but only 26 strict numerical comparisons matched; the two LHDC nonzero passes remain QUALITY_UNQUALIFIED. Eight zero/lifecycle cases and 328 balanced provider allocations/frees do not turn that numerical failure into a pass.
- Preserve the original clock-fixture failure on published baseline and candidate, then service the real asynchronous host mailbox in the fake-host harness. Corrected baseline and candidate each passed 394 CPU checks with all prior assertions retained; production clock source is unchanged. Media retention (35 checks), five output-wait/lifecycle modes and receiver lifetime (349 checks) also passed. This is finite CPU regression evidence, not a full-suite/sanitizer or hardware-clock claim.
- Retain dev.15 voice, BLE, I2S/buffer/clock policies, supplied codecs and component terms. Existing LHDC playback shortages, controller/call interoperability, acoustics and the 200 ms total latency target remain unresolved or unqualified. These source and test updates have not been deployed; no stable-audio or LHDC continuity claim is made.

## 0.1.0-dev.15 - experimental candidate, 2026-10-27

- Integrate the frozen opt-in HF/HS voice source, bounded synchronous transport and worker, conditional SDP and a copied microphone-input provider API. Voice remains off by default; no provider means silence.
- Retain dev.14's mono HE-AAC allocation reduction and all existing codec defaults and notices. The combined candidate passed Receiver and VoiceQualification CM7 builds and static link audits; independent review and hardware qualification remain.
- Retain six decoder-operation families, eight Codec identities and seven enabled defaults: SBC/SBC-XQ, AAC-LC, HE-AAC v1, aptX/aptX HD, LDAC, LHDC V5 and Opus. SBC-XQ shares the SBC identity. All-codec physical operation remains unqualified.
- Seventeen combined-source ARM64 CPU fixtures passed with fake radio/I2S and single-thread reentry; focused byte-pacing and HF admission cases also passed. These results do not establish controller or call behavior.
- Controller routing, phone call interoperability, real microphone input, long calls, acoustic output and the 200 ms total phone-to-DAC target remain unqualified. Existing long-playback PCM shortages remain unresolved; this candidate is not an audio-continuity fix.

## 0.1.0-dev.14 - experimental development source, 2026-10-27

- Allocate the HE-AAC envelope-dequant tail for the opened channel count in the same owned state block. Mono HE payload/provider requests are 960 bytes lower in paired portable CPU fixtures; LC/stereo bytes, block counts, PCM and framing/arithmetic remain unchanged in those tests. The M7 compiler confirms the geometry and a 56-byte InitSBRState frame; MCU allocation/PCM execution remains unverified.
- Integrate an optional serialized RFCOMM/HF/HS control owner with secure PSM3 admission, durable copied AT queues and scoped local/remote channel retirement. The default has no voice arena and retains PSM3 refusal; the M7 actor type adds 28 bytes.
- Retain unadvertised voice profiles: SDP voice records, real SCO routing, synchronous PCM worker/I2S arbitration and a public pluggable microphone path remain unfinished. Voice-only mode is not enabled.
- Preserve all existing codecs/defaults, operation-mode examples, I2S pin options, 80/90-ms LHDC reserve, BLE policy, clock control, stock HCD and component notices. Brief long-playback shortages remain unresolved; this is not an audio repair or stable release.
- Record a fresh combined CM7 build and exact versioned source ZIP separately from the earlier component builds and portable CPU evidence.


## 0.1.0-dev.13 - experimental development source, 2026-10-27

- Add public typed AVRCP transport taps, copied metadata/status snapshots, owner-bound cancellation and separate response outcomes.
- Preserve ordered AVRCP press/release and add scoped queue/mailbox retirement before channel reuse; expired or stale replies cannot complete a fresh request.
- Add standalone owned RFCOMM/HFP/HSP protocol modules and an internal None/Provider microphone PCM contract. They are unadvertised; RFCOMM host ownership, SCO/duplex voice and a public microphone registration API remain unfinished.
- Record 28 passing portable Android ARM64 CPU cases and the separate CM7 package build. No phone interoperability or dev.13 firmware deployment is claimed.
- Retain all existing codecs, lazy selected-codec buffers, LHDC80/90 defaults, I2S, stock HCD and BLE policy. Brief long-playback PCM shortages remain unresolved; this update is not an audio continuity repair.
- Main stays on its separately maintained reviewed snapshot; this is development source.

## 0.1.0-dev.12 - experimental development source, 2026-10-27

- Raise only the LHDC automatic base startup reserve from60 to80ms; its default ring becomes90ms. At192kHz this selects a141344-byte DMA/ring arena (+30720bytes), rather than allocating other codec arenas. Keep DMA halves, radio commands/HCD, other codec bases and clock algorithm unchanged.
- Preserve explicit manual60/70ms and legacy packet startup. An explicit70ms LHDC ring without a startup override now safely rejects the inherited80ms watermark; document and compile-check this compatibility effect.
- Record the public60/70ms baseline failure:59328 missingPCM frames,309 underrun callbacks,135 verified GATT exchanges, no RTP discontinuities or decode/transport errors. Record the80/90ms SCHED0/3M candidate600s window:130 verified GATT exchanges across3cycles, zero recorded audio faults, but14 clock overruns. No universal cure/stable/RF/fidelity/latency claim.
- The version12 fullCM7 build/link audit retains170 libraryTUs, six codec operation families, seven defaults and stockHCD; its BIN is byte-identical to the field image. ARM constexpr buffer/override checks pass, including the observed76053us arrival-gap storage screen. Native Windows runtime tests were not executed.

## 0.1.0-dev.11 - experimental development source, 2026-10-27

- Add a shared controller-UART baud admission policy and exact-value checks. Keep 2 Mbaud by default and admit experimental 3 Mbaud through `compiler.cpp.extra_flags`; a sketch-local define cannot configure separately compiled library C++ files. Keep 4 Mbaud unsupported by this port.
- Record the public opt-in 3 Mbaud SCHED0 full CM7 compile and linked-image audit (170 library translation units, all six codec operation families / seven defaults, project host and unchanged stock HCD). The compile-time policy assertions pass with the official ARM compiler; both integration units also compile at default 2 Mbaud. The public image was not deployed; the full Windows native test suite was not executed.
- Record a different private 3 Mbaud image's 600-second ordinary-source LHDC V5 192 kHz/24-bit playback test: three BLE reconnect/data cycles, 135 verified 512-byte GATT exchanges, zero recorded audio/RTP faults, but 19 clock CUM overruns. Its 80/90 ms buffer override and timing observers differ from public defaults. BRR=40/PRESC=0 and normal HCI/GAP responses establish transport operation; `kernel_hz=0` remains UNKNOWN. Earlier shortages have no demonstrated root-cause fix. Preserve HCD, codecs, buffers, licenses and all stability/fidelity/latency limits.

- Add A2DP-only, BLE-only and combined examples using explicit profile selection. All three have full Cortex-M7 build evidence; operating-mode hardware validation remains incomplete.
- Use 921600 in all example serial setup calls and document native USB CDC baud behavior. The six edited sketches compile with the ARM SDK flags. This does not establish higher USB throughput or repair the unresolved audio shortages.

## 0.1.0-dev.10 - experimental development source, 2026-10-26

- Select automatic packet/PCM storage for the negotiated configuration and wait for real decoded PCM before DMA startup/recovery. Keep explicit per-codec overrides and the legacy startup_packets path.
- Use the configured running inventory target T=P-2H for real-PCM startup, retained through learning/recovery. This target includes estimated compressed work; it is not a real-PCM floor or measured total latency.
- Retain all six supplied decoder-operation families/eight Codec identities/seven defaults, hardware I2S pin configuration, original controller HCD and all root/component terms.
- Record the actual private SCHED1 LHDC192 90.072132986 s / GATT 46 window: 43 non-clock gates zero, but clock CUM FAILED 2/88, max 22.1229167 us. Five printed tracking cohorts remain UNKNOWN; supply witness unexercised and prior starvation unresolved.
- Record actual SCHED0 M7 valid-vector execution: 12 cases, 20 passes, 3440 successful calls and complete-case oracle/deadline/status/heap-close gates valid. Capture remains FRAMING_DEGRADED with one partial replay row; no global PASS or A2DP/I2S qualification.
- Keep LHDC golden MISSING, LDAC tone-only, aptX HD PCM16/HE SBR/Opus fixture limits, unfinished call/microphone/typed-AVRCP features and all historical failures visible.
- Root extracted the 503-file build-basis Git archive cffc98e8... from c5db0292..., compiled the default SCHED0 Receiver in 104.422 s (exit 0/quiescent) and completed static audit: 170 library TUs, all six Ops/seven defaults, stock HCD, no RTX wraps, 32768-byte main stack. Actual BIN cf7132ca.../496552 bytes; no parity to private e417 or field-deployment claim.
- This final revision changes documentation only. Root verifies final archive members, all 373 compiler-input bytes and unchanged library.properties version against that actual build basis before develop/tag publication. The final ZIP is not claimed separately compiled; its SHA256 stays in an external sidecar. Main/stable/audio-cure/E2E claims remain absent.

## 0.1.0-dev.9 - experimental development source, 2026-10-26

- Prepare source from the exact corrected all-codec f42 compiled tree; production C/C++/headers and seven license/notice files remain byte-identical to that input. R2 also corrects the distributed LHDC exported hash/size record and includes two nonproduction fixture repairs; it is not a rebuild of f42.
- Retain owner-scoped controller priority, LE lifecycle budgets, strict interval rejection with per-generation counter, and bounded clock SUM/WORST/RESTORE witnesses. All original timing/ownership/restore gates remain.
- Include the one-leaf finite LHDC float-to-S32 saturation correction already compiled in f42. It is not proof of clipped-sample occurrence, full24-bit fidelity, pop cause/cure or clock repair.
- Record ECA4's six finite phone-family windows and retained cumulative clock failures9/1/1/0/4/5; record the new f42 LHDC run90.066154810s/GATT43 with4/91 overruns. No before/after causal or stability claim.
- Preserve compact AAC selected-memory history, unsupported sender/feature limits and all earlier failed evidence. Total200ms latency and acoustic/low-bit fidelity remain unproved.
- No ZIP, tag, main/develop change or publication was performed in this preparation. Root must verify the actual remote branch and final extracted-archive/build provenance before publishing develop only.

Earlier dev.8/dev.7/dev.6 records remain in docs/STATUS.md and docs/BUILD.json; they are not transferred into current qualification.

### R2 review corrections (subsequently built)

- Correct only the existing LHDC vendor-manifest exported_sha256/exported_bytes fields to80d3672c.../94400; retain original/upstream/grant semantics and every other record. New ordered input projection differs in this manifest row only.
- Repair README encoding to ASCII.
- Include Root-verified tools/test_audio_clock.py and tests/test_clock_priority.cpp; no claim that all424 code files remain exact once the nonproduction fixture CPP changes. Production source stays exact prior f42.
- Root subsequently verified/extracted the R2 archive and built/audited its actual source. R3 is the separate documentation-only postbuild finalization; see evidence below.

Fixture repair scope: tools/test_audio_clock.py now extracts the production observer field block/counterEnabled into its model; tests/test_clock_priority.cpp adds read-only register/pending stubs and the seventh observe selector while retaining the original six checks/native flags. Root reproduced the stale parent missing-member compile failure and compiled the patched model to an official ARM object with exit0/quiescent. That smoke compile suppressed only the preexisting GCC7 misleading-indentation warning at unchanged I2sClockTrim.h211. It executed no native/runtime assertions; this is not an all-tests-pass result. The production source and ordered365 input set are unaffected by those two fixture leaves.

### R3 documentation-only build-evidence finalization

- Record the actual475-file R2 ZIP verification/extracted official M7 build106.797s, exit0/quiescent, and corrected all-codec static audit.
- Preserve the failed first audit receipt. Root verified only guard JSON CRLF/LF serialization differed, the map/all469 files were identical, and one expected serialized hash was corrected. No source/build/hardware rerun occurred.
- Record exact byte parity to field-tested f42 binary493052B and the new package ELF439ff6ba.../separate linked-instruction review. Finite LHDC clock failure4/91 remains; no new hardware, clock-cure, full-precision, acoustic or E2E claim.
- Keep all365 code-input hashes/projection2cb3a3a2... unchanged. All production/Receiver/properties/native-fixture/license bytes match R2. Only existing documentation changes; final R3 ZIP identity stays external and Root verifies parity before develop-only publication. No stable/main/tag claim is made.
