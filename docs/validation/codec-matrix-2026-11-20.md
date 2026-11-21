# Over-the-air codec matrix, 0.2.2 (2026-11-20)

Physical GIGA R1 (UID 003C00393533510934353531) running `examples/Receiver`
(owned host, `pcm_ring_d2_sram=true`, BLE advertising, coexistence reserve
200 ms). Sender: PipeWire on a VMware Ubuntu guest with a Realtek RTL8922AU
USB adapter. Each profile was selected with `pactl set-card-profile`, then a
generated 60 s broadband signal (filtered noise plus stepped tones, 48 kHz/16)
was looped into the GIGA's sink with `pw-play`. The GIGA's own counters
(`GBR_STATUS`/`GBR_TIMING`) were recorded for 42-44 s per profile. The BLE
echo load was not running.

| PipeWire profile | Codec on GIGA | Rate / bits | Received kbps | Decode errors | Source loss | Silence |
|---|---|---|---|---|---|---|
| a2dp-sink-sbc | SBC | 48 kHz / 16 | 390 | 0 | 0% | 0 ms |
| a2dp-sink-sbc_xq | SBC-XQ | 48 kHz / 16 | 507 | 0 | 0% | 0 ms |
| a2dp-sink-aac | AAC-LC | 48 kHz / 16 | 290 | 0 | 0% | 0 ms |
| a2dp-sink-aptx | aptX | 48 kHz / 16 | 384 | 0 | n/a (no RTP) | 132 ms |
| a2dp-sink-aptx_hd | aptX HD | 48 kHz / 24 | 577 | 0 | 0.3% | 19 ms |
| a2dp-sink-ldac | LDAC | 96 kHz / 24 | 707 | 0 | 30% | 0 ms |
| a2dp-sink-opus_g | Opus | 48 kHz / 16 | 262 | 0 | 0% | 0 ms |
| a2dp-sink-ssc | Samsung SSC | 96 kHz / 24 | 597 | 0 | 0% | 0 ms |
| a2dp-sink | LHDC V5 | 192 kHz / 24 | 782 (1000k nominal) | 0 | 22.5% | 0 ms |

"Source loss" is RTP sequence gaps; the VM sender drops packets when its
socket returns EAGAIN, and concealment filled every gap here. "Silence" is
PCM frames the I2S output had to substitute because the ring was empty.

## What changed since the 0.2.1 matrix

- aptX HD failed every packet in 0.2.1: the adapter used `aptx_decode`, which
  never re-acquires the codeword parity sequence after a discontinuity. It now
  uses `aptx_decode_sync` (`tests/test_aptx_resync.c`: 15,700 codewords with
  audio after a lost packet vs 471 on the old path).
- SBC/SBC-XQ/aptX showed 15-76 ms of silence with zero loss: at 48 kHz only
  the 50 ms startup reserve applied. With `pcm_ring_d2_sram` the coexistence
  margin now applies at every rate (SBC 150 ms, aptX with a 200 ms BLE
  reserve 260 ms).
- The earlier AAC (20 kbps) and Opus (6 kbps) rows were near-silent input.
  This run used a non-silent signal; both decoded at their full bitrate.

## Limits

- aptX over A2DP carries no RTP header, so lost packets cannot be detected
  or concealed. The 132 ms above came from a ~14 s stretch in which the VM
  sender delivered about 2% below 48 kHz (the window's average byte rate was
  exactly nominal); catch-up is capped at +/-1%.
- One sender (PipeWire in a VM); no phone source in this matrix.
- Decoded PCM was not compared against the source signal on the board, and
  the I2S pins were not measured; "0 errors" means each decoder accepted
  every frame.
