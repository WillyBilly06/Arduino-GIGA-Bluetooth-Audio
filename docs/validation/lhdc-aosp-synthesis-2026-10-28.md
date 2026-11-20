# LHDC V5 synthesis correction from the AOSP encoder

The local library now uses the AOSP analysis window and an algebraic output
scale instead of fitted windows and empirical volume trims. This corrects
measured reconstruction errors in the supplied decoder. The corrected decoder
subsequently passed an [actual M7 CPU test](lhdc-aosp-m7-2026-10-28.md).
Full Receiver streaming and physical audio validation remain pending.

Reference: [Android Bluetooth LHDC V5 encoder](https://android.googlesource.com/platform/packages/modules/Bluetooth/+/c77db469de80f86660bc053bec4dee0c5d4b947c/system/audio/codecs/lhdcv5/),
commit `c77db469de80f86660bc053bec4dee0c5d4b947c`, tag
`android-17.0.0_r1`. All 28 fetched Git blobs were checked against Gitiles
object IDs and SHA-256. The complete unmodified Rust encoder built and ran on
the Windows x86_64 host. Cargo.lock and source provenance accompany the tests.

## Findings and correction

For 256 actual AOSP-encoded stereo frames spanning 44.1/48/96/192 kHz and
16/24 bits, the decoder recovered all 245760 quantized coefficients, SNS
scale factors and gain indices exactly. Instrumented encoder and decoder
outputs matched their uninstrumented outputs. This localizes the measured
reconstruction errors downstream of those bitstream stages for these cases.

The old windows used fitted/resampled ramps of 88/176/352 samples. The encoder
uses 96/192/384 samples, with zero padding 72/144/288. Power complementarity of
the old windows did not establish compatibility with the analysis window.
The new constant tables come from the unmodified encoder's Q31 ramp routine,
rounded once to float. All 3360 production float values match that reference.
The private shape check now derives ramp length from folding geometry so
rounded unity endpoints retain the fast overlap path. Fast and general overlap
give exactly equal numeric float values; the fast zero fill may change the
sign of zero, which has no effect on emitted integer PCM.

The old output scale contained empirical per-rate/per-depth dB trims and a
`N/480` factor. A direct double-precision inverse transform of the encoder's
unquantized coefficients establishes the correct combined scale as
`N * 2^(output_container_bits - 32)`. The correction applies this scale both
to emitted PCM and to the existing channel sanity check. No entropy, SNS,
quantizer, FFT arithmetic or global compiler floating-point policy changed.

## Synthetic round-trip results

Every case uses 32 stereo frames, reported encoder bitrate 1000 kbps, distinct
tones and deterministic broadband noise. Measurements use the known sample
delay and omit four startup frames. Channel ranges below include both channels.

| Rate / source depth | Old gain error, dB | Corrected gain error, dB | Corrected signal/error, dB |
|---|---:|---:|---:|
| 44.1 kHz / 16 | -5.042 to -5.041 | -0.0012 to -0.0010 | 71.85 to 73.83 |
| 44.1 kHz / 24 | -0.7593 to -0.7592 | +0.00009 to +0.00020 | 43.34 to 45.48 |
| 48 kHz / 16 | -5.042 to -5.041 | -0.0014 to -0.0011 | 43.07 to 45.16 |
| 48 kHz / 24 | +0.3504 to +0.3507 | -0.00013 to +0.00013 | 43.87 to 46.96 |
| 96 kHz / 16 | -5.043 to -5.041 | -0.0029 to -0.0010 | 41.43 to 42.82 |
| 96 kHz / 24 | -0.7303 to -0.7294 | -0.00090 to +0.00007 | 41.27 to 42.95 |
| 192 kHz / 16 | -5.050 to -5.036 | -0.0101 to +0.0046 | 26.17 to 27.66 |
| 192 kHz / 24 | +1.3494 to +1.3502 | -0.0012 to -0.0004 | 25.88 to 27.89 |

The unquantized, independent inverse-transform check exceeds 128 dB across
these cases with the matching windows. That isolates window/normalization
behavior; it is not the fidelity of the lossy codec. The actual encoded
round trips are the separate measurements in the table.

`tools/test_lhdc_aosp.py` runs without Rust or NumPy, verifies fixture hashes,
compares the windows and fast/general overlap, and exercises the real decoder
and adapter with output guards and zero residual tracked allocations. Its fixed
regression limits are absolute gain error <=0.1 dB and signal/error >=20 dB.
AddressSanitizer and scoped UndefinedBehaviorSanitizer pass; the latter excludes
legacy shifts and signed overflow as explicitly recorded in its receipt.

The final full native AddressSanitizer run passes 91 primary executions and
eight child suites. The official Arduino Mbed GIGA 4.6.0 Cortex-M7 Receiver
build passes with all seven codec defaults enabled: 191 library translation
units, 223 compile rows and stock HCD. All 424 production files match the
audited build snapshot. The 541908-byte BIN has SHA-256
`758bba5adbf955a82bb5c832cd75e10a3d67b1737c9d5462d45c4808353a9ebf`.
This is a build/static-audit result, not target execution. Machine-readable
evidence and local receipt paths are in the adjacent JSON report.

## Limits and separate unresolved issues

These results cover these synthetic stereo stimuli and the listed lossy
configurations. They do not establish all bitrates, lossless modes, music
quality, phone interoperability, acoustic fidelity or latency.

The Cortex-M7 versus ARM64 bitwise output mismatch remains open. The previous
controlled experiment disabled compiler contraction on both targets and still
produced different LHDC CRCs; blanket contraction flags were not adopted.
The AOSP encoder uses a custom power approximation and Q30 quantization while
the decoder still reconstructs with `exp2f`. The gain-index exponent matched
all 16384 probed configurations/indices, but that does not establish an exact
inverse quantizer or a complete cause of cross-target differences.

The imported decoder's active GIGA path is scalar C (`LHDC_HOST_BUILD=1`),
excluding its ESP32/Xtensa assembly and dual-core path. That does not rule out
every portability issue; it rules out active Xtensa instructions as the cause
of these particular measured errors.

The correction ran in the bounded CPU-only M7 diagnostic and was then removed
by exact restoration. The full Receiver application remains undeployed; the
field board remains on its earlier image, SHA-256
`ad1d2f5e9ff2743fcab45582027f31aa5180cee74abb9c13d5d09b7fd8c1242f`.
The BLE soak, clock overruns and occasional PCM shortages belong to separate
field evidence. No experiment here establishes their cause or fixes them.
