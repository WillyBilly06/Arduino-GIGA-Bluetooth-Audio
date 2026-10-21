# Development evidence and outstanding validation

Snapshot status as of 2026-10-21: owned dual-mode host integration is work in
progress. The initial LE command `0x200a`, status `0x0c` (Command Disallowed)
failure was corrected by using the verified reset state to omit a redundant
initial advertising-disable command. The
OwnedReceiverV3 local development image was flashed and readback-verified;
its application SHA-256 is
`85e79cd22ac8f0e79c111d5258235f2205b297e22d7ead7202703befb65ed4dd`.
The finite `20261021-131509` target capture showed controller ready, all BLE
setup/enable completions successful, and zero transport errors. A real phone
GATT probe then passed negotiated MTU 247, ten successful write callbacks,
ten exact notification echoes and client close. Client close records the
Android API call, not independent verification of every disconnect layer.

The phone bonded and connected A2DP. The full local image's codec readback
was LHDC 192 kHz/24-bit with quality 0x8007, nominal 900 kbps. Earlier descriptions
incorrectly called this 1000 kbps. The subsequent exact-route trial used a
90-second source and showed receive/decode/DMA progress with queue drops and
underruns. The actor stopped at 204972 ms since boot on receive-window
accounting. A second GATT probe was attempted after the actor had stopped and
timed out. Thus the initial standalone GATT pass does not establish concurrent
operation, and the attempted second probe did not test a healthy streaming
actor. Continuous stable audio and Classic/BLE concurrency remain unqualified.

The current candidate replaces polled UART transmission with a bounded
interrupt-driven mailbox and retains failure diagnostics and Host Number Of
Completed Packets (HNC) counters. Local image V5 SHA-256
`b800c8237f7f21459cbfd54412b8c49749f204e5804346e90e71c7f76f4bb76a`
was flashed/readback-verified. Controller startup took 5.080s versus 19.965s in
the earlier polling candidate. A 25-second exact-route source trial completed
BLE MTU 247, 10 successful writes and 10 exact echoes while LHDC audio was active,
with zero transport/decode/DMA errors and graceful stop. It still had 217 local
queue drops and 1.993s of prepared zero frames. This finite result does not
establish stable playback or the absence of future credit-window failures.

The codec quality mapping was independently corrected against Savitech's
[pinned sender constants](https://gitlab.com/savitech-lhdc/savitech-lhdc-codec-aosp-13.0.0_r2/-/raw/3ac44e21e92983bb800eee8cb9d938522e3852d2/src/packages/modules/Bluetooth/system/stack/include/a2dp_vendor_lhdc_constants.h):
0x8007/32775 selects nominal 900 kbps, and0x8008/32776 selects 1000 kbps. The V3/V4/V5
trials described above used900 kbps. Testing at the requested 1000 kbps setting is
separate work; none of those earlier runs is labeled a 1000 kbps validation.

The full supplied decoder source set is included, with original
notices and explicit licensing uncertainties. Source inclusion and the earlier
local image's finite progress do not qualify continuous streaming for this
candidate. Raw captures, device identities and application binaries are excluded.

Portable owned protocol/profile tests exercise packet bounds, fragmentation,
output backpressure, actor scheduling, connection cleanup and BLE GATT state.
ASAN and trapping UBSan runs passed for reviewed snapshots. These tests use
synthetic protocol events and do not establish controller behavior or security
interoperability. The owned BLE role is GATT server only.

Separate Arduino GIGA Cortex-M7 decoder benchmarks passed SBC/Opus and other
supplied codecs against complete frame sequences, with allocation cleanup
and frame canaries checked. The finite memory probe passed 220 configuration
open/close cases with zero residual tracked bytes. These are decoder/memory
results, not owned-host audio results or redistribution-license clearance.

Earlier Classic SBC and LDAC receive/decode/I2S progress was captured using the
historical third-party host. That host and its local history are excluded.
Those captures cannot qualify this replacement host. Concurrent Classic audio
and BLE, continuous streaming in this candidate, real PCM output, pause/resume behavior, synchronization,
and end-to-end latency still require broader target validation for the owned runtime.
Versioned development ZIPs are explicitly marked prereleases; stable release
qualification remains separate.
