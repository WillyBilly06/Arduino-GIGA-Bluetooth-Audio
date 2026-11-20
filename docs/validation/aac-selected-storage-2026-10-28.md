# Local AAC selected-storage successor, 2026-10-28

This uncommitted successor of dev.17 sizes nine AAC element-state planes to the
selected one- or two-channel decoder. Program records and those aligned planes
share the existing base allocation; coefficient, overlap and SBR storage retain
their selected capacities. Allocation-block counts are unchanged. SBR setup now
publishes a complete state only after every allocation succeeds, frees partial
state on failure, and permits a clean retry. Channel guards run before missing
element planes or an output channel beyond the configured PCM format are used.
An error after an earlier valid element can leave that element's PCM written;
the decoder does not promise output-atomic errors.

The change preserves the decoder arithmetic, tables, full per-element syntax
capacities, raw/generic program-count choices, wrapper formats and all codec
defaults. Original upstream notices and repository LF endings are retained.
The vendor manifest identifies the current bytes; historical source-provenance
and earlier release receipts retain their original scopes. Component licensing
limits remain in [THIRD_PARTY.md](../../THIRD_PARTY.md).

## Executed host checks

`python tools/test_aac_allocations.py --asan --ubsan` compiled the 29 AAC C
units and passed 14 native cases on Windows x64. It exercises actual AAC code
and tracked allocations, with no board/radio/phone or external media files:

- Selected mono/stereo allocation sizes, all ten aligned/nonoverlapping spans,
  their complete capacities and program-record boundaries.
- Twenty-two SBR allocation-failure/retry positions, repeated enable, flush,
  release and zero residual tracked allocations.
- Generic sixteen-program parsing/storage, mono channel-entry rejection, output
  channel bounds, and valid one-SCE/two-SCE synthetic controls with seeded history.

AddressSanitizer was enabled. UndefinedBehaviorSanitizer used the retained
fixed-point exclusions for shifts and signed integer overflow; this is scoped
UBSan coverage. The real AAC frontend separately passed all 11 retained-prefix,
fragmentation, recovery and ownership cases under AddressSanitizer with fake
radio/I2S boundaries. Test-runner plan checks verify that planning launches no
compiler, executable or child suite.

The frozen semantically identical candidate separately passed 16 Android ARM64
CPU cases. Its paired test covered 66 allocation configurations and eight finite
LC/HE-v1 PCM streams with unchanged digests versus its same-target baseline.
Those digests establish equality on those inputs, not independent conformance.
Private HE media are not added to this source tree.

## Cortex-M7 build and layout

The final source passed the official Arduino CLI Receiver build for
`arduino:mbed_giga:giga:target_core=cm7`, stock Mbed GIGA 4.6.0, followed by a
static image audit: 191 library translation units, all seven build switches
enabled (eight codec identities), six linked operation tables, project-owned
host, UART 2000000, WS74 and the unchanged 35182-byte/139-command stock HCD.

Application BIN: 542100 bytes; SHA-256
`da887e8d3a02860f2f17b4e21299dfea735675662d00bd5e0edc65231f0bdb71`.
It fits the separately checked 786432-byte application limit. The linker ELF,
load segments, vectors, soft-float calling convention and stack declarations
were checked. This image has not been flashed or executed.

The official M7 compiler also emitted the following ABI/layout constants from
the old and final headers and their allocation formulas. These are requested
LC decoder payloads before wrapper buffers, allocator overhead or SBR:

| API/configuration | Before | Selected | Difference |
|---|---:|---:|---:|
| Raw, mono | 11254 | 10388 | 866 bytes less |
| Raw, stereo | 19446 | 19484 | 38 bytes more |
| Generic 16-program, mono | 12484 | 11620 | 864 bytes less |
| Generic 16-program, stereo | 20676 | 20712 | 36 bytes more |

The pointer/alignment overhead increases stereo payload slightly. Do not claim
universal RAM reduction. These constants are compiler evidence, not executing
M7 heap measurements. The native x64 raw mono/stereo payloads are 10484/19580;
the earlier same-target pair measured 822 fewer mono bytes and 82 more stereo
bytes. Architecture and program-record alignment explain the different totals.

## Remaining qualification

Real common-window/correlated-PNS and coupled-SBR streams with an independent
oracle, broader input coverage, final-image M7 decoder execution, sender
negotiation, concurrent radio/I2S timing, electrical output and acoustics remain
separate. Direct mixed internal `InitSBRChannels`/`FreeSBR` sequences are not a
supported public lifecycle. HE-AAC v1 remains experimental; HE-AAC v2/PS and
xHE-AAC remain unsupported. This AAC change is not a demonstrated fix for the
rare LHDC delivered-input shortages or clock-timing overruns.
