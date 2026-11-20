# Experimental HF/HS receiver

This sketch opts into the existing HFP/HSP control and voice worker. It retains
the A2DP codecs and BLE server, uses D70/D72/D74 with MCLK disabled, and prints
the actual controller/backend/service-level/audio states through a bounded USB
console. The monitor setting is 921600.

The example explicitly permits the known Broadcom controller's readback-bound
PCM route setup. The setup preserves the other route fields and refuses an
unknown or failed controller contract. A successful `begin()` alone does not
establish that voice is available. Inspect `backend`, `slc`, `audio`, setup and
controller status before interpreting a call-audio result.

No call is placed automatically. With a real established HF service-level
connection, `a` requests answer and `h` requests hangup, once per serial input.
The request binds the observed HF generation; `Busy` is not automatically
replayed. The library now exposes explicit HSP button intents through
`headsetCommand` with a token copied from `voiceStatus`; this example sends none.
An idle button may redial/place a call, so do not automate it. Physical HSP
button behavior and advanced HF procedures remain unqualified.

Microphone input defaults to explicit None/silence. To transmit microphone
audio, supply a `VoicePcmProvider` through `setVoiceInput`, honor its requested
format, and fence its lifetime using `detachVoiceInput` before freeing its
context. No microphone pin or input hardware is assumed by this example.

Controller routing, phone interoperability, physical DAC/call audio, microphone
hardware, long-call drift and acoustic quality remain unqualified. The source
and CPU models are described in [voice status](../../docs/voice-phase2-source-status.md).
This is a qualification example, not a claim of a production hands-free device.
