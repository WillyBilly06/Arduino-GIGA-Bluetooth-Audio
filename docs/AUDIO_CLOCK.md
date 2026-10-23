# Audio clock compensation and LE control

`ReceiverConfig::clock` enables worker-side clock feedback by default when I2S is enabled. All codec identities use the common decoded-frame/output path; this is not a codec-specific resampler. The controller preserves decoded PCM values and order. It changes only the exclusively owned PLL2 fractional numerator after checking the output epoch, clock consumers and register readback.

```cpp
giga_bredr::ReceiverConfig config;
config.clock.enabled = true;
config.clock.limit_ppm = 500;             // accepted range0..500
config.clock.slew_ppm_per_second = 10;    // accepted range0..10
receiver.begin(config);
```

The hard limit is500ppm and the maximum slew is10ppm per second. Decisions occur at most once per second after learning the existing backlog for two seconds; feedback is sampled at most once per millisecond on the decoder worker. No control IRQ, new startup reserve, sample insertion/deletion or per-sample filter is added. Disabling clock feedback leaves the nominal output-clock path.

For compressed queues with at least four slots, the learned target is bounded
by the exact PCM capacity plus the average frames per packet times
`Q - ceil(Q/4)`. The ceiling is also reduced after a measured packetization
contraction. The measured filter remains intact when a target is clipped.
Queues of one to three slots retain their previous target policy. This is
estimated inventory headroom, not a guarantee of physically free packet slots
or absorption of every burst. The bundled Receiver example's LHDC profile
selects 48 compressed slots and 26 startup packets; the additional 16 slots
request 32,800 bytes only when that profile is selected. Bare ReceiverConfig
defaults remain queue 8/startup 2; configure codec-local buffers explicitly or
start with the measured Receiver example.

Tracking requires a PCM ring holding at least four actual DMA halves. A smaller legal ring remains accepted and plays through the existing path, while `stats().clock.enabled` reports the requested setting and `eligible` remains false. `ring_capacity` and `required_ring_frames` explain this condition. DMA halves round upward to four frames, so a nominal4ms ring can still be insufficient at44.1kHz. Default20ms rings are eligible. Very small buffers remain sensitive to packet timing; automatic inhibition preserves their previous behavior rather than adding clock-control starvation.

`ReceiverStats::clock` is one coherent published observation at `observation_us`, or an explicit begin/stop publication. It is not simultaneous with separately sampled live output/radio fields. `stream_epoch`, `output_epoch` and `expected_output_epoch` distinguish lifetimes. `state` separates disabled, inactive, learning, tracking, suspended and backend fault. Requested and accepted quantized coefficients are separate signed Q16ppm fields (`requested_ppm_q16`, `applied_ppm_q16`; divide by65536). These coefficients and PLL readback are software observations, not a measured wire rate or acoustic result.

Within that row, decoded source creditA is recorded before output writes; explicit post-decode discardD is removed; real prepared framesP include both initial DMA halves; missing/held framesZ remain separate. The exact decoded backlog isA-D-P. Completed accepted-packet counts and framesF/G, queued payload and retained fragments estimate compressed backlog. Valid zero-frame fragments remain represented. New media/output errors, recovery, invalid accounting, unsupported clock ownership or backend refusal suspend/relearn or fault control; they do not turn a historical error counter back to zero. Accepted actuator quantization stays within the current decision interval and is the anti-windup state.

Portable regressions are run with `python tools/test_audio_clock.py`. They use the actual controller/frontend, retained-media metadata, current production counter/write/restore functions and register models. Legacy frontend mocks explicitly report no clock backend; those mocks are not linked into the active clock tests. Native proofs do not measure ARM interrupt masking, physical WS/jitter, RF delivery, acoustics or enduring sender interoperability. The current phone cannot select Opus, HE-AAC or a distinct SBC-XQ mode; separate decoder/native/target evidence does not fill those phone-field gaps. LHDC has no independent encoded PCM reference fixture.

## Nonblocking LE lifecycle requests

`bleDisconnect(expected_generation, advertise_after_disconnect=true)` uses the nonzero generation observed from `bleStats()` during the same `begin()` lifetime. Discard saved generations after `end()`/`begin()`. A zero or stale generation is invalid. `Status::Ok` means one bounded LE-only disconnect was admitted; controller events determine actual departure and when the observed generation becomes zero. A matching duplicate is idempotent, while a conflicting request or occupied radio mutex returns `Busy`. The call does not wait for a radio procedure.

Passing `false` holds later advertising after the observed peer departs. `bleResumeAdvertising()` releases that hold through the existing asynchronous GAP path; it can return `Busy` while a held peer is still attached/disconnecting. Stopped/disabled and unsupported backend calls return `Unsupported`. Neither call proves a physical reconnect, BLE airtime limit or audio quality. LE connection-preference statistics continue to distinguish requested/submitted parameters from controller-observed actual parameters.
