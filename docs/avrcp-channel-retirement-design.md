# AVRCP channel retirement

The implemented policy passed the final28-case Root Android ARM64 CPU batch,26 official Cortex-M7 object checks and the full official CM7 Receiver compile/link. Original queued/partial, outer admission/mailbox and stale response-ID RED evidence is preserved in the integration handoff. Actual M7 execution and normal phone interoperability remain unverified.

ClassicProfiles retains the old local CID, remote CID and Classic peer owner when its AVRCP channel closes. Its scoped read-only control-queue query prevents a replacement profile generation from opening while matching old SDUs or a prepared/partial ACL transaction remain. The query changes no queue payload, cursor, reservation or HCI credit.

OwnedHost preflights matching close/reuse signaling before the normal parser can accept a reused CID. One bounded tombstone waits for that scoped drain and the existing ordered adapter mailbox readiness. A DisconnectionResponse can start retirement only for the actual DISCONNECTING channel and matching pending disconnect ID. The old profile generation becomes visibly closed before a compound CLOSE+CONNECT transaction can reuse the kernel slot.

Only AVRCP-related retirement bundles can wait in the existing bounded copied-control FIFO. Media continues through its independent CID and releases its RX leases; LE control remains independent. A stalled5s retirement requests the actual Classic disconnect lifecycle. It neither detaches a still-live TX queue nor returns fabricated HCI credits. Whole-peer teardown follows its existing actual disconnection completion.

All old matching SDUs drain; immediate withdrawal is not implemented. A partially emitted ACL SDU and its queued key release retain ownership and order. Already committed commands can reach the old channel, so cancellation/timeout still cannot promise to unsend them. Replacement admission waits for the ordering barrier; mailbox readiness is not UART TC, RF receipt or player action.

The fixtures verify:

- queued and5-byte-fragment partial taps across same-CID reopen, complete ACL assembly, unrelated Classic/LE preservation and HCI credit conservation;
- real synthetic H4/L2CAP compound CLOSE+CONNECT admission, adapter mailbox fence, eventual configuration/new generation and LE preservation;
- media callback and pending RX completion progress behind retained control, plus actual stalled Classic disconnect while LE survives;
- stale response IDs leaving an OPEN generation intact, and a valid response completing an actual emitted disconnect;
- the existing Classic/AVRCP metadata, cancellation, timeout, facade, abort, signaling and combined owner regressions.

These are bounded software/compile observations. They do not establish actual handset control/metadata interoperability, physical delivery, audio quality, timing or SCO support. HFP/HSP remain unadvertised with no integrated SCO/duplex voice path.
