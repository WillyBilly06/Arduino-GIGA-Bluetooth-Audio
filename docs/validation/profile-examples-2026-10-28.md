# Local AVRCP and HF/HS examples, 2026-10-28

Two examples expose the existing project-owned profile APIs without requiring
changes to the Receiver diagnostic sketch:

- [AvrcpRemote](../../examples/AvrcpRemote/AvrcpRemote.ino) keeps A2DP and BLE
  available, receives bounded metadata callbacks and submits one typed
  play/pause/stop/next/previous tap for each explicit serial key. It reports
  admission and paired press/release response state without automatic replay.
- [HfpReceiver](../../examples/HfpReceiver/HfpReceiver.ino) opts into HF/HS
  control and the voice worker. It explicitly permits the known controller's
  readback-bound route setup and reports actual backend, service-level and
  audio readiness. Microphone input defaults to None/silence. Answer and
  hangup require explicit serial input and an observed HF generation.

Both use the existing bounded 4096-byte USB console, pump at most 64 bytes per
foreground turn and retain the all-codec build defaults. They use D70/D72/D74
and MCLK=-1. Neither places a call automatically.

The official Arduino CLI and stock GIGA core 4.6.0 compiled and linked each
sketch for Cortex-M7. Independent static image audits checked all 191 library
translation units, the seven enabled codec switches, six decoder operation
tables, owned host, stock HCD, soft-float calling convention, image load
bindings, vectors and application flash limit. The respective public AVRCP
and voice methods are present in the linked images.

| Sketch | Application BIN bytes | SHA-256 |
|---|---:|---|
| AvrcpRemote | 533792 | `83af92b5eeb4368abf36e31bd69a210be9f0d1618f4018a2f6516bcbdc42f65f` |
| HfpReceiver | 534820 | `8ce2f2436f493166817329eddd21071082ad432360c20a5b8098de96a2f358d9` |

These images have not been flashed or executed. Build success does not prove
that a particular player accepts a tap or supplies metadata, or that the
controller/phone completes a synchronous call-audio connection. Controller
route/flow geometry, physical SCO/mSBC interoperability, microphone hardware,
long-call drift, electrical output, acoustics and total latency remain separate
qualification work. HSP button commands and optional advanced HF procedures
are not supplied by these examples.

See [AVRCP semantics](../avrcp.md), [voice implementation and limits](../voice-phase2-source-status.md)
and [AAC successor evidence](aac-selected-storage-2026-10-28.md).
