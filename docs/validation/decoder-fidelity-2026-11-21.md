# Decoded-audio fidelity against reference decoders and the source (2026-11-21)

The over-the-air codec matrix ([codec-matrix-2026-11-20.md](codec-matrix-2026-11-20.md))
only shows that each decoder accepted every frame. This run checks that the
decoded PCM is the right audio.

## Method

`tools/fidelity/` (`sh tools/fidelity/run.sh`, Linux, g++/gcc):

1. `enc.cpp` generates 10 s of known stereo audio. Each channel gets its own
   tone set (220/1000/5300/12100 Hz left, 330/1800/7700/15500 Hz right) plus
   low-passed noise and slow level movement, peaking around -6 dBFS. A second
   pass (`TONES=1`) drops the noise.
2. It encodes this with the same system libraries PipeWire uses on the Linux
   sender: libsbc, libfreeaptx 0.2.2, libldacBT_enc, libopus and libfdk-aac
   (LATM MCP1). It packs the result into A2DP media packets: RTP plus the
   codec's media header, or raw for aptX.
3. Where the system has an independent decoder, it also decodes with it
   (the "reference").
4. `gbdec.cpp` sends the packets through the library's production
   `MediaAssembler` and `Decoder`. These are the unmodified
   `src/codecs/Codecs.cpp`, `src/detail/*.cpp` and vendored decoder sources,
   with one codec enabled per binary. It follows the receiver's decode loop,
   including the receiver-sized 1024-frame PCM scratch.
5. `cmp.cpp` aligns the two outputs by cross-correlation and reports, per
   channel: signal-to-error ratio, gain error, maximum difference (24-bit
   LSB) and the bit-exact fraction. The tones-only pass is periodic, so it
   uses the lag measured on the noise pass.

Every run decoded with 0 errors, 0 resyncs and 0 leaked bytes.

## Results: library decoder vs independent reference decoder

| Codec (encoder settings) | Reference decoder | Signal/error L / R | Gain error | Max diff | Bit-exact |
|---|---|---|---|---|---|
| SBC 48k, joint stereo, bitpool 53 | libsbc | 68.0 / 68.1 dB | -0.002 dB | 5 LSB16 | 29% |
| SBC-XQ 48k, dual channel, bitpool 47 | libsbc | 67.9 / 68.0 dB | -0.002 dB | 5 LSB16 | 29% |
| AAC-LC 48k, 256 kbps, LATM | FDK-AAC | 80.6 / 80.3 dB | 0.000 dB | 1 LSB16 | 90% |
| aptX 48k | libfreeaptx | identical | 0 | 0 | 100% |
| aptX HD 48k/24 | libfreeaptx | identical | 0 | 0 | 100% |
| Opus 48k, 256 kbps, 20 ms | system libopus (float) | 69.5 / 68.7 dB | 0.000 dB | 8 LSB16 | 38% |

SBC, AAC and Opus differ only at the 16-bit rounding level, which is expected
between independent implementations (fixed vs floating point). For aptX and
aptX HD, the receiver's per-block `aptx_decode_sync` path produces exactly the
same output as libfreeaptx's own decoder.

## Results: library decoder vs the original source

| Codec | With noise, L / R | Tones only, L / R | Gain error |
|---|---|---|---|
| SBC bitpool 53 | 22.9 / 22.2 dB | 31.3 / 30.5 dB | < 0.012 dB |
| SBC-XQ bitpool 47 | 37.4 / 36.7 dB | 54.8 / 50.6 dB | < 0.004 dB |
| AAC-LC 256k | 24.6 / 24.3 dB | 46.2 / 47.6 dB | < 0.05 dB |
| aptX | 28.1 / 27.4 dB | 41.7 / 38.7 dB | < 0.002 dB |
| aptX HD | 38.9 / 38.4 dB | 55.1 / 52.6 dB | < 0.001 dB |
| LDAC 96k/24, 990k (HQ) | 27.5 / 27.5 dB | 77.4 / 73.4 dB | < 0.003 dB |
| LDAC 96k/24, 660k (SQ) | 22.5 / 22.5 dB | 67.0 / 58.6 dB | < 0.035 dB |
| Opus 256k | 22.9 / 22.7 dB | 27.2 / 26.9 dB | < 0.03 dB |

These numbers are the lossy codec's own error on a deliberately hard signal
(full-band noise and tones up to 15.5 kHz). Where a reference decoder exists,
it gives the same figures, so the decoder adds nothing measurable.

LDAC has no independent decoder on the system. Its 77/73 dB tones-only result
at 990k, unity gain and the correct per-channel content (a left/right swap
would read about 0 dB) show it decodes correctly. In every row, left and right
match their own source channel, so channel order is correct.

Lag values (frames of codec delay): SBC 73, AAC 2048, aptX/aptX HD 0,
LDAC 256, Opus 312.

## Not covered

- LHDC V5 and Samsung SSC are covered by their own fixture tests
  (`tools/test_lhdc_aosp.py` round trips, `tools/test_ssc.py` bit-exact). No
  system encoder for them was used here.
- HE-AAC was not run.
- This is a host-native build (x86-64, gcc). The Cortex-M7 build compiles the
  same sources, but its numeric parity with this build was not compared.
- Decoded PCM on the board and the electrical I2S output were not captured.
  That needs capture hardware on the I2S pins.
- No listening tests.
