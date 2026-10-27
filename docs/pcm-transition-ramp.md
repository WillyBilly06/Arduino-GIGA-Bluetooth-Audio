# PCM transitions at starvation and recovery

The DMA consumer previously changed immediately from the last real sample to
zero padding, and resumed queued PCM at full amplitude. Those sample jumps are
a concrete output-path defect. Their audible contribution has not been isolated
with an analog capture or listening comparison.

The consumer now interpolates briefly from the last scheduled stereo sample
toward zero on starvation, and toward incoming PCM on recovery. It preserves
the existing queue cursor, real-input counts, reserve threshold and hold policy.
Uninterrupted PCM remains bit-identical. Initial playback and stop behavior are
unchanged. This correction cannot reconstruct omitted encoded audio.

State is seeded from the final sample of **both** primed DMA halves before DMA
starts: the other half plays before the first refilled half. A transition carries
across halves; an early direction change restarts from the actual scheduled tail.
Its length is the nearest power of two to the approximately one-millisecond DMA
half, bounded to 2–256 frames. At 192 kHz it spans 256 frames, or about 1.33 ms.
There is no additional PCM allocation, clock read, logging or interrupt masking
in the transition path. Only transitioning frames enter the interpolation loop.

The integer blend uses bounded 64-bit products and shifts. Actual installed
Cortex-M7 object inspection finds no division instruction/helper in the envelope;
the compiler reports a 56-byte stack frame for that function. These are codegen
observations, not measured interrupt latency or a total interrupt-stack bound.

`GBR_PCM_TRANSITION` reports stereo-frame counters:

- `synthesized`: absent-input slots prepared with either channel nonzero.
- `zero_padding`: absent-input slots with both words zero.
- `mixed_real`: received frames processed by the recovery envelope, even if the
  resulting values happen to match. This overlaps real-input accounting.

`synthesized + zero_padding == starved + held`. Legacy API `silent_frames`
fields count absent-input slots; the example labels these `missing_pcm_frames`.
They must not be described as literal zeros once interpolation is active.

Original actual-backend tests reproduce hard starvation and recovery edges.
The candidate passes those tests (9 and 12 checks), 21,825 transition checks,
831 backend lifecycle checks, and the existing I2S suites. Independent tests use
ASan and trapping UBSan. Coverage includes partial starvation, stereo opposite
signs, integer extremes, continuity across halves, early reversal, primed-tail
ordering, unchanged consumption and accounting. Native HAL/MMIO/DMA models do
not prove physical output or preemption.

Frozen implementation and test evidence is retained in
`build/pcm-transition-probe/`; the independent review is in
`build/pcm-transition-independent-review/`. Root integration/build evidence is
in `build/pcm-transition-integration/`.

The maintained candidate links for Arduino GIGA core 4.6.0 with a 32 KiB main
thread stack, the owned host, and the unchanged controller patch. Image
`4ece5feffa2cdb14e250318c8b5744f8224b7613cea28e2e9ed046c94a00a4fa`
(466468 bytes) was written and fully compared with flash readback in
`docs/validation/captures/20261022-123050-m7-dfu.json`. The first linked candidate
was preserved without flashing before adding the read-only priority diagnostic.

A subsequent 95-second LHDC 192 kHz/24-bit/fixed 1000 kbps capture ran three
GATT sessions with all 30 acknowledged writes and exact echoes passing. All 45
retained snapshots report streaming and BLE connected. Live priority reads show
UART7 priority 0, DMA1 stream7 priority 6, and grouping 3: UART can preempt the
refill. This establishes priority capability, not measured interrupt latency.
Decoder, queue-full, DMA, transport and diagnostic-drop counters did not increase.

The same run still gained 27 timestamp/inner-sequence discontinuities and 702336
missing-input slots (46080 starvation plus 656256 reserve-hold). Of these slots,
61200 contained nonzero synthesized transition output and 641136 contained
literal zeros. Another 61440 received frames were mixed during recovery. The
accounting invariant holds. These are periodic software snapshots, not a closed
packet ledger, wire measurement, or acoustic comparison. The changed source
content and connection history preclude ranking this run against older captures.
The correction is not a demonstrated cure for the remaining encoded omissions.

Raw serial, exact GATT receipts and numeric analysis are retained in
`build/uart-rx-residence-live/runs/68d7604986bb48a59979fdc9a30c063f/`.
