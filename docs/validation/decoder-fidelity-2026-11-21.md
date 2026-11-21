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

## LHDC V5 and Samsung SSC (existing fixture tests, re-run the same day, gcc 15)

- `tools/test_lhdc_aosp.py`: 8 real encoder configurations (44.1-192 kHz,
  16/24-bit, 1000 kbps), decoded through `Codecs.cpp`. All PASS, with gain
  error |x| <= 0.011 dB.
  - Signal/error at 44.1-96 kHz: 41-74 dB.
  - Signal/error at 192 kHz: 25.9-27.9 dB, which is the codec's own error
    at that rate and bitrate.
- `tools/test_ssc.py`: 7 streams made by Samsung's own encoder (44.1k,
  96k UHQ, bitrate switches, one or two frames per packet). Each is decoded
  through `Media.cpp` and `Codecs.cpp`, and the PCM is bit-exact against the
  pinned upstream-decoder digests. Malformed and truncated frames are
  rejected, with no allocation during decoding. PASS.
- On gcc this needs `-Wno-misleading-indentation`, because
  `tests/test_ssc_codec.cpp` trips that warning, which is treated as an
  error. The script targets clang.

## On the board: Cortex-M7 build vs the native build

`tools/fidelity/board/` contains:
- `gen_cases.py`: makes the case files, the native outputs and a generated
  `parity_data.h`.
- `DecoderParity/DecoderParity.ino`
- `read_board.py`

The sketch embeds 17 encoded streams, 645 KB in all:
- the first 64 KB of each of the 7 system-encoder streams above (noise signal);
- all 8 LHDC AOSP fixtures;
- 2 Samsung SSC fixtures.

For the SSC cases, the native output is identical to the pinned upstream
decoder.

The sketch was built with arduino-cli for `arduino:mbed_giga:giga` (core
4.6.0): 1,020,640 B flash, 64,216 B RAM. It was flashed to the physical GIGA
(UID 003C00393533510934353531). It decodes each stream through the same
production `MediaAssembler`/`Decoder` loop on the M7 and streams the int32 PCM
back over USB CDC. Every case reported 0 errors, and every frame count matched
the native build exactly.

| Case | Board vs native x86-64 |
|---|---|
| SBC, SBC-XQ, AAC-LC, aptX, aptX HD | **bit-exact** (byte-identical PCM) |
| LDAC 96k/24 990k | 139.9 / 138.6 dB; max diff 0.9 LSB24 |
| LHDC V5 x/24 (44.1, 48, 96, 192 kHz) | 117.8-137.7 dB; max diff 3.1 LSB24 |
| LHDC V5 x/16 (44.1, 48, 96, 192 kHz) | 99.2-106.3 dB; max diff 1 LSB16, in under 0.11% of samples |
| Samsung SSC UHQ 96k/24 584k, 44.1k bitrate switch | 137.0-141.2 dB; max diff 0.4 LSB24 |
| Opus 48k 256k | 85.0 / 87.0 dB; max diff 1 LSB16, in 2.9% of samples |

The fixed-point decoders produce identical output on the M7. The
floating-point paths (LDAC, LHDC, SSC, Opus) differ only by final rounding.
That comes from the M7 compiler's FPU code generation (e.g. fused
multiply-add), and is far below each codec's own error. Combined with the
reference-decoder results above, the board's decoded PCM is the correct audio
for every codec.

Decode time on the M7 for these streams (integer `micros()`, includes nothing
but `Decoder::decode`):

| Case | Audio decoded | M7 decode time | Load |
|---|---|---|---|
| SBC 48k | 1.43 s | 42 ms | 2.9% |
| AAC-LC 48k | 1.98 s | 68 ms | 3.4% |
| aptX HD 48k | 0.88 s | 140 ms | 15.9% |
| LDAC 96k 990k | 0.52 s | 52 ms | 10.0% |
| Opus 48k | 1.96 s | 120 ms | 6.1% |
| LHDC V5 192k/24 | 0.16 s | 53 ms | 33% |
| SSC UHQ 96k | 0.22 s | 32 ms | 14.8% |

## Not covered

- No system encoder for LHDC V5 or SSC was used; their own fixtures are above.
- HE-AAC was not run.
- The electrical I2S output was not captured. The PCM above is what the
  decoder hands to the I2S path. The packing and DMA after that point are
  covered only by native tests (`tests/test_i2s_packed.cpp`), not measured on
  the pins.
- No listening tests.
