# Configuration-sized SBC and compact Opus storage

SBC uses the negotiated block/subband/channel geometry for decoder work and
frontend PCM storage. A configuration without selected SBC codec information
retains the legacy maximum geometry. Legal bitpool changes remain supported;
a different selected geometry requires reopening the decoder. The bounded
reset entry point preserves the existing unbounded public reset API.

For `N = blocks * subbands` and `C = channels`, the Cortex-M7 decoder's tracked
payload is `140 + C * (432 + 6*N)` bytes, compared with `140 + 1200*C` before
this change. The synthesis history remains 432 bytes per channel, including
four-subband operation: its stride and ring window still require that space.
The frontend allocates `8*N` bytes for stereo 32-bit output, including mono
input expansion. Its maximum remains 1,024 bytes.

Opus retains the complete SILK, Hybrid and CELT decoder. Its CNG sampler can
address at most 256 history entries; storage and history updates now preserve
exactly that reachable prefix instead of 320 entries. The wrapper retains
only the rate/channel configuration it needs. This does not remove SILK state
for short CELT-only configurations or make every internal codec table lazy.

## Measured memory and regression checks

Paired firmware probes on the GIGA tested all 256 SBC selections and all 60
Opus rate/channel/duration selections. Every case opened with three live
payload allocations and closed with zero residual allocations. Injected
allocation failure also left no residual allocation.

| Measurement | Reduction |
|---|---:|
| Opus decoder, each of 60 target configurations | 548 payload bytes |
| SBC decoder, four blocks/four subbands/stereo | 1,344 payload bytes |
| SBC decoder, maximum 128-sample geometry | 0 bytes |
| SBC frontend, four blocks/four subbands | 896 bytes in native allocation test |

Target numbers count requested allocation payloads, excluding allocator
headers/alignment and static memory. The frontend measurement is separate
from the physical decoder-only probe. The native combined decoder/frontend
reduction reaches 2,240 bytes; this is not a measured total-SRAM reduction.

The portable sanitizer suite exercises 256 SBC selections, malformed
configurations, bitpool changes, bounded resets, allocation-failure budgets,
legacy no-info decoding and partial mono-to-stereo output. The four-subband
partial decoder also corrects its raw synthesis stride. Opus checks include
all 60 configurations, actual SILK/Hybrid/CELT/DTX transitions, PLC/FEC calls,
output/error state, aliasing, caller-owned config mutation and no decode-time
allocation. Its 12,288-case CNG digest matches the preserved original history.
The small owned Opus packet corpus requires no external encoder at test time.
SBC 32 and Opus 300 allocation-budget attempts include successful opens; they
are not counts of actual allocation failures.

Run `python tools/test_codec_memory.py --sanitize --asan` with clang and
clang++ on PATH. The regular native codec and public owned-host test runners
also invoke these tests. Fixed-point legacy shift and signed-overflow UBSan
checks are excluded; ASan and the other trapping UBSan checks remain enabled.

Independent FFmpeg decoding of finite owned SBC signals agreed within three
S16 LSBs for three distinct four-subband geometries and four LSBs for the
tested eight-subband cases, without fitting gain or sample alignment. These
are finite reference comparisons, not normative SBC conformance coverage.
The full codec/HE-AAC reference suite and owned-host checks also passed.

## Receiver validation and limits

The M7 receiver built with all codec defaults enabled, the project-owned host
and zero forbidden BTstack host symbols. Its private image was flashed and
fully read back. This is distinct from any transformed public-source binary.
The controller firmware and I2S routing were unchanged.

A subsequent 95-second LHDC V5/192 kHz capture retained streaming while all
30 exact 512-byte GATT echoes passed at MTU 517. The observed BLE parameters
were 120 ms, latency 2 and a five-second timeout. Four timestamp and inner
sequence jumps remained, with 97,152 missing PCM frames in the matched
diagnostic bracket and no new local queue, decoder, DMA or transport errors.
These memory changes do not fix the remaining audio gaps. No new acoustic,
I2S clock, throughput or Opus-phone interoperability qualification is claimed.

The [validation receipt](codec-memory-geometry.json) identifies the build and
bounded proof artifacts. Original decoder source identities are retained in
the import manifest; exactly eight imported vendor files change. All codec
choices remain enabled, with original third-party notices retained.
