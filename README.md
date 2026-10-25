# GigaBredr develop: 0.1.0-dev.7

Developmental Arduino GIGA R1 WiFi Cortex-M7 source snapshot with a
project-owned Classic A2DP sink, existing AVRCP volume/metadata path, public BLE GATT
server, configuration-sized decoder storage and PCM5102A I2S output.
**Known audio/coexistence failures remain. This is not a stable release.**

## Current source and results

This update separates verified Classic security from readiness behind a retained
event. Unrelated deferred BLE work can no longer manufacture a Classic security
loss, stop output and rebuild the decoder. Actual security loss still stops
audio; ordered receive, credit, owner and new-profile-work barriers remain.
Only `OwnedHost.cpp` changes in the receiver/compiler source set.

The actual dev.7 M7 build compiled all 168 library units with all seven decoder
defaults. Its image is
`d0a93d5896183a4cfa45aa6490e1d1725d7c13d756dedd19f50c8a326e1cd0ee`
(488068 bytes), byte-identical to the reviewed correction built with dev.6
metadata and exercised on the board. No diagnostic probe, private compact AAC,
worker-stack telemetry or unfinished AVRCP facade is included.

The normal LHDC V5 192 kHz/24-bit test completed 98 verified 512-byte GATT
exchanges over 60.047 seconds, MTU 517 and actual 120 ms. Audio still failed:
1136 short refills, 218112 missing-real PCM frames and eight timestamp/inner
sequence steps, with unchanged audio/output epochs. No new outer RTP sequence,
decoder, queue-full, DMA or transport error was reported. The conditional
retained-event trigger was not observed; this is not hardware path coverage or
a cure for the separate supply gaps. The previous image was restored with full
readback and fresh output, independently from its also-failed audio quality.

For installation, use Sketch > Include Library > Add .ZIP Library in Arduino
IDE. GitHub commit-specific source ZIPs (`archive/<commit-sha>.zip`) preserve
the exact versioned source; the final package is checked against the compiled
source manifest. A successful build does not establish smooth audio.

Typed AVRCP control/status APIs and HFP/HSP call audio remain unfinished.
There is no implemented microphone path yet; the requested pluggable input API
remains required work. See [current status](docs/STATUS.md).

### Historical parent results (dev.6 image)

The ordinary six-mode45-second matrix did **not qualify**:

| Mode | Rate | Finite result |
|---|---:|---|
|SBC/SBC-XQ|44100Hz|45s software gate PASS|
|AAC-LC|44100Hz|45s software gate PASS|
|aptX|48000Hz|45s software gate PASS|
|aptX HD|48000Hz|45s software gate PASS|
|LDAC|96000Hz|45s software gate PASS|
|LHDC V5,24-bit/nominal1000kbps selection|192000Hz|NOT_QUALIFIED|

LHDC reached a late GATT connection, MTU517 and subscription, then the
existing22s helper deadline expired before a completed ten-write/ten-echo
exchange. Receiver diagnostics retained actual120ms BLE parameters together
with PCM starvation/recovery shortages and one RTP timestamp/LHDC-inner step.
Missing-input totals grew from12096 to19392 frames across the failure tail;
these include deliberately held recovery output and are not acoustic-loss
measurements. RTP packet sequence gaps stayed zero, which does not prove
continuous encoded-frame coverage. The source was cancelled rather than
naturally completing its45 seconds. Five passing cases do not create a
six-case PASS or establish a causal fix.

A separate600s playback attempt used a diagnostic observer image, not this
production image. Its fourth LDAC reconnect failed with callback status133;
no qualified600s completion followed, and the intended LHDC600s case did not
run. That independent NOT_QUALIFIED result is retained without transferring
it into production qualification. No phone, controller, RF or host cause is
assigned by these observations. See [current status](docs/STATUS.md).

## Codec, allocation and fidelity scope

All257 selected supplied decoder files remain included. All seven build
macros remain enabled, covering SBC/SBC-XQ, AAC-LC, experimental HE-AACv1,
aptX, aptX HD, LDAC, LHDC V5 and Opus. The host negotiates one A2DP codec at
a time; source inclusion does not make a phone offer every codec. HE-AACv2/PS,
xHE-AAC and BLE Audio are not implemented. Opus, HE and a distinct SBC-XQ
selection retain their separate decoder/interoperability limitations.

Immutable tables stay in flash. Decoder workspaces, packet/access-unit queues
and PCM buffers are allocated for the selected configuration and released on
stop/reconfiguration. The supplied ARM path, mono/stereo geometry, lazy HE/SBR
selection and existing memory-provider API remain intact. This snapshot does
not include the separate private compact AAC RawChannels change or claim its
memory saving. It does not establish SDRAM placement, worker-stack high-water,
independent LHDC PCM fidelity, all low24-bit fidelity, alternate-device
interoperability, electrical I2S accuracy or acoustic quality.

[THIRD_PARTY.md](THIRD_PARTY.md), original per-file notices and source manifests
remain authoritative for component terms and unresolved grants. Apache-2.0
covers original project contributions; it does not relicense every bundled
codec or third-party table, grant patents or resolve redistribution questions.
No new licensing permission is asserted by this update.

## PCM5102A and build

| GIGA pin | PCM5102A |
|---|---|
|D70 / PI1|BCLK / BCK|
|D74 / PB12|LRCK / WS / LCK|
|D72 / PI3|DIN|
|GND|GND|

Use the breakout's required supply,3.3V signaling and Philips I2S. Stereo slots
are32-bit with24-bit samples left-aligned. MCLK defaults to-1/disabled; the
PCM5102A can use its BCLK PLL. Compatible SPI1/2/3 routes are selected through
`ReceiverConfig.i2s`; alternate pads and optional256Fs MCLK electrical output
remain physically unqualified. See [routing](docs/runtime-i2s-pins.md) and
[clock limits](docs/AUDIO_CLOCK.md).

Use Arduino Mbed OS GIGA Boards4.6.0 and the Cortex-M7 target. Start with the
bundled Receiver example, which carries the codec-specific buffer settings:

```sh
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 --jobs 4 --library . examples/Receiver
```

The Receiver uses LHDC48 queued/start26/max2048/PCM20ms, LDAC40/start16,
SBC12/start4, AAC/HE16/start6, aptX24/start10, aptX HD32/start15 and
Opus16/start7. AAC/HE, aptX/HD and Opus also select a2048-byte packet limit.
These are the example's finite tested profiles, not a stable-playback or
latency guarantee. Zero per-codec overrides inherit global configuration.
`availableCodecs()` reports compiled decoders and `config.codecs` limits
negotiation; check `begin()` and the example's status diagnostics.

## BLE and serial diagnostics

BLE is a public unencrypted peripheral/server. MTU starts at23 and can
negotiate up to517; subscribed value size is `min(512, negotiated_mtu-3)`.
An acknowledged blocked request owns its bounded copy before physical RX
storage retires. Generation-scoped replies reject stale reconnect data.
Pairing/encrypted BLE and central/client roles remain unsupported.

During secure Classic audio, host policy services Classic receive work first
and shapes ordinary ATT output with27-byte fragments and10ms spacing. The
default interval preference is80..120ms; Receiver requests120ms, latency2 and
a5s supervision timeout. Production keeps the initial1s connection-age
eligibility condition and the separate peer-reply cooldown. Optional initial
application notifications wait for an actual parameter match or a bounded10s
fallback. Mandatory ATT/security/control work remains available; these host
policies do not reserve RF airtime or guarantee continuous audio. Requested,
submitted and actually observed parameters remain distinct.

Receiver serial output uses a4096-byte main-thread ring and USB `send_nb`
pumps of at most64 bytes. A disconnected monitor skips transmission; a full
queue records diagnostic drops rather than waiting for USB space. Host/decoder
diagnostics are copied through a bounded mailbox before foreground printing.
The setup-only monitor wait is bounded separately. This existing nonblocking
logging path is retained; it is not a new scheduling fix or a guarantee that
logging has no CPU cost. Watch the reported drop counters when recording logs.

## Historical dev.5 evidence

The separately dated dev.5 queue-headroom image
`210791643c83b4de1f79ae45c55e741511d392cfd311ad74304956d161b0cf10`
(487944 bytes) had a fresh extracted-ZIP build/full application readback,
six finite30s phone-mode gates with ten512-byte GATT echoes per case at
MTU517/actual120ms, and a separate passive-established-BLE LHDC192 observation
covering1802.520 receiver seconds. Its documentation also records earlier
decoder/reference/allocation checks and their limitations. These are retained
historical identities and results, not new dev.6 tests or transferable
qualification of the current binary. Earlier continuity failures remain
documented in the dated validation files.

No new broad native, malformed/parser, decoder-stress or crash-fixture suite
was executed to declare this snapshot qualified. Existing source tests and
dated reports retain their own scopes. This source-only develop update
contains no raw captures, device identities, phone data, private paths,
firmware binaries or local history. It does not update main or create a
stable tag/release. Export provenance and a fresh final ZIP build must be
verified before any corresponding packaging claim is made.
