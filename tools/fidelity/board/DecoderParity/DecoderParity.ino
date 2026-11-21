// On-board decoder parity: decode embedded encoded streams through the
// production MediaAssembler/Decoder on the M7 and stream the PCM to the host.
// Protocol (binary, after the host sends 'g'): tag byte + u32 length + payload.
//   'C' case name, 'D' int32 stereo PCM, 'E' {errors, frames, decode_us}, 'Z' done.
#include <GigaBredr.h>
#include "detail/Decoder.h"
#include "detail/Media.h"
#include "parity_data.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;
static int32_t pcm[2048 * 2];
static void block(char tag, const void* data, uint32_t n) {
  Serial.write(uint8_t(tag)); Serial.write(reinterpret_cast<const uint8_t*>(&n), 4);
  if (n) Serial.write(reinterpret_cast<const uint8_t*>(data), n);
}
static void runCase(const ParityCase& k) {
  CodecConfig c; c.codec = Codec(k.codec); c.sample_rate = k.rate; c.bits_per_sample = k.bits; c.channels = 2;
  if (c.codec == Codec::Opus) c.frame_duration_us = 20000;
  if (c.codec == Codec::LhdcV5) c.frame_duration_us = 5000;
  block('C', k.name, strlen(k.name));
  uint32_t errors = 0, frames = 0, us = 0;
  Decoder dec;
  if (dec.open(c) != Status::Ok) errors = 1000;
  else if (k.raw) {
    size_t pos = 0;
    while (pos < k.bytes) {
      uint32_t t = micros(); DecodeResult r = dec.decode(k.data + pos, k.bytes - pos, pcm, 960); us += micros() - t;
      if (r.status != Status::Ok || !r.consumed) { ++errors; break; }
      block('D', pcm, r.frames * 8); frames += r.frames; pos += r.consumed;
    }
  } else {
    MediaAssembler media;
    if (media.configure(c, 8192) != Status::Ok) errors = 2000;
    size_t pos = 0;
    while (!errors && pos + 2 <= k.bytes) {
      uint16_t len = uint16_t(k.data[pos] | (k.data[pos + 1] << 8)); pos += 2;
      const uint8_t* packet = k.data + pos; pos += len;
      MediaView view; Status st = media.feed(packet, len, view);
      if (st == Status::NeedMoreData) continue;
      if (st != Status::Ok) { ++errors; continue; }
      size_t off = 0; bool need = false; const size_t cap = 1024;
      while (off < view.bytes) {
        size_t in = view.bytes - off;
        if (c.codec == Codec::Aptx || c.codec == Codec::AptxHd) { size_t w = c.codec == Codec::Aptx ? 4 : 6, b = cap / 4 * w; if (in > b) in = b; }
        uint32_t t = micros(); DecodeResult r = dec.decode(view.data + off, in, pcm, cap); us += micros() - t;
        if (r.status == Status::NeedMoreData && !r.consumed) { need = true; break; }
        if (r.status != Status::Ok || !r.consumed || r.consumed > in || r.frames > cap) { ++errors; break; }
        block('D', pcm, r.frames * 8); frames += r.frames; off += r.consumed;
      }
      if (need && off && c.codec == Codec::Aac) { if (!media.retainDecodedSuffix(view, off)) media.consumed(); }
      else if (!need) media.consumed();
    }
    media.close();
  }
  dec.close();
  const uint32_t stats[3] = {errors, frames, us}; block('E', stats, sizeof stats);
}
void setup() { Serial.begin(115200); }
void loop() {
  const int command = Serial.available() ? Serial.read() : -1;
  if (command == 'B') { delay(100); _ontouch1200bps_(); }  // DFU entry for tools/flash_giga.py
  if (command == 'g') {
    for (const auto& k : parityCases) runCase(k);
    block('Z', nullptr, 0);
  }
  delay(1);
}
