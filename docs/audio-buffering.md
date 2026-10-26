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
| LHDC V5 192 | 60 ms | 70 ms |
| Opus48 /20-ms blocks | 40 ms | 50 ms |
| Opus48 /40-ms blocks | 52 ms | 62 ms |
| Opus48 /60-ms blocks | 72 ms | 82 ms |

These are proposed engineering settings undergoing validation, not measured minimum jitter coverage. The requested 200 ms total phone-to-DAC latency remains unmeasured. Accepted private low-rate modes can require more reserve: HE16k and AAC-LC8k use 140-ms startup/150-ms rings. Such modes cannot be assumed to fit 200 ms total; HE16k is not advertised by the current radio profile. HE8k is rejected by the decoder.

DMA halves retain the existing approximately 1-ms duration, rounded to four stereo frames for separate cache lines. Let H be a half, P the real-frame startup watermark and R the ring capacity. Require `2H <= P <= R`. P includes the 2H frames subsequently moved to DMA. A decoder block may fill beyond P before prime; `GBR_BUFFERS` reports the configured threshold, actual initial queued frames and DMA-half size. It is a minimum reserve, not an exact measured startup time.

## Capacity and memory

Automatic maximum packet size is the advertised 2048-byte receive MTU; AU capacity is 8192 bytes and allocates only for codecs that need assembly. Queue slots use a 120-ms/block-duration hint plus four slots, bounded to 8..64 before a 64-KiB stride-allocation target. Each slot costs `maximum_packet_bytes + 2`, so 2048-byte slots permit 31 under that target. At least one slot remains available for an explicitly larger legal packet. Explicit slot counts are never silently byte-capped. Slot capacity does not determine packet duration or grant additional playout delay.

Stereo S32 storage uses 8 bytes per frame, including expanded mono and lower-bit-depth input. Decoder scratch holds one selected block. PCM ring bytes are `8R`; double-DMA bytes are `16H`, plus ring-object/alignment space. At LHDC192 automatic geometry, R=13440, H=192 and the shared DMA/ring arena is 110624 bytes on the checked ARM layout; 28 incoming2048-byte slots cost57400 bytes before allocator headers. Decoder state, AU assembly, packet copy, scratch and RTOS stacks are additional allocations. Actual heap/headroom and fragmentation still require hardware checks.

The existing clock inventory A-D-P already includes both queued ring PCM and unsent decoded scratch. Do not add the ring a second time when accounting for buffered audio. DMA completion counters are not physical output measurements.

Fast and Dma categories share the default allocator. The current audio clock requires exclusive PLL2 ownership; stock Arduino SDRAM initialization also selects PLL2R. This change does not initialize SDRAM or override clock ownership. An external allocator must be evaluated with its actual clock/memory setup.

## Explicit settings

```cpp
ReceiverConfig config;
auto& lhdc = config.codec_buffers[static_cast<size_t>(Codec::LhdcV5)];
lhdc.startup_buffer_ms = 60;
lhdc.pcm_buffer_ms = 70;
lhdc.queue_packets = 32; // Explicit capacity; not a startup packet count.
```

For the legacy packet-startup path, set `startup_packets` and leave `startup_buffer_ms` zero in that layer. Its legacy prime/recovery threshold is retained. Setting both in the same layer is rejected. A codec-layer startup mode overrides the global mode. An explicit ring smaller than its requested real-PCM watermark is rejected before codec/buffer allocation. Existing manual profiles remain possible; their aggregate latency is the caller's responsibility.

Full M7, allocation, clock gates and codec/BLE traffic checks must pass before qualification. The last measured candidate still failed cumulative clock timing4/91 despite zero sampled media faults. Physical I2S continuity, numerical fidelity, headphone acoustics and total latency remain unproved.
