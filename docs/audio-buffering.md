# Configured audio buffering (working development branch)

The decoder fills an actual PCM reserve before playback. I2S DMA then consumes at the negotiated sample rate, with the existing slow bounded clock-drift correction. Decoding ahead replenishes storage; backpressure bounds further writes. Fragment arrival, compressed queue fullness and allocated byte capacity do not authorize early playback.

Zero global fields select automatic settings for the negotiated codec, rate and decoded block size. Nonzero codec fields override nonzero globals. Only the selected queue, needed AU assembler, one decoded block and one PCM ring allocate; profiles for other codecs are data only.

## Startup and recovery

The automatic startup target is the larger of the codec's base reserve and its decoded block duration rounded up to milliseconds plus 12 ms. Automatic ring capacity adds 10 ms. The same real-frame watermark governs starvation recovery. A shortage remains an error with absent/synthesized PCM counted; recovery does not restore missing audio.

| Selected configuration | Startup target | Ring capacity |
|---|---:|---:|
| SBC44.1, AAC-LC44.1 | 50 ms | 60 ms |
| HE-AACv1 44.1 | 59 ms | 69 ms |
| HE-AACv1 48 | 55 ms | 65 ms |
| aptX48 | 60 ms | 70 ms |
| aptX HD48 | 55 ms | 65 ms |
| LDAC96 | 50 ms | 60 ms |
| LHDC V5 192 | 80 ms | 90 ms |
| Opus48 /20-ms blocks | 40 ms | 50 ms |
| Opus48 /40-ms blocks | 52 ms | 62 ms |
| Opus48 /60-ms blocks | 72 ms | 82 ms |

These are proposed engineering settings undergoing validation, not measured minimum jitter coverage. The requested 200 ms total phone-to-DAC latency remains unmeasured. Accepted private low-rate modes can require more reserve: HE16k and AAC-LC8k use 140-ms startup/150-ms rings. Such modes cannot be assumed to fit 200 ms total; HE16k is not advertised by the current radio profile. HE8k is rejected by the decoder.

The LHDC base reserve was increased from 60 to 80 ms after the public dev.11 60/70-ms image, at opt-in 3 Mbaud, recorded 59,328 missing PCM frames during a 600-second LHDC192/BLE test. RTP sequence/timestamp/codec-sequence discontinuities and decode/transport errors were zero; the longest recorded host-arrival interval was 76,053 us. This establishes inadequate reserve in that observed run, not the origin of the delivery delay. The 80/90-ms public-code SCHED0/3M image completed a600-second host window with3BLE cycles,130 verified512-byte exchanges and zero recorded audio/RTP faults, but14 clock overruns. This is not a stable-operation or total-latency guarantee. All other codec bases remain unchanged.

DMA halves retain the existing approximately 1-ms duration, rounded to four stereo frames for separate cache lines. Let H be a half, P the real-frame startup watermark and R the ring capacity. Require `2H <= P <= R`. P includes the 2H frames subsequently moved to DMA. A decoder block may fill beyond P before prime; `GBR_BUFFERS` reports the configured threshold, actual initial queued frames and DMA-half size. It is a minimum reserve, not an exact measured startup time.

## Capacity and memory

Automatic maximum packet size is the advertised 2048-byte receive MTU; AU capacity is 8192 bytes and allocates only for codecs that need assembly. Queue slots use a 120-ms/block-duration hint plus four slots, bounded to 8..64 before a 64-KiB stride-allocation target. Each slot costs `maximum_packet_bytes + 2`, so 2048-byte slots permit 31 under that target. At least one slot remains available for an explicitly larger legal packet. Explicit slot counts are never silently byte-capped. Slot capacity does not determine packet duration or grant additional playout delay.

Stereo S32 storage uses 8 bytes per frame, including expanded mono and lower-bit-depth input. Decoder scratch holds one selected block. PCM ring bytes are `8R`; double-DMA bytes are `16H`, plus ring-object/alignment space. At LHDC192 automatic geometry, R=17280, H=192 and the shared DMA/ring arena is 141344 bytes on the checked ARM layout; 28 incoming2048-byte slots cost57400 bytes before allocator headers. The selected LHDC arena increases by 30,720 bytes from dev.11; other codec arenas are not allocated alongside it. Decoder state, AU assembly, packet copy, scratch and RTOS stacks are additional allocations. Actual heap/headroom and fragmentation still require hardware checks.

The existing clock inventory A-D-P already includes both queued ring PCM and unsent decoded scratch. Do not add the ring a second time when accounting for buffered audio. DMA completion counters are not physical output measurements.

In real-PCM startup mode, the running clock controller uses the actual backend startup watermark minus the two already-prepared DMA halves as its inventory target. For LHDC192 this is15360-384=14976 frames (78 ms in the controller's domain, plus2 ms already prepared for DMA). The healthy two-second learning period still initializes packet estimates and filtering; it no longer replaces this configured target with the early average, including after recovery. The target includes estimated compressed work as well as PCM, so it is not a guaranteed minimum amount of real PCM or measured total latency. Playback continues while the decoder replenishes storage.

Explicit legacy startup_packets retains learned-target behavior. A manually selected real-PCM watermark of only2H leaves no unprepared reserve: audio still follows that manual priming choice, while optional clock tracking reports eligible=false instead of selecting an empty target or silently learning another one. No new allocation, public configuration field, playback pause, clock slew or hardware-timing change is introduced by this handoff.

Fast and Dma categories share the default allocator. The current audio clock requires exclusive PLL2 ownership; stock Arduino SDRAM initialization also selects PLL2R. This change does not initialize SDRAM or override clock ownership. An external allocator must be evaluated with its actual clock/memory setup.

## Explicit settings

```cpp
ReceiverConfig config;
auto& lhdc = config.codec_buffers[static_cast<size_t>(Codec::LhdcV5)];
lhdc.startup_buffer_ms = 80;
lhdc.pcm_buffer_ms = 90;
lhdc.queue_packets = 32; // Explicit capacity; not a startup packet count.
```

For the legacy packet-startup path, set `startup_packets` and leave `startup_buffer_ms` zero in that layer. Its legacy prime/recovery threshold is retained. Setting both in the same layer is rejected. A codec-layer startup mode overrides the global mode. An explicit ring smaller than its requested real-PCM watermark is rejected before codec/buffer allocation. Existing manual profiles remain possible; their aggregate latency is the caller's responsibility.

Compatibility: an explicit LHDC `pcm_buffer_ms=70` with no startup override now conflicts with the inherited 80-ms watermark and is rejected. To retain the old 60/70-ms geometry, explicitly set both `startup_buffer_ms=60` and `pcm_buffer_ms=70`; that geometry failed the field workload above. No manual override is silently enlarged.

Full M7, allocation, clock gates and codec/BLE traffic checks remain required for qualification. Historical dev.10 evidence: the configured-target private SCHED1 Receiver window completed a 90.072132986-second natural LHDC192 source and 46 verified 512-byte GATT exchanges over 60.005 seconds at MTU 517. All 43 non-clock gates were zero, but cumulative clock timing FAILED 2/88 (maximum 22.1229167 configured microseconds; timing_unknown=0). Forty coherent printed tracking cohorts matched the then-current 11136-frame target, zero mismatched; five remain UNKNOWN across 1 ms print boundaries. The first-starvation supply witness was unexercised/coverage unknown and prior starvation remains unresolved. See validation/dev10-2026-10-26.md and its JSON companion (field facts SHA 63382f784242c6e74f0f7c4ff0bbb677705519e2d7a48ba07d5b19f733403679). This private result is separate from the completed default SCHED0 Receiver build-basis compile/static audit and the newer LHDC reserve candidate. Physical I2S continuity, numerical fidelity, acoustics and total latency remain unproved.
