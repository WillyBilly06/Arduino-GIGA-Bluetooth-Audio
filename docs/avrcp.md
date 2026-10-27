# AVRCP control and received state

The project-owned host already receives absolute/relative volume, bounded UTF-8 track metadata and playback-status notifications. This work adds typed transport taps and copied public snapshots. It does not use the BTstack runtime, start a radio/I2S endpoint, or change decoder, buffer, clock, controller-HCD or BLE policy. The new controls have not yet been qualified with a phone.

Call these APIs from foreground code and continue calling receiver.poll() for the existing bounded callbacks. Serialize begin/end/poll and keep the receiver alive through callbacks. Commands/cancellation use the same nonblocking radio-owner try-lock as BLE data calls: Busy asks the application to retry admission later; request_id is unchanged unless admission succeeds. Snapshots briefly serialize with the radio owner and return copied values. These are not ISR APIs.

```cpp
auto peer = receiver.avrcpStatus();
uint32_t request = 0;
if (peer.connected) {
    // Invoke once per deliberate button action, not every iteration of loop().
    Status result = receiver.avrcpCommand(AvrcpCommand::Pause, peer.generation, request);
    // Only Ok admits this request. Save peer.generation and request together.
}
auto metadata = receiver.avrcpMetadata();
if (metadata.valid_attributes & 1u) {
    // metadata.title is a copied, NUL-terminated UTF-8 string.
}
```

Supported typed commands are Play, Pause, Stop, Next and Previous. One request may be outstanding. Queued means copied intent waiting for two control slots and two transaction labels. Pending means both ordinary AVRCP press/release transactions were committed to the existing ordered queue. Completed requires actual matching ACCEPTED responses for both; it does not prove that the remote player changed state or that audio played. Rejected, TimedOut, Disconnected and TransportError remain distinct. press_response/release_response retain the actual AV/C response code, with0xff meaning unknown. No uncertain command is automatically retried. Three-second queue/reply deadlines use unsigned clock wrap before control response consumption and in the existing host poll; expired labels are not reused on that channel. The diagnostics/tests must verify target operation before a phone-interoperability claim.

Cancel with receiver.avrcpCancel(saved_generation, request). While Queued, cancellation emits no key. After Pending, both committed transactions remain ordered and the release is retained: cancellation cannot unsend a command. Cancelled then requires both responses; a reject or timeout takes precedence. Matching repeated cancellation is idempotent. AVRCP session generations change on every channel reconnect, including within the same Classic link; zero means detached. A stale generation or request cannot cancel a replacement. The last command result is identified by command_generation/command_id independently of the current connection generation. Discard all scalar tokens after end()/begin(); counters refuse exhaustion instead of wrapping identities.

The copied metadata fields are title(attribute1), artist2, album3 and playing_time7, at most127 UTF-8 bytes each plus NUL. valid_attributes and truncated_attributes use bit(attribute_id-1). A valid empty string differs from an absent value. A track CHANGED event clears current values; an older in-flight response is consumed without repopulating the new track. A newer playback notification also prevents an earlier GetPlayStatus response from replacing current playback state. Metadata continuations and abort responses are independent of status transaction rejection. Metadata callbacks retain their existing foreground-poll lifetime and bounded event queue; copy callback text if retaining it. Playback status is a received AVRCP value, independent of A2DP streaming. song_position_ms is the last observed position, not an extrapolated clock;0xffffffff means unknown.

This implements neither browsing nor cover art, seeking/hold controls, repeat/shuffle/player-settings, a general command FIFO, or full AVRCP coverage. Public metadata/status access is not a full HFP/HSP/RFCOMM/SCO or microphone implementation.

## Verification state

Root observed the missing public API compile RED with the official ARM compiler before production edits. The new source fixtures exercise the actual ClassicProfiles/controller/control queue and the public facade with hardware-boundary stubs. Root executed Android ARM64 CPU-only baseline fixtures and observed the intended lifecycle/metadata regressions fail before the local corrections. Corrected-source Android builds and M7 object compilation are recorded separately; Root subsequently executed all28 final Android CPU cases with return0, including separate regressions, stale/valid disconnect response IDs, copied-control media/HNC preservation, scoped partial drain and the full Classic/AVRCP fixtures. The full official CM7 Receiver link also passed with all codec defaults, UART3M, SCHED0 and pins70/72/74/-1. Actual M7 execution and real phone interoperability remain unverified. Compiling fixtures does not establish their assertions passed. No Windows project native executable was run by this lane, and no phone, radio, waveform or audio-cure result is claimed.

Committed control SDUs cannot be withdrawn by cancellation, timeout or a later channel-generation change. The transport queue is scoped to the Classic peer generation and may retain already committed old-CID SDUs until its ordered drain or whole-peer teardown. The new scoped retirement barrier retains the old channel identity and delays replacement admission until its queued/partial SDUs drain and the ordered adapter mailbox is ready. It copies AVRCP-only closing/reuse signaling into the existing bounded FIFO so media delivery and RX credit release continue. A stalled barrier requests actual Classic disconnect; it does not detach a live queue or fabricate HCI credit release. The final portable CPU suite verifies the scoped query, partial drain, same-CID compound admission, mailbox fence, media/HNC progress and real stalled-disconnect lifecycle; those observations remain separate from field interoperability. DisconnectionResponse retirement requires the actual pending state and matching disconnect identifier. No partial ACL SDU is canceled.
