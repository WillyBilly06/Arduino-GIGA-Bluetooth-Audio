# Experimental dev.14 source and packaging boundary

This package combines only the frozen AAC mono allocation patch, optional voice-control owner and release documentation. It retains all unrelated codec, buffer, clock, I2S, controller-patch and LE source bytes. Existing root/component notices and unresolved grant boundaries remain unchanged; the project license does not relicense supplied decoder components.

The versioned Arduino ZIP is created from one local Git commit and checked member by member against its final manifest. Compiled source, Receiver and library.properties are pinned before a fresh combined CM7 build; later edits are documentation only. The final extracted archive is not separately recompiled. The ZIP self-hash is stored in an external sidecar. A remote push/tag is a separate Root-reviewed action; main is unchanged.

[Dev.14 validation](validation/dev14-2026-10-27.md) separates the new combined build, prior component build attempts and actual portable CPU results. No dev.14 firmware deployment or phone/call test is claimed. Long-playback shortages and complete voice support remain unresolved. No private first-shortage recorder, SDK heap logger, raw capture, workstation path or device identifier is added.

## Historical dev.13 and earlier export records

# Experimental dev.13 source and packaging boundary

This development package adds only the reviewed profile implementation plus release metadata and documentation. It retains every original codec/buffering/I2S/HCD/GATT source outside the approved AVRCP owner/queue changes and preserves all root/component license and notice bytes. The versioned ZIP is built from a local Git commit and checked member-by-member; its SHA256 remains in an external sidecar. Remote publication is a separate action. The main branch is unchanged.

[Dev.13 validation](validation/dev13-2026-10-27.md) distinguishes the actual package CM7 build from the 28 portable CPU cases and all older field images. This package has not been flashed or tested with a phone. HFP/HSP remain unadvertised groundwork, and the existing intermittent PCM shortages remain unresolved. No private first-shortage recorder, raw capture, device identifier or workstation path is added. Original component terms and unresolved grants remain unchanged.

## Historical dev.12 and earlier export records

# Experimental dev.12 source and packaging boundary

The version12 SCHED0/opt-in3M fullCM7 build produces BIN68e0879498d369ed2f008e541cfdf34ec53e0c6e9559b295489dee610f85ae3d, byte-identical to the readback-verified80/90ms LHDC field image. Final documentation-only archive members are checked against the compiled source/Receiver hashes; the final extracted ZIP is not separately recompiled. The finite field window had no recorded audio faults but14 clock overruns; full qualification remains incomplete. DefaultUART remains2M, which has integration-object checks only in this update. See [dev12 validation](validation/dev12-2026-10-27.md). Root/component terms remain unchanged; main is outside this development update.

## Historical dev.11 and earlier snapshots

The following text retains its original version/time scope.

# Experimental dev.11 source and packaging boundary

Dev.11 adds the opt-in 3 Mbaud controller-UART admission policy to the existing operation-mode examples. Its public SCHED0 Receiver has an actual full CM7 compile and linked audit; the final documentation-only archive is checked against those exact source and Receiver input hashes. That is not a fresh compilation of the final extracted ZIP or a deployment of its public defaults. [Dev.11 validation](validation/dev11-2026-10-27.md) and its JSON identify this build separately from the private field-test image and all older evidence. Default UART remains 2 Mbaud. The original project and component terms are preserved. Versioned development tags/ZIPs do not establish stability, codec fidelity, acoustics or total latency. Main remains outside this development update.

## Historical dev.10 snapshot

The following sections retain their original step/time scope; they are not dev.11 build or export evidence.

# Experimental dev.10 source and packaging boundary

This checkout prepares develop-only version 0.1.0-dev.10 from source basis 0449e41630dae0ac1f02b717c18100687c1cc33d. It retains all supplied decoder families/identities/defaults, project-owned host, stock controller HCD and original component licenses/notices. This final step changes documentation only; the already-built library.properties version/description and all 373 compiler-input bytes remain unchanged. Baseline 501 files/373 Receiver compiler rows/170 library TUs identify the code basis, not the final export member count.

The actual private Receiver build is SCHED1 BIN e417e6adc814177267f0387de5082c2428eaa364913686a4b04487f5f13a7c4a (503812 bytes), ELF b549d04900939f3cdc1a086e4b4fed2f40522f32553786d2575c31de53912d6d. It has fresh linked review and the bounded V11 LHDC192 field evidence recorded in current validation; clock CUM still FAILED 2/88 and supply remains unexercised. The SCHED0 decoder-only benchmark is a different actual image 3c7e880a... with 12 complete retained cases and CAPTURE_FRAMING_DEGRADED. Neither is a fresh default Receiver ZIP build or blanket qualification.

Root actually extracted the 503-member Git build-basis archive cffc98e8f260727c9c678f1f598703239717f2cb6a3428c1c84d38944eea1969 from commit c5db02928f7e4eaee3cf7f6557c8f0e58a9ad636 and completed official default Receiver SCHED0/no-RTX-wrap M7 compile 104.422 s (exit 0/quiescent) plus static audit. BIN cf7132ca0b9442c71e54bc026f18ee4942382fb46822f750fc1b2b3625ac337e/496552 bytes and ELF c5c15b7dd05ba9f045f72822ae7cb3ecb7fd320b592d8a2624608711b1291da9 have their own actual facts c55f41fad0f312ccaab5baf698d2d0591e943f00ad383621727f71f047e42301. No default-image parity to private e417, field deployment or stability is inferred. Existing BUILD.json is preserved in history.dev9_and_embedded_earlier; current_dev10 and planned_current_package_code_basis separate new evidence from old snapshots. All failed/unknown timing, supply, fidelity and interoperability results remain.

The [versioned v0.1.0-dev.10 source-ZIP URL](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/tags/v0.1.0-dev.10.zip) is prospective until Root completes develop-only publication/tagging. Main is outside this update; experimental development source is not stable. The final documentation-only ZIP has not itself been compiled. Root verifies its members, all 373 compiler-input bytes and unchanged library.properties version against the actual compiled build basis before publication. Keep the final ZIP SHA256 in an external sidecar; do not put its self-hash inside the payload.

No private HE reference media, benchmark fixture headers, raw captures, workstation paths or device identifiers are added to this library export. Dated validation publishes sanitized artifact identities and metrics only. Historical source/component mappings and all root/vendor licenses/notices remain exact, including existing unresolved grants; source publication creates no new licensing grant.

## Historical dev.9 export snapshot (retained verbatim)

Statements below retain their original step/time scope and do not assert dev.10 packaging/build/publication.

# Experimental dev.9 source and packaging boundary

The475-file R2 source ZIP was verified, extracted and actually compiled/audited. R3 starts from that frozen export and changes only existing noncompiler documentation; all365 ordered input hashes and all other files are identical to R2. All production src/example C/C++/headers/INO and the seven identified root/component license-notice files remain byte-identical to that prior compiled input. The distributed vendor manifest has one record corrected in two exported hash/size fields; two nonproduction fixture leaves have Root compile-only verification. Those metadata/fixture differences and listed documentation/library description changes are explicit; R2 added CHANGELOG.md and the dated validation MD/JSON; R3 keeps those same paths. The final external delta/manifest records every entry. No maintained root, old export, SDK, public clone or branch is modified.

The export includes the bounded clock phase/restoration diagnostic source actually linked in f42, not an observer-free artifact. All original gates and failures remain documented. Exact compiler-row bytes/projection, original component mappings/notices, actual compiled artifacts and finite field evidence have separate provenance. An identical compiler input set is not a fresh build of an extracted final ZIP.

Root created the R2 build-basis archive, verified475 members, extracted it and completed the official M7 build/static audit. This documentation-only R3 preparation creates no new archive or build. Root will verify final R3 members and the identical365 code-input hashes again. The final R3 ZIP self-hash stays in an external sidecar rather than its own payload; the R2 build-basis ZIP hash is a separate historical artifact identity.

The first R2 audit failed the expected serialized guard-JSON hash. Root confirmed CRLF/LF-only byte normalization, identical maps and all469 actual file hashes, then corrected one expected serialized hash in audit_m7_r2.py. root-audit-run.json remains a failed receipt. The authoritative root-audit-run-r2.json is exit0/quiescent, with no source/build/hardware rerun. Both identities and the correction are recorded in BUILD.json. The package ELF has its own passed linked mark-before-BASEPRI instruction review; the original build-facts PENDING label is preserved as a snapshot field with separate companion proof.


Before publication, Root verifies the actual remote branch and parent revision; experimental develop-only publication must not update main or imply a stable release. Final R3 packaging/member-and-input verification and develop-only publishing are separate Root work; R2 ZIP verification/extracted compilation are already recorded actual evidence. No tag/URL/version publication is invented by these documents. Existing Apache-2.0/NOTICE/THIRD_PARTY and all supplied vendor terms are exact; no new grant/relicense or licensing clearance follows from this source copy.

Public export metadata excludes workstation paths, device identifiers and private raw captures. External local preparation receipts retain their own source/evidence hashes without becoming library archive members. The pinned upstream SBC illustrative path and existing license form-feed bytes remain source/licensing text, not binary artifacts or new path-scanner exceptions.

The corrected257-row vendor manifest changes only the LHDC lhdc_dec.c exported_sha256/exported_bytes fields. Original/upstream hashes, grants, patch descriptions and the other256 records remain exact. It is one ordered compiler-input row, so current projection differs from prior f42 even though production code does not. The actual R2 ZIP build result is now recorded independently and is byte-identical to prior f42; R3 relies on explicit365-input parity rather than inventing another build.

Final user-documentation snapshot (R4): the475 paths and all365 compiler-input hashes/projection2cb3a3a2... remain exact to the recorded build basis. Only README readability, this snapshot note and current provenance hashes change from the prior documentation seal. The versioned source-ZIP link targets tagv0.1.0-dev.9; tag publication and final archive/member-parity checks are separate distribution actions. Final ZIP self-hash stays outside its own payload.
