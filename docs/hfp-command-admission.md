# HF command admission and generation

The expected generation for `voiceCommand` is `VoiceStatus.hands_free.generation`. The top-level `VoiceStatus.generation` belongs to the synchronous audio session and may be0 while a valid HF service-level connection is ready. It is not the command-generation token.

Commands are explicit typed intents. Ok means the valid intent was copied, not that a phone executed it. A valid pending user or codec transaction must report Busy without replacing the earlier intent; invalid or stale HF generations remain InvalidArgument. A caller may retry Busy after observing current state and must discard generations after receiver end/begin.

Root executed the unchanged wrapper regression against the old mapping: valid pending Answer and pending BCS codec cases both returned InvalidArgument instead of Busy; invalid generation remained correctly rejected. The one-line wrapper now maps BTH_MORE to Busy. The regression establishes actual secure host, PSM3/RFCOMM and HF SLC software states, then checks queued Answer followed by Hangup, or a pending BCS codec transaction followed by Hangup. It invokes no phone/Bluetooth/media APIs and performs no call. Fresh GREEN CPU execution has its own receipt; this control result is not phone interoperability proof.
