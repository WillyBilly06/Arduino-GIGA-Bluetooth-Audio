# A2DP and BLE operation modes

Use one example per sketch. ReceiverConfig.enable_a2dp defaults to true, so existing callers keep A2DP+BLE operation. Both profiles disabled is invalid. enable_i2s=false only changes audio output; it does not disable Classic. Public configuration consumers must rebuild against the updated header.

| Example | A2DP | BLE GATT | Audio worker / DAC |
|---|---|---|---|
| A2dpOnly | true | false | Existing decoder and I2S path |
| BleOnly | false | true | No decoder thread, codec/PCM queue allocation or I2S startup |
| A2dpBleCoexist | true | true | Existing audio path plus bounded foreground echo |

BLE-only keeps the common owned radio controller/transport/GATT storage. It does not begin the Classic profile, advertise Classic service UUIDs, enable inquiry/page scan or reserve/admit peer0. An unexpected Classic success is disconnected without audio configuration. Hardware BR/EDR capability in RadioStats is not a statement that the selected Classic profile is enabled. A2DP-only leaves BLE advertising disabled through the existing BleConfig.enabled path.

## PCM5102A output

Connect D70 to BCLK/BCK, D72 to DAC DIN, D74 to LRCK/WS, and a common ground. The examples explicitly set MCLK=-1; PCM5102A uses its BCLK clocking path. Supply/logic/analog loading requirements remain those in the main README. BLE-only needs no DAC.

For alternate pins, set config.i2s before begin(). All selected pads must form a hardware-compatible route to the SAME supported I2S peripheral; arbitrary GPIO combinations are not emulated. Invalid audio routes reject begin for A2DP modes. Audio pin/buffer/codec options are unused in BLE-only mode. See runtime-i2s-pins.md for actual compatible pad mappings; alternate wiring is not physically qualified by these examples.

## BLE echo / notification

The public unencrypted service UUID is9f770001-9c49-4aaf-919c-030ad23e65d8. Write RX UUID9f770002-9c49-4aaf-919c-030ad23e65d8 (handle9); read/subscribe TX UUID9f770003-9c49-4aaf-919c-030ad23e65d8 (handle11, CCCD12). This is GATT data, not BLE Audio or a central/client implementation. ATT begins at MTU23 and may negotiate517. For a single notification keep each write within min(512, negotiatedMTU-3), normally20 bytes before MTU negotiation. No application fragmentation protocol is added.

Each loop calls poll(), then the same tested NonblockingBleEcho helper from detail/NonblockingBleEcho.h. It keeps one copied BleWrite and calls bleNotify(origin), preserving generation binding. Busy/OutputFull keeps that exact value and does not dequeue another write; a later loop tries once again. Ok releases the local pending value; stale InvalidArgument or other terminal errors discard it. A disconnected/reconnected peer cannot receive a stale-generation echo within the same begin lifetime. If you add end()/begin() restarting, discard the pending helper first (for example assign echo=NonblockingBleEcho{}); BleWrite origins must never cross receiver lifetimes. Ok means local admission, not remote receipt; without notification subscription the TX value may be readable without a notification. The echo has bounded storage/attempts, not a guaranteed service deadline or loss-free delivery.

There is no user callback, callback logging or retry spin. Serial messages occur once in setup; no while(!Serial) holds radio startup. delay(1) yields the foreground loop; existing worker priorities and controller/audio timing remain unchanged.

## Verification scope

Compile-only tests cover the real public mode selection/pure host policy and actual reply helper retention. Source review must connect the policy calls to real host scan/admission/UUID/resource paths. Official ARM example/object compilation is Root-owned. These examples do not establish controller/page scan/RF behavior, notification delivery, codec fidelity, acoustic continuity, clock-bound compliance or latency; those require separate evidence. Existing intermittent audio/clock failures are not fixed or waived by operation-mode examples.
