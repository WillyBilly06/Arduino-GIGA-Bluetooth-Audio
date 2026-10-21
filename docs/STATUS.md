# Development evidence and outstanding validation

Status as of 2026-10-21: the project-owned Classic/BLE host is implemented and
has passed finite phone A2DP and GATT exchanges. Stable continuous audio remains
unqualified. No BTstack, Bluedroid or Cordio protocol runtime is selected in the
owned image. The installed Arduino core retains two inert BLE SDK globals;
linked-symbol checks distinguish those from a second host implementation.

## Current target experiment

A readback-verified development probe used the same owned source with an LHDC
profile of 32 compressed packets, a 26-packet startup reserve, 2048-byte maximum
packet size and a 20 ms PCM ring. These experimental settings are not the
library's global defaults. It used LHDC V5 at 192 kHz/24-bit and phone quality
0x8008/32776, the actual nominal 1000 kbps selection.

A 35-second exact-route phone source completed with zero AudioTrack underruns.
During playback, BLE negotiated ATT MTU 517 and completed ten acknowledged
512-byte writes and ten exact notification echoes. The receiver recorded zero
local queue-full drops, decode errors, DMA errors and transport errors, then a
graceful audio stop. This establishes a finite concurrent exchange, not stable
acoustics, RF conformance or every connection/disconnection case.

Audio still failed the continuity target: 362304 prepared silent frames at
192 kHz (1.887 seconds) and fourteen verified discontinuities. Every retained
record had RTP sequence +1, timestamp +27840 rather than +960, and LHDC inner
sequence +29 with exactly one validated encoded frame. The timestamp excess was
392 frame periods, or 1.960 seconds. This locates missing inner packet numbers
before RTP numbering and is consistent with the public sender's 28-buffer queue
flush. It does not prove why this phone discarded them; controller/link/host
backpressure and source scheduling still need separation. A larger buffer cannot
reconstruct discarded encoded content. No listening or physical wire capture
was performed for this probe.

## Corrections and scheduling evidence

An earlier UART adapter drained up to 2048 raw bytes before servicing its owned
actor. A 244-byte BLE transfer, fragmented into 27-byte HCI payloads, stopped the
physical host after five successful echoes. The failure receipt showed four
unprocessed LE slots and no retired fragments. A native actor reproduction
matched that condition. The adapter now services each complete HCI frame before
parsing the next, preserving the byte budget and four-packet receive-window
safety check. The subsequent MTU-517/512-byte phone exchange passed. Native
negative tests still show that blocked TX can exhaust the window; broader stress
and scheduling/credit measurements remain required.

Earlier interrupt-driven TX replaced byte polling. Controller startup improved
from 19.965 seconds to 5.080 seconds in those finite captures. That TX change
alone did not fix audio: its nominal-900-kbps run had 217 local drops and 1.993
seconds of prepared zeros. Separate true-1000-kbps tests found:

| Compressed profile | Local full drops | Prepared silence | RTP timestamp excess |
| --- | ---: | ---: | ---: |
| 8 packets, startup 2 | 87 | 2.017 s | 1.540 s |
| 32 packets, startup 2 | 0 | 1.608 s | 1.540 s |
| 32 packets, startup 26, audio only | 0 | 1.311 s | 1.400 s |

These short runs differ in delivery/teardown and BLE traffic; they do not
establish an optimal startup reserve or a latency guarantee. The queue increase
removed measured local overflow while pre-RTP timestamp omissions persisted.

The codec quality mapping was corrected against Savitech's
[pinned sender constants](https://gitlab.com/savitech-lhdc/savitech-lhdc-codec-aosp-13.0.0_r2/-/raw/3ac44e21e92983bb800eee8cb9d938522e3852d2/src/packages/modules/Bluetooth/system/stack/include/a2dp_vendor_lhdc_constants.h):
0x8007/32775 selects nominal 900 kbps; 0x8008/32776 selects 1000 kbps. Historical
V3/V4/V5 runs using 32775 are not 1000-kbps validations. Original receipts were
preserved and their descriptive labels corrected.

## Source, native and decoder checks

The full selected supplied decoder source set is included, with original
component notices and explicitly recorded licensing uncertainties. Original
project contributions use Apache-2.0; source inclusion is not blanket clearance
of another author's conditions or codec patents.

Portable protocol/profile tests cover bounds, retained input, fragmentation,
output pressure, command/ACL ownership, lifecycle, HCI service order and BLE
MTU/value separation. ASAN and trapping UBSan checks passed on reviewed source.
New maximum-value tests exercise 512-byte writes/notifications across multiple
ACL fragments, exact copies/retries, 513-byte rejection and 247-MTU fallback.
The RTP ledger preserves the first sixteen anomalies, validates bounded framing,
and suppresses expected timestamp arithmetic for unknown/malformed packets.

Per-codec buffer tests exercise actual queue admission, independent packet/access
unit limits, selected-only allocation, startup clamping, runtime PCM geometry,
codec switches, allocation-failure cleanup and concurrent receive/reconfigure.
M7 object checks preserve softfp/fpv5-d16 and internal DMA address restrictions.
Native and compilation checks do not establish controller behavior or audio.

Separate physical Cortex-M7 decoder benchmarks passed independent SBC/SBC-XQ,
AAC-LC, aptX/HD, Opus, LDAC and experimental HE-AAC v1 references. A finite memory
probe passed 220 codec configuration lifecycle checks with zero tracked residual
bytes. LHDC still has no independent encoded-to-PCM reference in this collection.
Earlier SBC/LDAC phone progress used the historical host and cannot qualify
those codecs on the replacement host. RedMagic supplies no Opus sender; Apple
AAC interoperability remains untested.

Raw captures, device identities, firmware/readbacks, media fixtures and phone
packages are excluded from public source. Controller firmware is provided by the
installed Arduino core. The BLE role is a public, unencrypted GATT server for
control/data, with no BLE audio profile or central/client role implemented.

Stable playback, pop suppression, long-duration coexistence, source loss/burst
recovery, pause/resume, clock drift, latency and cross-codec phone interoperability
remain open. Versioned development ZIPs are explicitly marked prereleases;
clean main and stable-release qualification remain separate.
