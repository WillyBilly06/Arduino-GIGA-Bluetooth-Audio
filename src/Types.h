#pragma once
#include <stddef.h>
#include <stdint.h>

namespace giga_bredr {
enum class Status : uint8_t {
    Ok, InvalidArgument, Unsupported, OutOfMemory, NeedMoreData,
    CorruptFrame, OutputFull, Busy, HardwareError, FirmwareMismatch
};
enum class Codec : uint8_t { Sbc, Aac, Aptx, AptxHd, Ldac, LhdcV5, Opus, AacHe, Count };
constexpr uint32_t codecBit(Codec c) { return static_cast<uint8_t>(c) < static_cast<uint8_t>(Codec::Count) ? uint32_t(1) << static_cast<uint8_t>(c) : 0; }
constexpr uint32_t AllCodecs = (uint32_t(1) << static_cast<uint8_t>(Codec::Count)) - 1;
enum class MemoryKind : uint8_t { Fast, Dma, External };
enum class AacTransport : uint8_t { Latm, Raw, Adts };
struct CodecConfig {
    Codec codec = Codec::Sbc;
    uint32_t sample_rate = 48000;
    uint32_t bitrate = 0;
    uint32_t frame_duration_us = 0;
    uint8_t channels = 2;
    uint8_t bits_per_sample = 16;
    uint8_t codec_info_size = 0;
    uint8_t codec_info[24] = {};
    AacTransport aac_transport = AacTransport::Latm;
};
enum DecodeConcealment : uint8_t {
    ConcealRightChannel = 1u << 0,
    ConcealFullFrame = 1u << 1
};
struct DecodeResult {
    Status status; size_t consumed; size_t frames;
    // Per successful encoded block; currently supplied by LHDC only.
    uint8_t concealment_flags = 0;
};
struct MemoryStats {
    size_t current_bytes = 0, peak_bytes = 0, current_blocks = 0;
    size_t category_bytes[3] = {};
    uint32_t failures = 0;
};
struct OutputStats {
    uint32_t sample_rate = 0, underruns = 0, dma_errors = 0;
    uint64_t frames_transmitted = 0;
    size_t queued_frames = 0;
    bool running = false, initialized = false, priming = false;
};
struct RadioStats {
    uint32_t received_packets = 0, received_bytes = 0, dropped_packets = 0;
    uint32_t hci_packets = 0, hci_bytes = 0;
    uint32_t transport_errors = 0, baud_rate = 0;
    bool controller_ready = false, bredr_supported = false, connected = false, streaming = false;
    uint8_t address[6] = {};
    uint16_t manufacturer = 0, lmp_subversion = 0;
};
const char* statusName(Status status);
const char* codecName(Codec codec);
}
