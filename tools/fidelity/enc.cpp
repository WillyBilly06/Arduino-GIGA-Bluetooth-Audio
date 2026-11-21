// Reference side (links the VM's system codec libraries, i.e. what PipeWire uses):
// generate a known stereo signal, encode it, packetize as A2DP media packets,
// and decode with the reference decoder where one exists.
// Output files: <tag>.src (int32 stereo, left-justified), <tag>.pkt (u16 length + packet),
// <tag>.ref (int32 stereo, reference-decoded; absent where no reference decoder).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
extern "C" {
#include <sbc/sbc.h>
#include <freeaptx.h>
#include <ldac/ldacBT.h>
#include <opus/opus.h>
#include <fdk-aac/aacenc_lib.h>
#include <fdk-aac/aacdecoder_lib.h>
}
typedef std::vector<int32_t> Pcm;
static uint32_t lcg = 12345;
static double noise() { lcg = lcg * 1664525u + 1013904223u; return (int32_t(lcg) / 2147483648.0); }
// 10 s: per-channel distinct tone sets plus band-limited noise, peak about -6 dBFS.
static Pcm signal(unsigned rate, unsigned seconds) {
    size_t n = size_t(rate) * seconds; Pcm p(n * 2);
    const double fl[] = {220.0, 1000.0, 5300.0, 12100.0}, fr[] = {330.0, 1800.0, 7700.0, 15500.0};
    double lp[2] = {0, 0};
    for (size_t i = 0; i < n; ++i) {
        double t = double(i) / rate;
        for (int c = 0; c < 2; ++c) {
            const double* f = c ? fr : fl; double v = 0;
            for (int k = 0; k < 4; ++k) v += 0.09 * sin(2 * M_PI * f[k] * t + k);
            lp[c] += 0.3 * (noise() - lp[c]);           // one-pole low-passed noise
            if (!getenv("TONES")) v += 0.12 * lp[c];
            double env = 0.75 + 0.25 * sin(2 * M_PI * 0.5 * t + c); // slow level movement
            p[2 * i + c] = int32_t(lrint(v * env * 2147483647.0));
        }
    }
    return p;
}
static void put(FILE* f, const void* d, size_t n) { if (fwrite(d, 1, n, f) != n) { perror("write"); exit(2); } }
static void writePcm(const std::string& path, const Pcm& p) { FILE* f = fopen(path.c_str(), "wb"); put(f, p.data(), p.size() * 4); fclose(f); }
struct Packets {
    FILE* f; uint16_t seq = 1; uint32_t ts = 0; size_t count = 0;
    explicit Packets(const std::string& path) { f = fopen(path.c_str(), "wb"); }
    ~Packets() { fclose(f); }
    void raw(const uint8_t* d, size_t n) { uint16_t len = uint16_t(n); put(f, &len, 2); put(f, d, n); ++count; }
    // RTP v2, PT 96, then optional one-byte A2DP media header, then payload.
    void rtp(int header, const uint8_t* d, size_t n, uint32_t frames) {
        std::vector<uint8_t> b(12 + (header >= 0) + n);
        b[0] = 0x80; b[1] = 96; b[2] = seq >> 8; b[3] = seq & 255;
        b[4] = ts >> 24; b[5] = ts >> 16; b[6] = ts >> 8; b[7] = ts; b[8] = 0x12; b[9] = 0x34; b[10] = 0x56; b[11] = 0x78;
        size_t o = 12; if (header >= 0) b[o++] = uint8_t(header);
        memcpy(&b[o], d, n); raw(b.data(), b.size()); ++seq; ts += frames;
    }
};
static int16_t s16(int32_t v) { return int16_t(v >> 16); }

static int doSbc(const std::string& tag, bool xq) {
    Pcm src = signal(48000, 10); writePcm(tag + ".src", src);
    sbc_t enc; sbc_init(&enc, 0);
    enc.frequency = SBC_FREQ_48000; enc.blocks = SBC_BLK_16; enc.subbands = SBC_SB_8;
    enc.mode = xq ? SBC_MODE_DUAL_CHANNEL : SBC_MODE_JOINT_STEREO; enc.allocation = SBC_AM_LOUDNESS;
    enc.bitpool = xq ? 47 : 53; enc.endian = SBC_LE;
    sbc_t dec; sbc_init(&dec, 0); dec.endian = SBC_LE;
    Packets out(tag + ".pkt"); Pcm ref;
    size_t frames = src.size() / 2, per = 128; std::vector<uint8_t> pkt; int inPkt = 0;
    for (size_t i = 0; i + per <= frames; i += per) {
        int16_t in[256]; for (size_t k = 0; k < 256; ++k) in[k] = s16(src[2 * i + k]);
        uint8_t frame[512]; ssize_t written = 0;
        ssize_t used = sbc_encode(&enc, in, sizeof in, frame, sizeof frame, &written);
        if (used != ssize_t(sizeof in) || written <= 0) { fprintf(stderr, "sbc_encode %zd %zd\n", used, written); return 1; }
        int16_t o[256]; size_t ow = 0;
        if (sbc_decode(&dec, frame, written, o, sizeof o, &ow) != written) { fprintf(stderr, "sbc_decode\n"); return 1; }
        for (size_t k = 0; k < ow / 2; ++k) ref.push_back(int32_t(o[k]) * 65536);
        pkt.insert(pkt.end(), frame, frame + written);
        if (++inPkt == 5) { out.rtp(inPkt, pkt.data(), pkt.size(), uint32_t(per * inPkt)); pkt.clear(); inPkt = 0; }
    }
    writePcm(tag + ".ref", ref); printf("%s packets=%zu frame_bytes=%zu\n", tag.c_str(), out.count, size_t(sbc_get_frame_length(&enc)));
    return 0;
}
static int doAptx(const std::string& tag, bool hd) {
    Pcm src = signal(48000, 10); writePcm(tag + ".src", src);
    aptx_context* e = aptx_init(hd); aptx_context* d = aptx_init(hd);
    size_t frames = src.size() / 2; std::vector<uint8_t> in(frames * 6);
    for (size_t i = 0; i < frames * 2; ++i) { int32_t v = src[i] >> 8; in[3 * i] = v; in[3 * i + 1] = v >> 8; in[3 * i + 2] = v >> 16; }
    std::vector<uint8_t> coded(frames * 2); size_t written = 0;
    size_t used = aptx_encode(e, in.data(), in.size(), coded.data(), coded.size(), &written);
    if (used != in.size()) { fprintf(stderr, "aptx_encode used %zu/%zu\n", used, in.size()); return 1; }
    coded.resize(written);
    std::vector<uint8_t> dec(frames * 6 + 64); size_t dw = 0;
    aptx_decode(d, coded.data(), coded.size(), dec.data(), dec.size(), &dw);
    Pcm ref(dw / 3);
    for (size_t i = 0; i < ref.size(); ++i) { uint32_t v = dec[3 * i] | (dec[3 * i + 1] << 8) | (uint32_t(dec[3 * i + 2]) << 16); ref[i] = int32_t(v << 8); }
    writePcm(tag + ".ref", ref);
    Packets out(tag + ".pkt"); const size_t chunk = hd ? 600 : 600; const size_t block = hd ? 6 : 4;
    for (size_t o = 0; o + chunk <= coded.size(); o += chunk) {
        if (hd) out.rtp(-1, &coded[o], chunk, uint32_t(chunk / block * 4)); else out.raw(&coded[o], chunk);
    }
    printf("%s packets=%zu coded_bytes=%zu\n", tag.c_str(), out.count, coded.size());
    aptx_finish(e); aptx_finish(d); return 0;
}
static int doLdac(const std::string& tag, int eqmid) {
    const unsigned rate = 96000; Pcm src = signal(rate, 10); writePcm(tag + ".src", src);
    HANDLE_LDAC_BT h = ldacBT_get_handle();
    if (ldacBT_init_handle_encode(h, 679, eqmid, LDACBT_CHANNEL_MODE_STEREO, LDACBT_SMPL_FMT_S32, rate)) { fprintf(stderr, "ldac init %d\n", ldacBT_get_error_code(h)); return 1; }
    Packets out(tag + ".pkt"); size_t frames = src.size() / 2, pos = 0; uint32_t ts = 0; (void)ts;
    const size_t lsu = 128;
    while (true) {
        uint8_t stream[1024]; int used = 0, ssz = 0, fn = 0;
        int r;
        if (pos + lsu <= frames) r = ldacBT_encode(h, &src[2 * pos], &used, stream, &ssz, &fn);
        else r = ldacBT_encode(h, nullptr, &used, stream, &ssz, &fn);
        if (r) { if (pos + lsu > frames) break; fprintf(stderr, "ldac encode %d\n", ldacBT_get_error_code(h)); return 1; }
        if (pos + lsu <= frames) pos += size_t(used) / 8; else if (!ssz) break;
        if (ssz) out.rtp(fn & 15, stream, size_t(ssz), uint32_t(fn * 256));
        if (pos + lsu > frames && !ssz) break;
    }
    ldacBT_free_handle(h); printf("%s packets=%zu\n", tag.c_str(), out.count); return 0;
}
static int doOpus(const std::string& tag) {
    Pcm src = signal(48000, 10); writePcm(tag + ".src", src);
    int err = 0; OpusEncoder* e = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
    opus_encoder_ctl(e, OPUS_SET_BITRATE(256000)); opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(10));
    OpusDecoder* d = opus_decoder_create(48000, 2, &err);
    Packets out(tag + ".pkt"); Pcm ref; const size_t per = 960; size_t frames = src.size() / 2;
    for (size_t i = 0; i + per <= frames; i += per) {
        int16_t in[960 * 2]; for (size_t k = 0; k < per * 2; ++k) in[k] = s16(src[2 * i + k]);
        uint8_t pkt[1500]; int n = opus_encode(e, in, per, pkt, sizeof pkt);
        if (n <= 0) { fprintf(stderr, "opus_encode %d\n", n); return 1; }
        int16_t o[960 * 2]; int got = opus_decode(d, pkt, n, o, per, 0);
        for (int k = 0; k < got * 2; ++k) ref.push_back(int32_t(o[k]) * 65536);
        out.rtp(1, pkt, size_t(n), per);
    }
    writePcm(tag + ".ref", ref); printf("%s packets=%zu\n", tag.c_str(), out.count);
    opus_encoder_destroy(e); opus_decoder_destroy(d); return 0;
}
static int doAac(const std::string& tag) {
    Pcm src = signal(48000, 10); writePcm(tag + ".src", src);
    HANDLE_AACENCODER e; if (aacEncOpen(&e, 0, 2) != AACENC_OK) return 1;
    aacEncoder_SetParam(e, AACENC_AOT, AOT_AAC_LC); aacEncoder_SetParam(e, AACENC_SAMPLERATE, 48000);
    aacEncoder_SetParam(e, AACENC_CHANNELMODE, MODE_2); aacEncoder_SetParam(e, AACENC_BITRATE, 256000);
    aacEncoder_SetParam(e, AACENC_TRANSMUX, TT_MP4_LATM_MCP1); aacEncoder_SetParam(e, AACENC_HEADER_PERIOD, 1);
    aacEncoder_SetParam(e, AACENC_AFTERBURNER, 1);
    if (aacEncEncode(e, nullptr, nullptr, nullptr, nullptr) != AACENC_OK) { fprintf(stderr, "aac init\n"); return 1; }
    HANDLE_AACDECODER d = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);
    Packets out(tag + ".pkt"); Pcm ref; const size_t per = 1024; size_t frames = src.size() / 2;
    for (size_t i = 0; i + per <= frames + 4 * per; i += per) {
        int16_t in[2048] = {}; bool flush = i + per > frames;
        if (!flush) for (size_t k = 0; k < 2048; ++k) in[k] = s16(src[2 * i + k]);
        uint8_t ob[4096];
        void* ib[] = {in}; INT ibid[] = {IN_AUDIO_DATA}, ibs[] = {INT(sizeof in)}, iel[] = {2};
        void* obp[] = {ob}; INT obid[] = {OUT_BITSTREAM_DATA}, obs[] = {INT(sizeof ob)}, oel[] = {1};
        AACENC_BufDesc inD = {1, ib, ibid, ibs, iel}, outD = {1, obp, obid, obs, oel};
        AACENC_InArgs ia = {}; ia.numInSamples = flush ? -1 : 2048; AACENC_OutArgs oa = {};
        AACENC_ERROR r = aacEncEncode(e, &inD, &outD, &ia, &oa);
        if (r == AACENC_ENCODE_EOF) break; if (r != AACENC_OK) { fprintf(stderr, "aacEncEncode %d\n", r); return 1; }
        if (!oa.numOutBytes) continue;
        out.rtp(-1, ob, size_t(oa.numOutBytes), per);
        UCHAR* p = ob; UINT sz = oa.numOutBytes, valid = sz; aacDecoder_Fill(d, &p, &sz, &valid);
        INT_PCM o[2048 * 2]; if (aacDecoder_DecodeFrame(d, o, 2048 * 2, 0) == AAC_DEC_OK) {
            CStreamInfo* si = aacDecoder_GetStreamInfo(d);
            for (int k = 0; k < si->frameSize * si->numChannels; ++k) ref.push_back(int32_t(o[k]) * 65536);
        }
    }
    writePcm(tag + ".ref", ref); printf("%s packets=%zu\n", tag.c_str(), out.count);
    aacEncClose(&e); aacDecoder_Close(d); return 0;
}
int main(int argc, char** argv) {
    if (argc != 2) return 1; std::string c = argv[1];
    if (c == "sbc") return doSbc(c, false); if (c == "sbcxq") return doSbc(c, true);
    if (c == "aptx") return doAptx(c, false); if (c == "aptxhd") return doAptx(c, true);
    if (c == "ldac990") return doLdac(c, LDACBT_EQMID_HQ); if (c == "ldac660") return doLdac(c, LDACBT_EQMID_SQ);
    if (c == "opus") return doOpus(c); if (c == "aac") return doAac(c);
    return 1;
}
