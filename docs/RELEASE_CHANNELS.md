# Release channels

## Choose a download

| Channel | Purpose | Download |
|---|---|---|
| `main` | Selected tested mainline baseline, with user-facing installation documentation | [Mainline source ZIP](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/refs/heads/main.zip) |
| `develop` | Ongoing development; changes may be incomplete or less tested | [Develop branch and its current README](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/tree/develop) |
| `v0.1.0-dev.*` tags | Versioned experimental source snapshots with their own dated evidence | [Tags](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/tags) |
| GitHub Releases | Repository-wide publication records and attached assets for specific tags | [Releases](https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/releases) |

Main is the default source download. Its current library identity is still `0.1.0-dev.12`; selecting this tested baseline for main does not qualify it as stable or complete. See the main [README](../README.md#tested-behavior-and-known-limits) for current capability and limitations. Work that has not been compiled, validated and promoted is not part of main's supported behavior.

Branch ZIPs change when their branches advance. Retain the full commit SHA and configuration with a test report. To pin an exact source revision, use `https://github.com/WillyBilly06/Arduino-GIGA-Bluetooth-Audio/archive/<FULL_COMMIT_SHA>.zip`, replacing `<FULL_COMMIT_SHA>` with that revision. A tag identifies a versioned snapshot; it does not imply that a corresponding GitHub Release object exists.

## Publication policy

Mainline and develop documentation have different purposes. Main explains installation, available behavior and its limits. Develop records ongoing work and development-specific evidence. A promotion must identify the tested source/configuration and retain failed or unknown validation results. Changing branch names or documentation does not change firmware provenance, qualify missing features or resolve audio faults.

GitHub Releases belong to the repository, not to a branch-specific release page. A release's target commit or tag does not make its release listing private to `develop` or `main`. Development versions use the **prerelease** flag. Existing tags and assets retain their original identity; they are not moved, deleted or relabeled as stable to tidy the mainline page.

A future stable release needs an explicit version, pinned source, documented test scope, known limitations and verified distribution artifacts. It must be published deliberately as a non-prerelease before users are directed to a stable/Latest download. There is currently no published stable release; use the mainline source ZIP above for the selected baseline.

## Release audit: 2026-10-27

The repository release API returned five published release objects: `v0.1.0-dev.1` through `v0.1.0-dev.5`. All five had `prerelease=true` and `draft=false`. Their existing flags already express development status; no release flags, tags or assets were changed for the mainline documentation update. Later development tags can exist without release objects and must not be presented as published stable releases.

Historical `EXPORT`, `BUILD`, `STATUS`, validation and release-body statements retain their original date/version scope, including statements that main was outside a particular development update. This guide describes the current channel policy; it does not rewrite those historical results.
