# Historical public-export audit

The text below is the original2026-10-21 documentation-scaffold audit, retained
for provenance. Its scaffold-only inclusion decisions describe that date.
The current supplied-source inventory and unresolved grants are recorded in
../../THIRD_PARTY.md. This history is not a new redistribution grant or a
replacement for original component terms.

# Public source licensing and export audit

Audit date: 2026-10-21. This is a file/provenance inventory for the public
repository, not a blanket legal clearance or a codec patent license.
Apache-2.0 applies to project-owned contributions covered by the project's
LICENSE/NOTICE. It does not replace third-party terms. No MIT relicensing of
third-party code is asserted.

The initial public export is a documentation-only scaffold. It includes no
library sources, vendor trees, encoded/PCM vectors, generated fixture headers,
controller firmware, firmware images, raw captures or local Git history. All
original local files remain unchanged. ALLOW-CONDITIONAL below means an
identified copyright path exists subject to its conditions; it is not an
instruction to include that component in the current scaffold.

## Component decisions

| Component / local evidence | Copyright/license evidence | State / public-export decision |
| --- | --- | --- |
| Project-owned documentation and LICENSE/NOTICE | Repository owner selected Apache-2.0. These new public documents are written for this export. | ALLOW: exact audited documentation files only. Preserve LICENSE and NOTICE bytes. |
| SBC / SBC-XQ: `src/vendor/codecs/sbc/`, including `include/oi_codec_sbc.h`; retained ESP-IDF license under notices | Android/Open Interface file headers explicitly select Apache-2.0. The primary Android source retains that grant. XQ is a configuration of SBC, not a separately licensed decoder. | ALLOW-CONDITIONAL copyright path. Before source export, preserve all attributions/license text, verify the complete selected-file chain and add required prominent modification notices where needed. Excluded from scaffold. |
| AAC-LC and experimental HE-AAC v1: `src/vendor/codecs/aac/` | Helix headers name RealNetworks and refer to RPSL, or RCSL if separately licensed. A matching complete grant/obligation set for this imported snapshot has not been established by this audit. | HOLD. Do not substitute Apache/MIT or a different Helix product EULA. Obtain the applicable full license, version/ownership chain and compliance plan before inclusion. |
| aptX / aptX HD: `src/vendor/codecs/aptx/freeaptx.{c,h}`; `aptx-family/main/COPYING` | The actual decoder headers and upstream libfreeaptx select LGPL-2.1-or-later. `aptx-family/LICENSE` is a CC0/public-domain template statement; it does not override the LGPL notices of the imported library. | HOLD for this export. Source may have an LGPL distribution path, but requires retained library terms, applicable modification notices and an explicit source/binary compliance plan. Static Arduino firmware/relinking obligations need separate review. No Apache/MIT conversion is made. |
| LDAC: `src/vendor/codecs/ldac/` | Local `ldacBT.h` retains Sony copyright; the decoder snapshot has no identified redistribution license. Its original README describes a decoder implementation and an encoder/all branch. AOSP's Apache-licensed `libldacBT_enc` is an encoder and does not establish a grant for this separate decoder snapshot/additions. | HOLD. Require an applicable license and exact source/author/version mapping for these files, including tables and decoder additions. Do not infer permission from the AOSP encoder's license. |
| LHDC V5: `src/vendor/codecs/lhdc/` | No license grant or authenticated authorship/rights chain was found in the imported decoder files/collection notices inspected. Source availability and successful decoding do not supply that missing grant. | HOLD. Obtain ownership evidence and an explicit license for this exact implementation and its tables before public source export. |
| Opus: `src/vendor/codecs/opus/`, `COPYING` | Retained upstream BSD-style copyright/disclaimer and endorsement conditions; Opus's primary license page identifies the implementation as BSD-3-Clause and separately describes patent grants/conditions. | ALLOW-CONDITIONAL copyright path. Preserve per-file notices and COPYING, pin/verify the imported snapshot and modifications, and retain applicable patent notices. Excluded from scaffold. |
| Historical Classic host: `src/vendor/btstack/` | Retained BlueKitchen terms restrict redistribution/use/modification to personal benefit and noncommercial purposes; upstream describes separate commercial licensing. | HOLD from the Apache project-owned export. Do not push the local baseline/history or call it Apache code. Keep the original notices and private baseline intact. |
| New project-owned host / GATT-server work | Project-owned contribution license is Apache-2.0 as selected by the owner. Source and integration are concurrently under development/review. | HOLD from initial main until ownership review, source freeze, host/native/ARM checks and a reviewed buildable snapshot. BLE scope is GATT server only; concurrent Classic/BLE hardware behavior is untested. |
| Arduino core/controller patch firmware | Firmware is linked from the installed core in local builds, not copied as a standalone source asset here. This audit does not establish a right to publish firmware/readbacks. | HOLD all firmware, application images and readbacks. Public installation instructions may name an official dependency later; no firmware blob is exported. |

Primary evidence: [Android SBC header](https://android.googlesource.com/platform/system/bt/+/ea7ab70a711e642653dd5922b83aa04a53af9e0e/embdrv/sbc/decoder/include/oi_codec_sbc_private.h),
[libfreeaptx source](https://github.com/iamthehorker/libfreeaptx/blob/master/freeaptx.c)
and [COPYING](https://github.com/iamthehorker/libfreeaptx/blob/master/COPYING),
[Opus licensing](https://www.opus-codec.org/license/),
[AOSP LDAC build/license scope](https://android.googlesource.com/platform/external/libldac/+/master/Android.bp)
and [LICENSE](https://android.googlesource.com/platform/external/libldac/+/refs/heads/main/LICENSE),
[BTstack upstream](https://github.com/bluekitchen/btstack/blob/master/README.md).
The Helix decision uses the retained per-file notices; no current product EULA
is treated as a replacement grant for those files.

## Fixture and evidence decisions

| Asset group | Evidence | State |
| --- | --- | --- |
| Procedurally generated 100 ms stereo input and independently encoded/reference-decoded SBC, AAC-LC, aptX/HD and Opus tests | Local fixture provenance describes owned synthetic 1/2 kHz input and records encoder commands/hashes. Existing commands contain private workstation paths. | HOLD from scaffold. A later explicitly owned synthetic-data export needs a public provenance manifest without workstation paths and an applicable codec/distribution review. Software license terms are not automatically a license for arbitrary external media, nor a codec patent clearance. |
| PipeWire SBC-XQ and LDAC fixtures | External-fixtures metadata points to another local project and gives hashes/cases, but is not a complete public ownership/license record for each input and encoded fixture. | HOLD until exact generator/input ownership and distribution terms are recorded publicly. Do not infer coverage merely from the encoder's source license. |
| Official DecoderCheck HE package, SBR-only mono WAV and generated prefixes | Retained Coding Technologies package agreement requires whole-package, unmodified copying and excludes commercial distribution without permission. | HOLD all package contents and extracted/generated media/header/firmware derivatives. Local test use is not permission to publish selected eight-frame prefixes. |
| Independent stereo HE sample | Local provenance explicitly limits it to regression use; an applicable public redistribution grant is not established. | HOLD media, PCM references, generated fixture headers and embedded firmware. |
| Phone captures and other recorded media | May contain device identities, transport keys, user/app data or captured encoded media. | HOLD raw captures and capture JSON/text. Publish only a separately reviewed, newly written aggregate summary. No byte dumps, MAC/USB identities or host paths are exported. |
| Existing private validation/provenance receipts | Valuable local evidence, but some contain absolute workstation paths and private device metadata. | HOLD original receipts. The public README uses bounded aggregate observations and distinguishes the historical host from new owned-host work. |

Source hashes and copied-file manifests remain local audit inputs. A hash match
proves file identity, not ownership, license permission, codec patent rights,
interoperability or acoustic output.

## Public-export gate

`tools/public_export.py` uses an exact source-to-destination allowlist. It does
not reuse the broader development-package whitelist, walk vendor/source trees,
copy Git metadata/history, or make release ZIPs. It rejects absolute machine
paths, private identity markers, non-text payloads and credential/firmware/media
file types in selected public documents. LICENSE/NOTICE are copied unchanged.
The public third-party summary is a separate document; the private original is
preserved.

Only a fresh, reviewed export directory should seed public main. Development
code belongs on develop after its own ownership and build review. A release tag
and installable ZIP must come from a reviewed buildable main revision with
checksums and accurate support claims. The documentation scaffold has no such
release artifact and must not be labeled a qualified audio library.

## Identified local notice samples

These SHA-256 values identify the exact inspected local files. They do not turn
a notice sample into a full-tree permission grant; held components stay held.
No private source paths or raw media are reproduced.

| Evidence | Relative file | SHA-256 |
| --- | --- | --- |
| SBC Apache header | `src/vendor/codecs/sbc/include/oi_codec_sbc.h` | `ce7dc1330e1f3109f530f86a2606a1709b3ff790df3d2c2d747c01a353fea4a4` |
| Helix retained notice | `src/vendor/codecs/aac/aacdec.c` | `5aefa92b9a944c03becbea04c6a411028061f8cadaa472bfb76efc139bf4f3bf` |
| freeaptx core notice | `src/vendor/codecs/aptx/freeaptx.c` | `b98f960f83232e8b9903cf0ce86a24354b900a9ea14166f62661f8da7baddd3c` |
| freeaptx LGPL text | `src/vendor/codecs/aptx-family/main/COPYING` | `dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551` |
| LDAC copyright-only header | `src/vendor/codecs/ldac/inc/ldacBT.h` | `9159623adcb61a6124868415e82c6b989f4833e5089d5767f796a713852b70e5` |
| LHDC inspected public header | `src/vendor/codecs/lhdc/lhdc_dec.h` | `cd59bb9ba2a099a3d26cf526676b4513493fc68edd84609a3c4cd0d3128efaa6` |
| Opus COPYING | `src/vendor/codecs/opus/COPYING` | `01e1167d54a096d123cf6dfbbeb19587278845c6481d2d66d545669846079551` |
| Historical host LICENSE | `src/vendor/btstack/LICENSE` | `10aa5f952979a9fa2f58bf63c1390c9795b41f8eca405ab975ab926e4d14fc5a` |
