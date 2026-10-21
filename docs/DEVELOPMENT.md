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
