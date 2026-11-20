# Samsung SSC decoder integration (host-verified only)

Samsung Scalable/Seamless Codec (SSC) is added as `Codec::Ssc`, an A2DP vendor codec
(type `0xFF`, vendor `0x00000075`, codec `0x0103`). `GBR_ENABLE_SSC` (default 1)
selects it; with 0 every SSC decoder unit compiles to an empty object and no endpoint is
advertised. The decoder is the BSD-3 `samsung-ssc-decoder` at commit
`b9425314ccf961599ac16d14a14d907e679eb559`, vendored under `src/vendor/codecs/ssc/`
with only a notice and build guard added (`src/vendor/codecs/ssc/manifest.json`).

## Behaviour

* **Endpoint.** SEID 10, appended after the existing nine so their SEIDs do not move.
  Capability `00 FF 75 00 00 00 03 01 3E`: 44.1/48 kHz, unlimited bitrate, HiFi 48 kHz/24
  and UHQ v2 96 kHz/24. The mandatory SBC endpoint stays and there is no Content Protection.
  The source chooses the codec. Galaxy phones rank SSC first, and the sink does not
  rank the endpoints.
* **Configuration.** The flags must hold exactly one of HiFi `0x04`, UHQ v2 `0x02` or one
  rate bit, plus optional `0x08`, and every selected bit must be in the advertised
  capability. `0x20` maps to 44.1 kHz/16, `0x10` to 48 kHz/16, `0x04` to 48 kHz/24 and
  `0x02` to 96 kHz/24. Everything else is rejected with `0x29`, including `0x40` (32 kHz,
  which the decoder cannot handle), the capability-only `0x01`, two modes at once, and
  length or ID mismatches. Channels are always 2.
* **Legacy UHQ `0x80`.** The decoder handles the 96 kHz mode, and the parser maps `0x80`
  to 96 kHz/24. It is **not advertised**, so a SetConfiguration that selects it is
  rejected because the bit is not in the capability. Phones never configure it against a
  `0x3E` sink (docs/a2dp.md of the decoder). Adding `0x80` to the capability byte would
  offer it, but no sender has been tried.
* **Media.** RTP header, then one or two frames back to back with no payload header.
  `MediaAssembler` hands over the payload without copying it (6 bytes minimum, at most the
  access-unit size). Each decode call takes one frame, sized from its own header by
  `ssc_frame_bytes`, and the receive loop moves on by `consumed`. This covers two-frame
  96 kHz packets and a bitrate change on any frame. If a frame claims more bytes than are
  left, or fails to decode, the call returns `CorruptFrame`, resets the decoder history
  and drops the rest of that packet. Frames never span packets, so the decoder never asks
  for more data.
* **PCM.** `ssc_decode_s32` writes straight into the library's int32 buffer. 16-bit full
  scale lands at `1<<31`, the same as `expand16`, and the 24-bit precision of HiFi/UHQ is
  kept. The frame capacity is 864.
* **Memory.** The adapter state plus the decoder's three blocks (22,920 bytes,
  `ssc_decoder_size`) are allocated through `gbr_malloc` when an SSC stream opens and freed
  when it closes. Nothing is allocated while decoding and no SSC assembly buffer is used.
* **Buffers.** The automatic profile has a 60 ms startup and a 70 ms PCM ring. It allows
  11 packet slots at 44.1/48 kHz and 18 at 96 kHz, with frames of 9 to 19.6 ms.

## Verified on the host (Linux, clang 21)

* `tools/test_ssc.py --asan --sanitize --reference-root <upstream>/ssc_decoder`: PASS.
  Full UBSan (trap mode, only vptr off) plus ASan and LeakSanitizer.
  Seven RTP-packetised streams of real Samsung-encoder frames go
  through `Codecs.cpp` and `Media.cpp`. The int32 PCM matches the standalone upstream
  `ssc_decode_s32` bit for bit, both live and against the pinned digests. The streams
  cover 44.1 kHz 192k; HiFi 48 kHz/24; UHQ v2 96 kHz at 291k with two frames per packet;
  584k with one frame per packet; a per-packet bitrate switch at 44.1 kHz across
  192/128/88/152k (224 to 484-byte frames, both header families); a 96 kHz switch between
  584k and 291k, including mixed-size two-frame packets; and the 96 kHz mode at 192k.
  The frames are the first 24 (48 for UHQ 291k) of the upstream `test_vectors/ssc` files.
  They are stored as base64 text in `tests/fixtures/ssc`, with source and output hashes
  in its `manifest.json`.
* Malformed input, using exact-size heap copies so ASan catches any over-read: every
  truncation of the first packets (a cut between two 96 kHz frames decodes the first),
  every value of each header byte, 1,500 random payload corruptions, random payloads
  behind valid headers for all 14 rate codes, and short or oversized payloads and bad RTP
  at the media layer. Each call returns `Ok` or `CorruptFrame`, the decoder still decodes
  afterwards, and nothing is allocated while decoding.
* Allocation accounting: four tracked blocks per open. Every allocation failure point is
  handled with no leak. 300 open/close cycles leave zero blocks. Unsupported rates, mono
  and 32-bit are refused before anything is allocated.
* `tests/test_owned_classic.cpp`: the SEID 10 discover and capability bytes, all eight
  accepted Galaxy flag values, 13 rejected flag values, wrong length, codec ID or vendor,
  and a cross-SEID configuration. `tests/test_buffer_sizing.cpp` and
  `tests/test_buffer_policy.cpp` compile with the SSC profile.
* `tools/run_owned_tests.py --sanitize` now runs `tools/test_ssc.py` without a live
  reference. With this patch applied to the current canonical tree, the plain command
  passes, and it also passes on that tree without the patch. On the older snapshot the
  patch was made against, the plain command stops at a missing `<cstring>` in
  `tests/test_classic_media_progress.cpp`. That failure is pre-existing: it happens the
  same way without the patch, and later canonical changes fixed it. On that snapshot a
  host-only compiler wrapper that adds the missing standard includes lets the full suite
  pass, with and without the patch.
* M7 compile of `examples/Receiver` (arduino:mbed_giga 4.6.0): 578,352 B flash and
  54,936 B globals, against 544,984 / 54,928 without the change. That is +33,368 B flash,
  of which about 34 KB is decoder code and const tables, and +8 B globals. With
  `-DGBR_ENABLE_SSC=0` it is 545,144 B / 54,928 B and the ELF has no `ssc_` symbols.
  On the current canonical tree it is 545,304 → 578,736 B flash (+33,432) and
  54,928 → 54,936 B globals.

## Not verified

* **No Galaxy (or any) phone interoperability.** Endpoint discovery, the phone's codec
  choice, SetConfiguration and real packet cadence or bitrate changes have not been seen
  over the air. Delay reporting and the phone's UHQ setting are also untested.
* **No M7 real-time measurement.** Nothing was flashed. The cost of decoding on the
  Cortex-M7 is unmeasured, and so is headroom at 96 kHz, where two frames per 10 ms
  tick are needed. The upstream ESP32 figure (about 4.5 ms per frame at 240 MHz) does not
  carry over. The M7 build uses the core's default flags without
  `-fno-math-errno` and may contract to FMA, so its PCM is not claimed bit-exact with
  the host result.
* **No acoustic or listening check.** Fidelity is only shown as equality with the upstream
  decoder, whose own accuracy rests on its encoder bit-position checks.
* Not exercised: legacy UHQ `0x80` from a real sender, 886k frames larger than the
  2048-byte automatic SDU, packet loss concealment (a bad frame is dropped, not
  concealed), and clock-compensation behaviour with 864-sample blocks.
