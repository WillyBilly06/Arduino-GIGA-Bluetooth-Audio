# Supplied decoder sources, notices and unresolved grants

This develop snapshot includes the complete selected **257-file user-supplied
decoder import**, its exact relative source/import hash manifest, project port
helpers and dated modification notices. Inclusion follows the owner's explicit
request to share the supplied development source set for noncommercial use.
That request is not represented as a blanket redistribution license or as
permission to disregard another copyright holder's conditions. Apache-2.0 in
the project root covers original project contributions, not every bundled file.

Original file bodies and notices are preserved. Previously modified imported
code receives an additional port notice, without replacing its original terms.
`docs/source-provenance.json` records each input, port and exported SHA-256;
`src/vendor/codecs/manifest.json` retains all 257 original relative mappings.
No new third-party implementation source was fetched for this export.

| Supplied component | Files | Retained evidence and limitations |
|---|---:|---|
| Android/OI SBC, including SBC-XQ configurations | 29 | Source headers select Apache-2.0; retained Android/Open Interface attributions and root Apache text apply. XQ is a configuration of SBC. [Primary Android source](https://android.googlesource.com/platform/system/bt/+/ea7ab70a711e642653dd5922b83aa04a53af9e0e/embdrv/sbc/decoder/include/oi_codec_sbc_private.h). |
| Helix AAC-LC and experimental HE-AAC v1, plus LATM integration parser | 40 | Helix notices refer to RPSL, or a separately obtained applicable RCSL. The applicable complete grant/version and obligation set are **unresolved** in this supplied snapshot; a complete matching RPSL/RCSL text has not been established here. The two LATM parser files retain their separate Apache-2.0 notices. Neither the parser license nor the project license relicenses Helix. |
| freeaptX / aptX HD implementation | 2 | Retained LGPL-2.1-or-later notices govern the library. Full LGPL text is retained under `aptx-family/main/COPYING`. [Primary library source](https://github.com/iamthehorker/libfreeaptx/blob/master/freeaptx.c). |
| Supplied aptX-family license files | 2 | `main/COPYING` is the LGPL text. The separate `LICENSE` public-domain/CC0 statement is retained as supplied; it does **not** override the actual freeaptX file notices. Static-firmware linking/relinking, source-offer and other applicable compliance obligations require separate handling before any binary distribution. No firmware binary is included here. |
| LDAC decoder | 20 | Sony copyright notices are retained. Source-family identification does **not** establish an applicable decoder redistribution grant; that grant remains **unresolved**. The AOSP Apache-licensed LDAC encoder cannot be assumed to license these decoder additions. |
| LHDC V5 decoder | 18 | The owner confirms implementation authorship; their public README selects Apache-2.0 for the implementation. Separately, `lhdc_tables.c` explicitly identifies data extracted from `liblhdcv5.so`. A separate grant for those tables remains **unresolved**; the implementation license is not asserted to establish third-party table rights. |
| libopus decoder subset | 145 | Retained BSD conditions, disclaimers and per-file notices govern these files. `opus/COPYING` is copied byte-for-byte, including its patent-notice links. [Primary Opus license](https://www.opus-codec.org/license/). |
| Supplied ESP-IDF license notice | 1 | Original Apache-2.0 license text retained under `notices/ESP-IDF/LICENSE`. Its presence does not replace any component's own notices. |

The [pinned LHDC owner README](https://github.com/WillyBilly06/LHDC-V5-Decoder/blob/cf857ce1a1429965e161e54adbc3f293fd9ce273/README.md)
contains the implementation license statement; the separate extraction
annotation is in the [pinned table file](https://github.com/WillyBilly06/LHDC-V5-Decoder/blob/cf857ce1a1429965e161e54adbc3f293fd9ce273/decoder/lhdc_tables.c).
The identified LDAC decoder family is
[O2C14/libldac-dec](https://github.com/O2C14/libldac-dec/tree/8c15f53b97c8322c18cff3fddf6d7129dbd3d349).
These links identify evidence, not a newly inferred license grant. The original
sources stay available locally; publication of this development snapshot does
not resolve the uncertainties recorded above.

The historical BTstack host and its local Git history remain excluded. The
installed Arduino core supplies runtime, platform headers and controller
firmware under its own terms; no firmware bytes, application images or
readbacks are included. Codec patent rights, Bluetooth qualification and
trademark permissions are separate from this source inventory. Free public or
noncommercial use by itself does not remove upstream obligations or grant
third-party patents.
