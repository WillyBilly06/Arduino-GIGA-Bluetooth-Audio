#include "GigaBredr.h"
#include "detail/Memory.h"
#include "detail/Decoder.h"
#include "detail/Media.h"
#include "detail/PacketQueue.h"
#include "detail/Volume.h"
#include "codecs/Codecs.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include <atomic>
#include <new>
#include <string.h>
#if defined(ARDUINO)
#include <Arduino.h>
#include <mbed.h>
#else
#include <mutex>
#include <chrono>
#endif

namespace giga_bredr {
namespace {
std::atomic<const void*> receiver_owner{nullptr};
#if defined(ARDUINO)
using Mutex = rtos::Mutex;
struct Lock { Mutex& mutex; explicit Lock(Mutex& m):mutex(m) { mutex.lock(); } ~Lock() { mutex.unlock(); } };
uint32_t clockUs() { return micros(); }
void waitOne() { rtos::ThisThread::sleep_for(1); }
#else
using Mutex = std::mutex;
using Lock = std::lock_guard<Mutex>;
uint32_t clockUs() {
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void waitOne() {}
#endif
bool sameConfiguration(const CodecConfig& a, const CodecConfig& b) {
    return a.codec == b.codec && a.sample_rate == b.sample_rate && a.bitrate == b.bitrate &&
        a.frame_duration_us == b.frame_duration_us && a.channels == b.channels &&
        a.bits_per_sample == b.bits_per_sample && a.aac_transport == b.aac_transport &&
        a.codec_info_size == b.codec_info_size && a.codec_info_size <= sizeof(a.codec_info) &&
        memcmp(a.codec_info, b.codec_info, a.codec_info_size) == 0;
}
bool validBufferOverride(const AudioBufferConfig& b) {
    return (!b.maximum_packet_bytes || (b.maximum_packet_bytes >= 64 && b.maximum_packet_bytes <= 65535)) &&
        (!b.maximum_access_unit_bytes || (b.maximum_access_unit_bytes >= 64 && b.maximum_access_unit_bytes <= 65535)) &&
        b.queue_packets <= 64 && b.startup_packets <= 64 &&
        (!b.pcm_buffer_ms || (b.pcm_buffer_ms >= 2 && b.pcm_buffer_ms <= 100));
}
AudioBufferConfig resolveBuffers(const ReceiverConfig& options, Codec codec) {
    const auto& override = options.codec_buffers[static_cast<size_t>(codec)];
    AudioBufferConfig out;
    out.maximum_packet_bytes = override.maximum_packet_bytes ? override.maximum_packet_bytes : options.maximum_packet_bytes;
    out.maximum_access_unit_bytes = override.maximum_access_unit_bytes ? override.maximum_access_unit_bytes : options.maximum_access_unit_bytes;
    out.queue_packets = override.queue_packets ? override.queue_packets : options.queue_packets;
    out.startup_packets = override.startup_packets ? override.startup_packets : options.startup_packets;
    if (out.startup_packets > out.queue_packets) out.startup_packets = out.queue_packets;
    if (!options.enable_i2s) out.startup_packets = 1;
    out.pcm_buffer_ms = override.pcm_buffer_ms ? override.pcm_buffer_ms : options.pcm_buffer_ms;
    return out;
}
size_t frameCapacity(const CodecConfig& c) {
    switch (c.codec) {
        case Codec::Sbc: return 128;
        case Codec::Aac: return 1024;
        case Codec::AacHe: return 2048;
        case Codec::Aptx: case Codec::AptxHd: return 512;
        case Codec::Ldac: return c.sample_rate > 48000 ? 256 : 128;
        // LHDC's 44.1/48 kHz modes both use a 240-sample transform hop.
        // Its nominal 5-ms enum is not an exact wall-clock duration at44.1 kHz.
        case Codec::LhdcV5: return c.sample_rate > 48000 ? c.sample_rate / 200 : 240;
        case Codec::Opus: {
            uint32_t duration = c.frame_duration_us ? c.frame_duration_us : 20000;
            if (duration > 120000 || c.sample_rate > 192000) return 0;
            return size_t((uint64_t(c.sample_rate) * duration + 999999) / 1000000);
        }
        default: return 0;
    }
}
}
struct GigaBredr::Impl {
    ReceiverConfig options;
    Mutex codec_mutex, queue_mutex, event_mutex, radio_mutex;
    detail::Decoder decoder;
    detail::MediaAssembler media;
    detail::PacketQueue queue;
    detail::RtpTiming rtp_timing;
    detail::PacketQueueTiming queue_timing;
    detail::PacketWorkTiming work_timing;
    detail::QueueToken queue_tokens[64];
    uint32_t stream_epoch = 0;
    size_t token_read = 0, token_write = 0;
    bool uses_rtp = false;
    uint32_t rtp_lhdc_hop = 0;
    AudioBufferConfig active_buffers; // Queue-owned; publish/clear with its storage under queue_mutex.
    CodecConfig format;
    ReceiverStats counters;
    std::atomic<bool> running{false}, playing{false};
    std::atomic<uint32_t> dropped{0};
    std::atomic<uint8_t> volume{127};
    uint8_t* packet = nullptr;
    int32_t* pcm = nullptr;
    size_t pcm_frames = 0;
    bool configured = false, configuration_event = false, output_owned = false, radio_owned = false;
    bool startup_buffered = false;
    char events[8][128] = {};
    uint8_t event_kind[8] = {}, event_length[8] = {};
    uint8_t event_read = 0, event_write = 0, event_count = 0;
    bool volume_event = false;
    uint32_t event_drops = 0;
#if defined(ARDUINO)
    rtos::Thread* host_thread = nullptr;
    rtos::Thread* decoder_thread = nullptr;
    rtos::Semaphore wake{0, 1};
    rtos::EventFlags host_events;
#endif
    explicit Impl(const ReceiverConfig& config): options(config) {}
    void note(const char* text) {
        if (!text) return;
        Lock lock(event_mutex);
        if (event_count == 8) { ++event_drops; return; }
        size_t length = 0; while (length < 127 && text[length]) ++length;
        memcpy(events[event_write], text, length); events[event_write][length] = 0;
        event_kind[event_write] = 0; event_length[event_write] = uint8_t(length);
        event_write = (event_write + 1) % 8; ++event_count;
    }
    void metadata(uint8_t attribute, const char* text, size_t length) {
        if (attribute < 1 || attribute > 7 || (!text && length)) return;
        Lock lock(event_mutex);
        if (event_count == 8) { ++event_drops; return; }
        if (length > 127) length = 127;
        if (length) memcpy(events[event_write], text, length);
        events[event_write][length] = 0;
        event_kind[event_write] = attribute; event_length[event_write] = uint8_t(length);
        event_write = (event_write + 1) % 8; ++event_count;
    }
    void remoteVolume(uint8_t value) {
        if (value > 127) return;
        Lock lock(event_mutex); volume.store(value); volume_event = true;
    }
    void releaseCodec() { // caller owns codec_mutex; queued packets cannot refer to freed storage
        playing.store(false);
        { Lock lock(queue_mutex);
          rtp_timing.endEpoch(stream_epoch); queue_timing.endEpoch(stream_epoch);
          queue.close(); token_read = token_write = 0;
          active_buffers = {}; uses_rtp = false; rtp_lhdc_hop = 0;
        }
        work_timing.endEpoch(stream_epoch);
        if (output_owned) { detail::i2sEnd(); output_owned = false; }
        decoder.close(); media.close();
        detail::release(packet); detail::release(pcm);
        packet = nullptr; pcm = nullptr; pcm_frames = 0;
        counters.decoder_memory_bytes = 0; configured = false;
        counters.buffers = {};
        startup_buffered = false;
    }
    Status configure(const CodecConfig& c) {
        Lock lock(codec_mutex);
        if (configured && decoder.active() && sameConfiguration(format, c)) return Status::Ok;
        releaseCodec();
        if (!(options.codecs & codecBit(c.codec))) return counters.last_error = Status::Unsupported;
        const AudioBufferConfig selected_buffers = resolveBuffers(options, c.codec);
        size_t capacity = frameCapacity(c);
        if (!capacity || capacity > 23040 || !c.sample_rate || (c.channels != 1 && c.channels != 2))
            return counters.last_error = Status::Unsupported;
        auto kind = options.external_buffers ? MemoryKind::External : MemoryKind::Fast;
        size_t before = detail::memoryStats().current_bytes;
        Status status = decoder.open(c);
        counters.decoder_memory_bytes = detail::memoryStats().current_bytes - before;
        if (status == Status::Ok) status = media.configure(c, selected_buffers.maximum_access_unit_bytes, kind);
        if (status == Status::Ok) {
            Lock queue_lock(queue_mutex);
            status = queue.configure(selected_buffers.queue_packets, selected_buffers.maximum_packet_bytes, kind);
        }
        if (status == Status::Ok) {
            packet = static_cast<uint8_t*>(detail::allocate(selected_buffers.maximum_packet_bytes, kind));
            pcm = static_cast<int32_t*>(detail::allocate(capacity * 2 * sizeof(int32_t), MemoryKind::Fast));
            if (!packet || !pcm) status = Status::OutOfMemory;
        }
        if (status == Status::Ok && options.enable_i2s) {
            status = detail::i2sBegin(c.sample_rate, selected_buffers.pcm_buffer_ms); output_owned = status == Status::Ok;
        }
        if (status != Status::Ok) { releaseCodec(); counters.last_error = status; return status; }
        format = c; pcm_frames = capacity; configured = true; counters.last_error = Status::Ok;
        if (!++stream_epoch) ++stream_epoch;
        { Lock queue_lock(queue_mutex);
          active_buffers = selected_buffers;
          rtp_timing.beginEpoch(stream_epoch); queue_timing.beginEpoch(stream_epoch, active_buffers.queue_packets);
          uses_rtp = c.codec != Codec::Aptx; token_read = token_write = 0;
          rtp_lhdc_hop = c.codec == Codec::LhdcV5 ? (c.sample_rate > 48000 ? c.sample_rate / 200 : 240) : 0;
        }
        work_timing.beginEpoch(stream_epoch);
        counters.buffers = selected_buffers;
        configuration_event = true;
        return Status::Ok;
    }
    void stream(bool start) {
        if (start) {
            CodecConfig selected;
            { Lock lock(codec_mutex); selected = format; }
            Status status = configure(selected);
            if (status == Status::Ok) { playing.store(true); note("A2DP stream started"); }
            else note("A2DP output could not start");
        } else {
            Lock lock(codec_mutex);
            releaseCodec(); note("A2DP stream stopped; codec memory released");
        }
    }
    void receive(const uint8_t* bytes, size_t n) {
        if (!playing.load() || !bytes || !n) { ++dropped; return; }
        { Lock lock(queue_mutex);
          if (!playing.load()) { ++dropped; return; }
          const uint32_t arrival = clockUs();
          detail::RtpView rtp;
          if (uses_rtp && detail::parseRtp(bytes, n, rtp) == Status::Ok) {
              uint32_t ssrc = (uint32_t(bytes[8]) << 24) | (uint32_t(bytes[9]) << 16) | (uint32_t(bytes[10]) << 8) | bytes[11];
              detail::RtpPacketDetail payload_detail;
              if (rtp_lhdc_hop) payload_detail=detail::lhdcRtpDetail(rtp.payload,rtp.bytes,rtp_lhdc_hop);
              rtp_timing.observe(stream_epoch, ssrc, rtp.sequence, rtp.timestamp, arrival, payload_detail);
          }
          if (!queue.push(bytes, n)) {
              if (n <= active_buffers.maximum_packet_bytes && queue.count() == active_buffers.queue_packets) { detail::QueueToken ignored; queue_timing.enqueued(stream_epoch, arrival, ignored); }
              ++dropped; return;
          }
          queue_timing.enqueued(stream_epoch, arrival, queue_tokens[token_write]);
          token_write = (token_write + 1) % active_buffers.queue_packets;
        }
#if defined(ARDUINO)
        wake.release();
#endif
    }
    void processOne() {
        Lock codec_lock(codec_mutex);
        if (!playing.load() || !configured || !packet || !pcm) return;
        size_t n;
        { Lock queue_lock(queue_mutex);
          if (!startup_buffered) {
              size_t reserve = active_buffers.startup_packets;
              if (queue.count() < reserve) return;
              startup_buffered = true;
          }
          n = queue.take(packet, active_buffers.maximum_packet_bytes);
          if (n) {
              queue_timing.dequeued(queue_tokens[token_read], clockUs());
              token_read = (token_read + 1) % active_buffers.queue_packets;
          }
        }
        if (!n) return;
        detail::PacketWorkSample work;
        const uint32_t packet_began = clockUs();
        auto record_work = [&]() { work.elapsed_us = clockUs() - packet_began; work_timing.record(stream_epoch, work); };
        detail::MediaView view;
        Status status = media.feed(packet, n, view);
        if (status == Status::NeedMoreData) { record_work(); return; }
        if (status != Status::Ok) { ++counters.decode_errors; counters.last_error = status; record_work(); return; }
        size_t offset = 0; bool need_more = false;
        while (offset < view.bytes) {
            size_t input = view.bytes - offset;
            if (format.codec == Codec::Aptx || format.codec == Codec::AptxHd) {
                size_t word = format.codec == Codec::Aptx ? 4 : 6;
                size_t bound = pcm_frames / 4 * word;
                if (input > bound) input = bound;
            }
            uint32_t start = clockUs();
            DecodeResult result = decoder.decode(view.data + offset, input, pcm, pcm_frames);
            uint32_t elapsed = clockUs() - start;
            work.decode_work_us += elapsed;
            if (elapsed > counters.maximum_decode_us) counters.maximum_decode_us = elapsed;
            if (result.status == Status::NeedMoreData && !result.consumed) { need_more = true; break; }
            if (result.status != Status::Ok || !result.consumed || result.consumed > input || result.frames > pcm_frames) {
                ++counters.decode_errors; counters.last_error = result.status == Status::Ok ? Status::CorruptFrame : result.status; break;
            }
            if (format.codec == Codec::LhdcV5) {
                if (result.concealment_flags & ConcealRightChannel) {
                    ++counters.lhdc_right_concealed_blocks;
                    counters.lhdc_right_concealed_frames += result.frames;
                }
                if (result.concealment_flags & ConcealFullFrame) {
                    ++counters.lhdc_frame_muted_blocks;
                    counters.lhdc_frame_muted_frames += result.frames;
                }
            }
            if (format.channels == 1) {
                for (size_t i = result.frames; i > 0; --i) { int32_t value = pcm[i - 1]; pcm[(i - 1) * 2] = value; pcm[(i - 1) * 2 + 1] = value; }
            }
            counters.decoded_frames += result.frames;
            if (options.apply_remote_volume) detail::scaleStereoVolume(pcm, result.frames, volume.load());
            if (options.enable_i2s) {
                size_t sent = 0; uint32_t began = clockUs();
                while (sent < result.frames) {
                    sent += detail::i2sWrite(pcm + sent * 2, result.frames - sent);
                    OutputStats output = detail::i2sStats();
                    if (!output.initialized || (!output.running && !output.priming)) {
                        counters.output_dropped_frames += uint32_t(result.frames - sent);
                        counters.last_error = Status::HardwareError;
                        note("I2S stopped; output and decoder released");
                        record_work(); releaseCodec(); return;
                    }
                    if (sent == result.frames) break;
                    if (!running.load() || clockUs() - began > 100000) { counters.output_dropped_frames += uint32_t(result.frames - sent); break; }
                    uint32_t waiting = clockUs(); waitOne(); work.write_wait_us += clockUs() - waiting;
                }
            }
            offset += result.consumed;
        }
        if (!need_more) media.consumed();
        if (offset == view.bytes) ++counters.decoded_packets;
        record_work();
    }
#if defined(ARDUINO)
    static void onOwnerWake(void* p) { static_cast<Impl*>(p)->host_events.set(1); }
    void hostMain() {
        while (running.load()) {
            { Lock lock(radio_mutex); detail::radioPoll(); }
            if (!running.load()) break;
            // Atomic wait-and-clear preserves signals before/during the wait.
            // Timers and RX retain their existing one millisecond fallback.
            const uint32_t event = host_events.wait_any(1, 1, true);
            if ((event & osFlagsError) && event != osFlagsErrorTimeout)
                rtos::ThisThread::sleep_for(1);
        }
    }
    void decoderMain() {
        while (running.load()) {
            wake.try_acquire_for(10);
            for (unsigned i = 0; i < 64 && running.load(); ++i) processOne();
        }
    }
#endif
    static Status onConfigure(const CodecConfig& c, void* p) { return static_cast<Impl*>(p)->configure(c); }
    static void onMedia(const uint8_t* b, size_t n, void* p) { static_cast<Impl*>(p)->receive(b, n); }
    static void onStream(bool play, void* p) { static_cast<Impl*>(p)->stream(play); }
    static void onDiagnostic(const char* s, void* p) { static_cast<Impl*>(p)->note(s); }
    static void onVolume(uint8_t v, void* p) { static_cast<Impl*>(p)->remoteVolume(v); }
    static void onMetadata(uint8_t a, const char* text, size_t bytes, void* p) { static_cast<Impl*>(p)->metadata(a, text, bytes); }
};
uint32_t GigaBredr::availableCodecs() { return detail::compiledCodecs(); }
bool GigaBredr::setExternalAllocator(Allocate a, Free f, void* user) { return detail::setMemoryProvider(MemoryKind::External, a, f, user); }
Status GigaBredr::begin(const ReceiverConfig& input) {
    if (impl_) return Status::Busy;
    if (!input.name || !*input.name || input.maximum_packet_bytes < 64 || input.maximum_packet_bytes > 65535 ||
        input.maximum_access_unit_bytes < 64 || input.maximum_access_unit_bytes > 65535 ||
        !input.queue_packets || input.queue_packets > 64 || !input.startup_packets || input.startup_packets > 64 ||
        input.pcm_buffer_ms < 2 || input.pcm_buffer_ms > 100 || input.host_stack_bytes < 4096 || input.decoder_stack_bytes < 8192)
        return Status::InvalidArgument;
    for (const auto& profile : input.codec_buffers) if (!validBufferOverride(profile)) return Status::InvalidArgument;
    ReceiverConfig config = input;
    if (!config.codecs) config.codecs = availableCodecs();
    if (!config.codecs || (config.codecs & ~availableCodecs())) return Status::Unsupported;
    const void* expected = nullptr;
    if (!receiver_owner.compare_exchange_strong(expected, this)) return Status::Busy;
    void* storage = detail::allocate(sizeof(Impl));
    if (!storage) { receiver_owner.store(nullptr); return Status::OutOfMemory; }
    impl_ = new (storage) Impl(config); ++lifetime_; impl_->running.store(true);
    detail::RadioCallbacks callbacks;
    callbacks.configure = Impl::onConfigure; callbacks.media = Impl::onMedia;
    callbacks.stream = Impl::onStream; callbacks.diagnostic = Impl::onDiagnostic; callbacks.user = impl_;
    callbacks.volume = Impl::onVolume; callbacks.metadata = Impl::onMetadata;
#if defined(ARDUINO)
    callbacks.owner_wake = Impl::onOwnerWake;
#endif
    Status status = detail::radioBegin(config.name, config.codecs, callbacks, config.ble);
    if (status != Status::Ok) { end(); return status; }
    impl_->radio_owned = true;
#if defined(ARDUINO)
    impl_->host_thread = new (std::nothrow) rtos::Thread(osPriorityAboveNormal, config.host_stack_bytes, nullptr, "gbr-host");
    impl_->decoder_thread = new (std::nothrow) rtos::Thread(osPriorityNormal, config.decoder_stack_bytes, nullptr, "gbr-decode");
    if (!impl_->host_thread || !impl_->decoder_thread) { end(); return Status::OutOfMemory; }
    if (impl_->decoder_thread->start(mbed::callback(impl_, &Impl::decoderMain)) != osOK ||
        impl_->host_thread->start(mbed::callback(impl_, &Impl::hostMain)) != osOK) { end(); return Status::OutOfMemory; }
#endif
    return Status::Ok;
}
void GigaBredr::poll() {
    if (!impl_) return;
    const uint64_t lifetime = lifetime_;
#if !defined(ARDUINO)
    detail::radioPoll(); impl_->processOne();
#endif
    char text[128]; bool has_event = false, volume_changed = false; uint8_t kind = 0, text_bytes = 0, selected_volume = 127;
    { Lock lock(impl_->event_mutex); if (impl_->event_count) {
        memcpy(text, impl_->events[impl_->event_read], sizeof(text));
        kind = impl_->event_kind[impl_->event_read]; text_bytes = impl_->event_length[impl_->event_read];
        impl_->event_read = (impl_->event_read + 1) % 8; --impl_->event_count; has_event = true;
    }
      volume_changed = impl_->volume_event; selected_volume = impl_->volume.load(); impl_->volume_event = false;
    }
    if (has_event && kind == 0 && impl_->options.diagnostic) impl_->options.diagnostic(text, impl_->options.user);
    else if (has_event && kind && impl_->options.metadata_changed) impl_->options.metadata_changed(kind, text, text_bytes, impl_->options.user);
    if (lifetime_ != lifetime || !impl_) return; // Discard old events after callback teardown/restart.
    CodecConfig selected; bool changed = false;
    { Lock lock(impl_->codec_mutex); if (impl_->configuration_event) {
        selected = impl_->format; impl_->configuration_event = false; changed = true;
    } }
    if (changed && impl_->options.configuration_changed) impl_->options.configuration_changed(selected, impl_->options.user);
    if (lifetime_ != lifetime || !impl_) return;
    if (volume_changed && impl_->options.volume_changed) impl_->options.volume_changed(selected_volume, impl_->options.user);
}
void GigaBredr::end() {
    if (!impl_) return;
    ++lifetime_; // Invalidate an active poll before destroying its Impl.
    impl_->running.store(false);
#if defined(ARDUINO)
    impl_->host_events.set(1);
    if (impl_->host_thread) { impl_->host_thread->join(); delete impl_->host_thread; }
    impl_->wake.release();
    if (impl_->decoder_thread) { impl_->decoder_thread->join(); delete impl_->decoder_thread; }
#endif
    if (impl_->radio_owned) detail::radioEnd();
    impl_->releaseCodec();
    impl_->~Impl(); detail::release(impl_); impl_ = nullptr;
    receiver_owner.store(nullptr);
}
ReceiverStats GigaBredr::stats() const {
    ReceiverStats result;
    if (impl_) {
        Lock lock(impl_->codec_mutex);
        result = impl_->counters; result.configuration = impl_->format;
        result.configured = impl_->configured; result.playing = impl_->playing.load();
        result.dropped_packets = impl_->dropped.load();
        result.absolute_volume = impl_->volume.load();
        { Lock event_lock(impl_->event_mutex); result.diagnostic_drops = impl_->event_drops; }
    }
    if (impl_) { Lock lock(impl_->radio_mutex); result.radio = detail::radioStats(); }
    result.output = detail::i2sStats(); result.memory = detail::memoryStats();
    return result;
}
BleStats GigaBredr::bleStats() const {
    if (!impl_) return {};
    Lock lock(impl_->radio_mutex);
    return detail::radioBleStats();
}
AudioDiagnostics GigaBredr::audioDiagnostics() const {
    AudioDiagnostics out;
    if (impl_) {
        Lock codec_lock(impl_->codec_mutex);
        Lock queue_lock(impl_->queue_mutex);
        out.rtp = impl_->rtp_timing.snapshot(); out.queue = impl_->queue_timing.snapshot();
        out.work = impl_->work_timing.snapshot();
    }
    out.output = detail::i2sTiming();
    return out;
}
Status GigaBredr::bleNotify(const uint8_t* data, size_t length) {
    if (!impl_) return Status::Unsupported;
    Lock lock(impl_->radio_mutex);
    return detail::radioBleNotify(data, length);
}
Status GigaBredr::bleNotify(const BleWrite& origin) {
    if (!impl_) return Status::Unsupported;
    Lock lock(impl_->radio_mutex);
    const BleStats peer = detail::radioBleStats();
    if (!origin.generation || !peer.connected || peer.generation != origin.generation)
        return Status::InvalidArgument;
    return detail::radioBleNotify(origin.data, origin.length);
}
bool GigaBredr::bleReadWrite(BleWrite& value) {
    if (!impl_) return false;
    Lock lock(impl_->radio_mutex);
    return detail::radioBleReadWrite(value);
}
}
