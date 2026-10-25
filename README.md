# GigaBredr develop: 0.1.0-dev.8

Experimental Arduino GIGA R1 Cortex-M7 A2DP receiver with a project-owned
Classic/BLE host, the complete supplied decoder set and configurable I2S.
**LHDC/BLE supply gaps remain unresolved. This is not a stable release.**

## Current change and evidence

Raw-AAC program storage is now sized for one selected program instead of the
generic sixteen-program array. Generic decoder entrypoints retain sixteen.
Arithmetic, SBR, other codecs, radio, clock and buffer policies are unchanged;
the dev.7 Classic security/readiness correction remains included.

At actual AAC-LC44100/stereo/16, selected-decoder requested payload measured
25262 bytes versus26484 baseline: **1222 bytes less**. This is not system free
heap, allocator overhead/rounding, fragmentation, SDRAM or CPU improvement.
Compact90s and baseline45s sources completed naturally with exact routes and
zero source underruns; the compact window completed118 verified512-byte GATT
exchanges at MTU517 over60.246s, with source live before and after the client.

Both receiver actual-fault/missing inventories are empty. Their original broader
NOT_QUALIFIED verdicts remain because each includes an initial Learning-clock
row before Tracking. No row or gate was discarded. This is not all-codec,
Apple/HE, low-bit fidelity, acoustic or long-soak qualification, and does not
resolve the separate LHDC192/1000kbps coexistence failures.

The fresh actual-dev.8 M7 build/audit covers all168 units/seven decoder defaults.
Its487940-byte image is identical to the exercised compact image:
`8c47b9cc80ce27a8e07d6405ae53fe883e69af484f395c7e2448587baccf7777`.
Vendor exported hashes now identify all257 distributed codec sources; original
upstream/portable-input hashes and notices remain, with scopes in THIRD_PARTY.

Install the versioned tag ZIP through Arduino IDE's Sketch > Include Library >
Add .ZIP Library. Commit-specific archives also preserve an immutable revision.
The private typed AVRCP prototype is excluded. Full AVRCP/HFP/HSP/RFCOMM/SCO and
pluggable microphone input remain unfinished. See STATUS and BUILD for scopes
and unchanged historical failures.

## Codec, allocation and fidelity scope

All257 selected supplied decoder files remain included. All seven build
macros remain enabled, covering SBC/SBC-XQ, AAC-LC, experimental HE-AACv1,
aptX, aptX HD, LDAC, LHDC V5 and Opus. The host negotiates one A2DP codec at
a time; source inclusion does not make a phone offer every codec. HE-AACv2/PS,
xHE-AAC and BLE Audio are not implemented. Opus, HE and a distinct SBC-XQ
selection retain their separate decoder/interoperability limitations.

Immutable tables stay in flash. Decoder workspaces, packet/access-unit queues
and PCM buffers are allocated for the selected configuration and released on
stop/reconfiguration. The ARM path, mono/stereo geometry, lazy HE/SBR and memory-provider API remain.
Selected raw-AAC storage now reserves one program record; generic entrypoints
retain sixteen-program capacity. One record is allocated at open, not deferred
until the first PCE. The measured saving covers the configuration below only.
It does not establish SDRAM placement, worker-stack high-water,
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
