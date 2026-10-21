# Audited source export policy

The public develop export is a new file tree created from an explicit per-file
allowlist. It includes all 257 selected supplied decoder files, project-owned
port/runtime helpers and the original import manifest. It does not copy
repository history or recursively mirror folders.
Vendor files are pinned by SHA-256 and retain relative input mappings; a changed
vendor input requires another audit. Project-owned paths are explicitly listed
and their actual bytes are hashed in each receipt. No inferred glob additions
are allowed. Export has no publishing, Git, archive or hardware operation.

Export-copy changes are restricted to rejecting the unavailable historical
host, removing its guarded bridge from
the project-owned radio parser, narrowing library metadata, and adding dated
modification notices to previously modified vendor files. Local sources stay
unchanged. All other selected bytes, including LICENSE and Opus COPYING, are
copied exactly. `CodecBuildConfig.h` is copied unchanged with all local decoder
defaults enabled. The previous automatic licensing-HOLD feature exclusions do
not apply to this full supplied-source development profile. Unresolved grants
are documented in THIRD_PARTY rather than portrayed as license clearance.
No codec fixtures/media are included in this snapshot.
The pinned SBC utilities header retains its original escaped illustrative
filename in an API documentation example. That exact upstream
literal is the only file-specific path-scanner exception; no workstation path
is exported, and other absolute paths in that same header are rejected.
The supplied LGPL COPYING text also keeps its original form-feed page breaks;
the control-byte exception is restricted to that pinned license file.
The exporter also refuses compilation-source drift against the isolated build
receipt. Changed source needs a new build and reviewed evidence before export.

Every written file is reread and checked against its expected byte length and
SHA-256, and the full entry set must match exactly. Symlinks/reparse points,
unsafe paths, binary content, private workstation paths, credentials and
unlisted output entries fail verification. The external receipt records the
snapshot as developmental source with target qualification pending. Build
outputs and private machine build logs remain outside the exported library.
