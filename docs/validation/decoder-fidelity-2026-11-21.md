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

## On the board: I2S at the pins

No external logic analyzer was used.

`tools/fidelity/board/I2sProbe` runs the library's own I2S output
(`i2sBegin`/`i2sWrite`) on the default pins:
- D70/PI1 BCLK
- D72/PI3 DIN
- D74/PB12 LRCK

It writes a frame-numbered 24-bit pattern: left = n ^ 0xA53C96,
right = n * 0x1F3 + 0x3C5A, each shifted into the top 24 bits.

The pins are then sampled through the GPIO input data registers. In
alternate-function mode these registers follow the actual pad level, so this
reads the pins, not the I2S peripheral. Each capture is a ~0.9 ms window with
interrupts masked, shorter than one DMA half, so every captured frame is
fresh. The probe refills for 60 ms before each capture. The probe's own
`underruns` counter rises only because its USB transfers between captures
outlast the 20 ms ring, not during a capture.

`read_i2s.py` decodes the captures on the host:
- BCLK rising edges, then DIN bits;
- frame alignment from LRCK, or from the pattern in GPIOI-only mode;
- each decoded frame checked against the pattern;
- the low byte checked to be zero.

Eight captures per rate and mode, physical GIGA, 2026-11-21:

| Rate | Sampling | BCLK measured | LRCK measured | Slot | Frames matching the written samples |
|---|---|---|---|---|---|
| 44.1 kHz | GPIOI+GPIOB 13.3 MHz | 2.8222 MHz = 64.00 fs | 44.10 kHz | 32 bit | 312/312, consecutive, low byte 0 |
| 44.1 kHz | GPIOI only 21.6 MHz | 64.00 fs, 0 missed edges | - | - | 313/313 |
| 48 kHz | GPIOI+GPIOB 13.3 MHz | 3.0720 MHz = 64.00 fs | 48.00 kHz | 32 bit | 337/337, consecutive, low byte 0 |
| 48 kHz | GPIOI only 21.6 MHz | 64.00 fs, 0 missed edges | - | - | 340/340 |
| 96 kHz | GPIOI only 21.6 MHz | 6.1435 MHz = 64.00 fs, 0 missed edges | 96.00 kHz (13.3 MHz pass) | - | 684/684 |
| 192 kHz | any | not resolvable | 192.0 kHz | - | not decodable |

At 44.1, 48 and 96 kHz, the pins carry Philips I2S with 32-bit slots and the
exact 24-bit samples written, MSB first, in order. Neither data nor framing
has errors.

At 192 kHz, BCLK is 12.288 MHz. Reading the GPIO register tops out at about
21.7 MHz, both from a CPU loop and from an MDMA block copy (also tried; at
96 kHz it gives the same results, less cleanly). That is below the 24.6 MHz
needed to resolve BCLK in real time.

### Equivalent-time capture (all rates including 192 kHz)

`tools/fidelity/board/I2sFold` writes one constant frame repeatedly
(left 0xA53C96, right 0x5A3C69, top 24 bits), so the pad waveform repeats
every frame. It then takes 0.9 ms GPIOI+GPIOB captures at 13.3 MHz with
interrupts masked.

The CPU clock (PLL1) and the I2S clock (PLL2) are unrelated, so the samples
fall at effectively random phases within the frame. `fold_local.py` fits
the frame phase locally from LRCK falling edges (least squares over ±20
edges, since the polling loop is not perfectly uniform). It folds every
sample into a 2048-bin frame and then:
- finds the BCLK harmonic and phase;
- reads DIN just after each BCLK rising edge.

| Rate | Captures | Frames folded per capture | Strongest BCLK harmonic | DIN contrast | 64-bit frame exact |
|---|---|---|---|---|---|
| 48 kHz | 8 | 42-43 | 64 per frame (3.072 MHz) | 0.94-1.00 | 8/8 |
| 96 kHz | 8 | 86 | 64 per frame (6.144 MHz) | 0.95-0.97 | 8/8 |
| 192 kHz | 8 | 173 | 64 per frame (12.288 MHz) | 0.96-0.97 | 8/8 |

The method was validated where the direct decode above is ground truth
(48 and 96 kHz). At 192 kHz, the pins carry BCLK at 64 fs and LRCK at
192.0 kHz (measured 191,990-191,993 Hz from the crystal-referenced CPU
clock). DIN carries the exact 64-bit Philips frame, at the same bit offset
relative to LRCK as at 48 and 96 kHz.

The fit window matters at 192 kHz. With ±4 edges the fold smears (0/8
exact); with ±8 edges, 7/8 are exact. With ±12 or ±20 edges all 8 are
exact, so the earlier misses were a limit of the method, not of the signal.

This shows the constant-frame waveform, i.e. clocking, framing and bit
order at 192 kHz. A changing sample stream at 192 kHz was only decoded
directly at 44.1-96 kHz, but it uses the same packing and DMA code.

### Alternate pin routes (48 kHz, equivalent-time)

`tools/fidelity/board/I2sRoutes` + `read_routes.py`: the same constant-frame
fold, run for each route through `ReceiverConfig.i2s`-style `I2sPins`. The
GPIO input registers of all of a route's ports are sampled (4.2-7.7 MHz with
three or four ports), in windows capped below one DMA half. Six captures per
route:

| Route (BCLK / DIN / LRCK / MCLK) | I2S block | `begin()` | Result at the pins |
|---|---|---|---|
| D75/PD3, D73/PC1, D74/PB12 | SPI2 | Ok | BCLK 64 fs, exact 64-bit frame 6/6 |
| D75, D73, D71/PI2 | - | InvalidArgument | correctly refused: PI2 is I2S2 data input only |
| D70/PI1, D72/PI3, D69/PI0, D68/PC6 | SPI2 | Ok | exact frame 6/6; MCLK strongest harmonic 256 per frame (12.287 MHz) in 5/6 |
| D85/PA5, D5/PA7, D84/PA4 | SPI1 | Ok | exact frame 6/6 |
| D91/PB3, D47/PB2, D84/PA4 | SPI3 | Ok after fix (was Busy) | exact frame 6/6 |
| D75, D73, D7/PB4 | SPI2 | Ok after fix (was Busy) | exact frame 6/6 |

The probe found a bug. On a stock board, `GPIOB->MODER` reads 0xFFFFFEBF,
the STM32H7 reset value, which leaves PB3 and PB4 in JTAG alternate
function 0. `i2sBegin` refused any pin in alternate-function mode as owned
by another user, so every documented route through D91 or D7 failed with
`Busy`. Fixed in `src/platform/I2sPins.h` (`I2sJtagResetPinsB`): AF0 on
PB3/PB4 now counts as free, and any other AF or output mode is still
refused. The fix has native tests in `tests/test_i2s_pins.cpp`, and both
routes above were re-verified on the board.

The one MCLK capture that missed (peak at harmonic 63) is a fold limit. MCLK
runs at 12.3 MHz against 5.1 MHz sampling, so it needs the finest phase fit.

### Route-table sweep at 44.1, 48 and 96 kHz

A second run (after the PB3/PB4 fix) used 12 routes. Together they exercise
every pad in `I2sRoutes` in each of its roles, with one exception:
- SPI1 BCLK D85/D91/D62; DIN D5/D93/D90; LRCK D84/D44; MCLK D76.
- SPI2 BCLK D1/D94/D75/D70; DIN D73/D80/D72; LRCK D7/D9/D74/D69;
  MCLK D68.
- SPI3 BCLK D91; DIN D47/D19; LRCK D84; MCLK D15.
- Not covered: SPI3 DIN on D93. The route planner picks SPI1 first for
  D91/D93/D84, so that role was not reached.

There were 4 captures per route and rate, 144 in total. `begin()` returned
Ok for every route except D75/D73/D71, which it refused at all three rates
as intended.

DIN was read against the LRCK frame phase (`din_only.py`). Every route,
at every rate, decoded the exact 64-bit frame in at least 3 of 4 captures;
120 of the 132 captures were exact in this way. Every miss was 60-63/64
and came from the routes with the slowest sampling (3.9-5.5 MHz,
three-port routes at 96 kHz). The folded BCLK peaked at 64 per frame in
every capture except four from SPI3 at 96 kHz sampled at 3.9 MHz. MCLK
peaked at 256 fs on D68, D76 and D15 at all three rates whenever the fold
was sharp; that is 11.29, 12.29 and 24.57 MHz. No route showed a
systematic error.

## Not covered

- No system encoder for LHDC V5 or SSC was used; their own fixtures are above.
- HE-AAC was not run.
- I2S at 192 kHz was verified by equivalent-time sampling of a constant frame,
  not by a real-time capture of changing samples.
- Alternate routes were probed at 44.1/48/96 kHz, not at 192 kHz (only the
  default route was folded at 192 kHz). SPI3 DIN on D93 was not reached.
- Analog electrical quality at the header was not measured: edge rates,
  levels under a DAC load, jitter. The probe sees logic levels at the pad
  input buffer only.
- No listening tests.
