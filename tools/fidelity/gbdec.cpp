// Library side: feed A2DP packets through the production MediaAssembler and
// Decoder exactly as the receiver loop does, write int32 stereo PCM.
#include "detail/Decoder.h"
#include "detail/Media.h"
#include "detail/Memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
using namespace giga_bredr; using namespace giga_bredr::detail;
int main(int argc, char** argv) {
    if (argc != 6) { fprintf(stderr, "usage: codec rate bits in.pkt out.pcm\n"); return 1; }
    std::string name = argv[1]; CodecConfig c;
    c.codec = name == "sbc" ? Codec::Sbc : name == "aac" ? Codec::Aac : name == "aptx" ? Codec::Aptx :
              name == "aptxhd" ? Codec::AptxHd : name == "ldac" ? Codec::Ldac : name == "opus" ? Codec::Opus : Codec::Count;
    c.sample_rate = uint32_t(atoi(argv[2])); c.bits_per_sample = uint8_t(atoi(argv[3])); c.channels = 2;
    if (c.codec == Codec::Opus) c.frame_duration_us = 20000;
    Decoder dec; MediaAssembler media;
    if (dec.open(c) != Status::Ok) { fprintf(stderr, "open failed\n"); return 3; }
    if (media.configure(c, 8192) != Status::Ok) { fprintf(stderr, "media configure failed\n"); return 3; }
    FILE* in = fopen(argv[4], "rb"); FILE* out = fopen(argv[5], "wb"); if (!in || !out) return 2;
    std::vector<int32_t> pcm(4096 * 2); size_t packets = 0, errors = 0, frames = 0, resyncs = 0, gapFrames = 0;
    uint16_t len;
    while (fread(&len, 2, 1, in) == 1) {
        std::vector<uint8_t> pkt(len); if (fread(pkt.data(), 1, len, in) != len) return 4; ++packets;
        MediaView view; Status st = media.feed(pkt.data(), len, view);
        if (st == Status::NeedMoreData) continue;
        if (st != Status::Ok) { ++errors; fprintf(stderr, "feed %zu status %d\n", packets, int(st)); continue; }
        size_t offset = 0; bool needMore = false; const size_t cap = 1024; // receiver-sized PCM scratch
        while (offset < view.bytes) {
            size_t input = view.bytes - offset;
            if (c.codec == Codec::Aptx || c.codec == Codec::AptxHd) { size_t w = c.codec == Codec::Aptx ? 4 : 6, b = cap / 4 * w; if (input > b) input = b; }
            DecodeResult r = dec.decode(view.data + offset, input, pcm.data(), cap);
            if (r.status == Status::NeedMoreData && !r.consumed) { needMore = true; break; }
            if (r.status != Status::Ok || !r.consumed || r.consumed > input || r.frames > cap) {
                ++errors; fprintf(stderr, "decode packet %zu status %d\n", packets, int(r.status)); break; }
            if (r.concealment_flags & ConcealDiscontinuity) { ++resyncs; gapFrames += r.gap_frames; }
            fwrite(pcm.data(), 4, r.frames * 2, out); frames += r.frames; offset += r.consumed;
        }
        if (needMore && offset && c.codec == Codec::Aac) { if (!media.retainDecodedSuffix(view, offset)) media.consumed(); }
        else if (!needMore) media.consumed();
    }
    fclose(in); fclose(out); dec.close(); media.close();
    auto m = memoryStats();
    printf("packets=%zu frames=%zu errors=%zu resyncs=%zu gap_frames=%zu leaked_bytes=%zu\n", packets, frames, errors, resyncs, gapFrames, size_t(m.current_bytes));
    return errors ? 5 : 0;
}
