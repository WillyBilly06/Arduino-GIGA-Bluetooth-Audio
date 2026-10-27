# Changelog

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
