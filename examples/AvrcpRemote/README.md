# AVRCP transport taps and metadata

Select GIGA Cortex-M7, wire the PCM5102A to D70/D72/D74 and open the serial
monitor at 921600. The sketch retains all compiled A2DP codecs and enables BLE.
It uses the receiver's bounded USB console; disconnected USB does not block
the audio worker. Skipped diagnostic rows increment `console_drops`.

Send one character for one action: `p` play, `a` pause, `s` stop, `n` next or
`b` previous. Each admitted request sends ordinary AVRCP press/release messages.
The status row distinguishes admission and response state. `Busy` requires a
new deliberate user action; the example does not replay uncertain commands.
An accepted remote response does not prove that the player changed state.

Metadata callbacks report the supported title, artist, album and playing-time
attributes. Device/player support varies. See [API behavior and limits](../../docs/avrcp.md).
The build check is separate from actual phone/player interoperability.

HFP/HSP voice has a separate opt-in configuration and controller-route
qualification; see [voice status](../../docs/voice-phase2-source-status.md).
This sketch does not start a phone call or enable an unqualified voice route.
