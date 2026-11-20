> **Current local source, October 29:** the legacy `allow_rx_pacing` option is
> ignored and the HCI SCO backend requires verified synchronous completion
> flow. HF/HS remain off by default and voice/call interoperability remains
> unqualified. Earlier source and test statements below are historical.

# Experimental voice phase2 source

This experimental source continues the control owner stage. It is not a stable HFP/HSP release or a repair for the currently investigated A2DP underrun. Voice is disabled by default. Controller routing, phone interoperability and physical voice output remain unqualified.

Local successor, 2026-10-28: the public HSP button facade is now implemented as
`headsetCommand(HspCommand::Button, status.headset_command_token)`. It binds the
receiver lifetime and HS session generation, uses bounded Busy admission and
preserves secure-link/HF-versus-HS/retirement fences. An idle button can redial
or place a call, so no automatic or physical idle-button test is included.
Targeted actor/facade tests and the integrated native suite (93 runtime
invocations/eight children) pass. Private AVRCP and voice probes also pass full
official M7 builds/static audits. Phone execution/call audio remain unqualified.

The actual source path now connects secure Classic ACL ownership, PSM3/RFCOMM and existing HF/HS controls to a separate synchronous handle/generation. It copies incoming H4SCO into bounded queues, emits owned accept/reject/disconnect commands, handles real controller completion counts separately from ACL/LE credits, and preserves exact copied TX receipts during worker mailbox contention. Reject retirement waits for the final synchronous connection event. Unsupported live synchronous geometry changes close the actual SCO handle rather than continuing with the old packet cadence.

An opt-in controller setup state machine uses standard command discovery and synchronous-flow readback. On a known Broadcom manufacturer only, successful FC1D readback must show routing1. Default policy never writes that route. Explicit `allow_controller_route_configuration` permits only FC1C after a successful five-parameter baseline, changes routing only, preserves the other four values, and verifies all five. Unknown/failed setup leaves voice unavailable; uncertain route writes require reset. No command was sent to the real controller by this lane. The exact stock CYW4343W/HCD HCI voice route remains a hardware qualification question.

Only a usable setup plus a caller-reserved real worker enables profile registration. HF/HS SDP records and EIR UUIDs are conditional; an unavailable backend refuses PSM3. The null default retains the published control stage's default refusal. A restart restores the original UUID list before any new registration. Basic remote gains and optional codec negotiation are advertised; unsupported three-way/CLI/voice-recognition/indicator extensions remain unset.

The existing decoder worker services voice through bounded mailboxes; there is no extra radio thread. Radio hooks never allocate, decode, call microphone providers or configure I2S. `setVoiceInput` copies a provider, while `detachVoiceInput` returns Ok only after fencing old callbacks. Busy requires a retry before destroying that context. The token binds the receiver lifetime and provider epoch; new begin/end lifetimes reject old tokens. None is explicit silence, counted separately from provider starvation. Speaker and microphone gains are separate from AVRCP volume. TX demand follows actual completed synchronous bytes. The earlier experimental RX-derived pacing policy is now disabled; its legacy opt-in is ignored, and RX progress is not treated as controller TX retirement.

Voice owns I2S exclusively. PCM5102A with MCLK=-1 uses16kHz physical output for both formats: native mSBC16k, and CVSD8k through an independently generated63tap2x halfband interpolator (~1.94ms group delay). Its selected output workspace is576bytes for CVSD or960 for mSBC. Worker PCM/packet storage is128bytes or604 respectively; the mSBC encoder/decoder context is separately lazy. A2DP's selected context is retained during admission for rollback, then rebuilt with a fresh output/decoder epoch when voice ends. Media intentionally preempted by voice has its own counter. A voice allocation failure before output takeover leaves music playing.

Software tests cover codec numeric comparisons, per-instance state, allocation lifecycle, H2 framing, host command/handle ownership, partial sink writes, bounded TX receipt retirement, input callback fencing, gains, conditional SDP/EIR, output arbitration/restoration and source bindings. WASM tests use fake mutex/clock/allocator/radio/I2S boundaries and do not prove concurrency or target timing. Eleven Android ARM64 fixtures passed on the separately frozen voice basis, which used older AAC source. The combined dev.15 candidate subsequently passed Receiver and VoiceQualification CM7 compile and static link audits; no combined runtime or hardware voice qualification is claimed. See [dev.15 validation](validation/dev15-2026-10-27.md).

Still required before claiming usable call audio: exact controller route/flow and HCI packet geometry verification; real M7 worker/stack/concurrency qualification; SCO/mSBC interoperability with the phone; long-call clock/drift and loss concealment tests; real microphone-provider hardware; latency/acoustic tests and broader profile procedures. Controller-to-host SCO flow is currently off; RX uses the reserved bounded transport/worker queues. Legacy SCO geometries without usable packet lengths and live synchronous renegotiation are not qualified. The public HSP button facade is implemented, while physical button behavior and optional advanced HF procedures remain unqualified. The broad native runner now includes the voice dependencies and unavailable-radio boundaries; its integrated93-invocation/eight-child result remains software evidence. No phone call, firmware patch, ROM write or hardware voice experiment is established by this source report.

Primary parameter references: [Linux Broadcom PCM routing](https://github.com/torvalds/linux/blob/master/drivers/bluetooth/btbcm.c), [BTstack HFP HCI voice settings](https://github.com/bluekitchen/btstack/blob/master/src/classic/hfp.c), [BTstack chipset HCI/SCO capability notes](https://github.com/bluekitchen/btstack/blob/master/chipset/README.md), [TI PCM5102A datasheet Table11](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf). These are protocol references; no host implementation was copied.
