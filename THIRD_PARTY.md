# Source licenses and held components

Project-owned library, host integration and portable protocol kernels are
provided under the root Apache License 2.0 and NOTICE. Original third-party
copyright notices and source bytes are retained. Modified imported files carry
an additional dated port notice; per-file input/port hashes are recorded in
`docs/source-provenance.json`.

| Component | Included status | License/evidence |
|---|---|---|
| Android/OI SBC decoder | Included | Apache-2.0 headers retained in each original source; root LICENSE supplies terms. [Primary Android source](https://android.googlesource.com/platform/system/bt/+/ea7ab70a711e642653dd5922b83aa04a53af9e0e/embdrv/sbc/decoder/include/oi_codec_sbc_private.h). |
| libopus decoder subset | Included | Original BSD conditions/notices retained; `src/vendor/codecs/opus/COPYING` copied byte-for-byte. [Primary Opus license](https://www.opus-codec.org/license/). |
| Helix AAC/HE-AAC | Held, disabled | RPSL/RCSL grant and redistribution conditions require separate resolution. Project Apache licensing does not relicense these sources. |
| freeaptX/aptX HD | Held, disabled | LGPL source-compliance plan is not part of this initial export. |
| LDAC decoder | Held, disabled | An encoder license does not establish a redistribution grant for the supplied decoder. |
| LHDC V5 decoder | Held, disabled | Owner confirms implementation authorship and public README selects Apache-2.0. However `lhdc_tables.c` explicitly describes data extracted from `liblhdcv5.so`; no separate table redistribution grant is established. The complete codec is held to keep its required table dependency out of this snapshot. |
| Historical BTstack host | Excluded | Its separate terms do not become Apache-2.0 through this project's license. No historical host source or history is exported. |

The LHDC ownership statement is preserved locally. Its public license evidence
is the [pinned owner README](https://github.com/WillyBilly06/LHDC-V5-Decoder/blob/cf857ce1a1429965e161e54adbc3f293fd9ce273/README.md)
and the separate table-source annotation appears in the
[pinned table file](https://github.com/WillyBilly06/LHDC-V5-Decoder/blob/cf857ce1a1429965e161e54adbc3f293fd9ce273/decoder/lhdc_tables.c).
This hold does not delete or alter local development sources, and does not
assert that reverse engineering alone establishes or removes legal rights.

The installed Arduino core supplies platform headers, runtime and controller
firmware under its separate terms. No controller firmware bytes are copied.
Codec patents, Bluetooth qualification and trademark permissions are separate
questions; the project license does not grant third-party codec patents.
