# Current main development baseline

Main now contains the tested v0.1.0-dev.12 source, merged with the original
documentation-only history. The source and firmware code are unchanged from
that development tag; this merge adds only these historical policy/audit notes.

The LHDC192/24 / opt-in3M /80ms-startup90ms-ring image completed the recorded
600-second BLE coexistence window with130 verified512-byte exchanges and no
recorded audio/RTP faults. The user reported smooth playback during BLE use
and reconnects. This is development evidence, not universal stability or a
measurement of total200ms latency. Default2M has separate, narrower evidence.

A brief gap on some song transitions has subsequently been reported and is
under investigation. The retained incident overlaps a changed title and a BLE
attachment; it does not isolate either as its cause. The underrun total is
cumulative per output epoch and clears when playback is stopped/restarted.
Clock-update timing over-budget measurements are tracked separately from audio
underruns. See validation/dev12-2026-10-27.md and README.md for current limits.

Main receives reviewed snapshots incrementally; develop receives integration
work. New typed AVRCP/metadata and RFCOMM/HFP/HSP work remains in isolated
branches until compiled and behavior/target checks are complete. Call audio and
the microphone backend are not advertised as working. Existing project and
component licenses/notices and unresolved grants remain unchanged; consult
../THIRD_PARTY.md for the current supplied-source inventory.

## Historical bootstrap policy (original text retained)

# Development status and promotion notes

The project-owned Bluetooth host is under active development. Its intended
public license is Apache-2.0 for project-owned contributions. Third-party codec
licenses and any imported implementation history remain separate review gates.

Current protocol work covers controller/HCI ownership, Classic signaling and
profiles, flow control, media delivery, and BLE GATT-server support. BLE is a
server role only. Neither Classic/BLE concurrency nor the new host's complete
phone/audio path has been hardware-qualified. Native kernels, adapter tests
and ARM compilation must be reported individually; they cannot be promoted
into evidence of negotiated reception or acoustic output.

Historical controller, codec, memory and DAC diagnostics are useful baseline
observations, but the earlier receiver used a different, third-party host. Do
not attach those observations to the replacement host without new target runs.
The public README's decoder timings are finite standalone M7 measurements, not
new-host transport performance or worst-case stream deadlines.

Promotion to main requires a source freeze, ownership/provenance review,
applicable third-party compliance, successful reproducible builds and tests,
independent review, and a support matrix that names remaining target gaps.
Installable ZIPs and release tags must identify the main commit and checksums.
The current main scaffold is documentation only and has no release ZIP.

Do not publish the local repository history. The original baseline includes
third-party host code and local evidence that is outside the public allowlist.
Seed a fresh public history from the audited export. Preserve all original
private files; export holds are not deletion or relicensing instructions.
