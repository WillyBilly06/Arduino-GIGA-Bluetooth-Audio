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
was LHDC 192 kHz / 24-bit / 1000 kbps. The subsequent exact-route trial used a
90-second source and showed receive/decode/DMA progress with queue drops and
underruns. The actor stopped at 204972 ms since boot on receive-window
accounting. A second GATT probe was attempted after the actor had stopped and
timed out. Thus the initial standalone GATT pass does not establish concurrent
operation, and the attempted second probe did not test a healthy streaming
actor. Continuous stable audio and Classic/BLE concurrency remain unqualified.

The exported source adds failure diagnostics and Host Number Of Completed
Packets (HNC) counters; it does not contain a receive-window policy fix. LHDC source is held from the
public profile, so the local image's progress cannot qualify public SBC/Opus
streaming. Raw captures, device identities and application binaries are excluded
from this snapshot.

Portable owned protocol/profile tests exercise packet bounds, fragmentation,
output backpressure, actor scheduling, connection cleanup and BLE GATT state.
ASAN and trapping UBSan runs passed for reviewed snapshots. These tests use
synthetic protocol events and do not establish controller behavior or security
interoperability. The owned BLE role is GATT server only.

Separate Arduino GIGA Cortex-M7 decoder benchmarks passed SBC/Opus and other
locally held codecs against complete frame sequences, with allocation cleanup
and frame canaries checked. The finite memory probe passed 220 configuration
open/close cases with zero residual tracked bytes. These are decoder/memory
results, not owned-host audio results; omitted codecs are not distributed here.

Earlier Classic SBC and LDAC receive/decode/I2S progress was captured using the
historical third-party host. That host and its local history are excluded.
Those captures cannot qualify this replacement host. Concurrent Classic audio
and BLE, SBC/Opus streaming in this subset, real PCM output, pause/resume behavior, synchronization,
and end-to-end latency still require target validation for the owned runtime.
No release ZIP or release qualification is implied by this source snapshot.
