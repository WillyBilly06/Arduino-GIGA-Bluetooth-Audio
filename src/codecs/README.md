# Decoder source layout

`Codecs.cpp` connects each decoder to the library's common configuration,
allocation and PCM interface. The decoder implementations are under
`../vendor/codecs/`:

| Codec | Implementation directory |
| --- | --- |
| SBC and SBC-XQ | `../vendor/codecs/sbc/` (XQ uses SBC with a different configuration) |
| AAC-LC and experimental HE-AAC v1 | `../vendor/codecs/aac/` |
| aptX and aptX HD | `../vendor/codecs/aptx/` |
| LDAC | `../vendor/codecs/ldac/` |
| LHDC V5 | `../vendor/codecs/lhdc/` |
| Opus | `../vendor/codecs/opus/` |

The full source profile uses `CodecBuildConfig.h` to select the separately
compiled decoders. An implementation being present does not prove phone
negotiation or continuous playback. Original decoder notices and conditions
remain separate from the project's original-code license.
