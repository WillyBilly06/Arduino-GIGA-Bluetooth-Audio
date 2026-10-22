#include "GigaBredr.h"
#include "detail/Memory.h"
#include "detail/SbcConfig.h"
#include "detail/Decoder.h"
#include "detail/Media.h"
#include "detail/PacketQueue.h"
#include "detail/Volume.h"
#include "detail/AudioClockControl.h"
#include "codecs/Codecs.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include "platform/I2sPins.h"
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
// Foreground BLE data calls never wait for the radio actor's ownership lock.
// Lifecycle and snapshot access retain their original synchronization.
struct TryLock {
    Mutex& mutex; bool held;
    explicit TryLock(Mutex& m):mutex(m),held(false) {
#if defined(ARDUINO)
        held=mutex.trylock();
#else
        held=mutex.try_lock();
#endif
    }
    ~TryLock(){if(held)mutex.unlock();}
    explicit operator bool()const{return held;}
    TryLock(const TryLock&)=delete;
    TryLock& operator=(const TryLock&)=delete;
};
// The outer Lock owns this mutex. Reacquire before that lock can leave scope.
// Used only around the backpressure sleep, never decode or buffer access.
struct UnlockDuringWait {
    Mutex& mutex;
    explicit UnlockDuringWait(Mutex& m):mutex(m) { mutex.unlock(); }
    ~UnlockDuringWait() { mutex.lock(); }
    UnlockDuringWait(const UnlockDuringWait&)=delete;
    UnlockDuringWait& operator=(const UnlockDuringWait&)=delete;
};
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
        case Codec::Sbc: {
            detail::SbcGeometry geometry;
            return detail::sbcGeometry(c,geometry)?geometry.frames():0;
        }
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
    // All mutable control/bookkeeping belongs to codec_mutex's decoder worker.
    detail::AudioClockControl clock_control;
    AudioClockStats clock_published; // Public row; live bookkeeping stays worker-private.
    uint32_t clock_output_epoch = 0, clock_last_us = 0;
    uint32_t clock_whole_units = 0, clock_remaining_units = 0;
    uint32_t clock_previous_decode_errors = 0, clock_previous_dropped = 0;
    uint64_t clock_previous_discarded = 0, clock_packet_start = 0, clock_rtp_errors[9] = {};
    bool clock_have_time = false, clock_packet_active = false, clock_input_fault = false, clock_local_backend_fault = false;
    bool clock_eligible = false; // Epoch geometry only; never changes requested enable.
    bool clockEnabled() const { return options.clock.enabled && options.enable_i2s; }
    void clockBegin() {
        clock_control = {}; clock_output_epoch = 0; clock_have_time = clock_packet_active = clock_input_fault = false; clock_eligible = false;
        clock_whole_units = clock_remaining_units = 0; clock_packet_start = 0; clock_local_backend_fault = false;
        clock_previous_decode_errors = counters.decode_errors; clock_previous_dropped = dropped.load(); clock_previous_discarded = 0;
        memset(clock_rtp_errors, 0, sizeof clock_rtp_errors);
        counters.clock = {}; counters.clock.enabled = clockEnabled();
        counters.clock.state = clockEnabled() ? AudioClockState::Inactive : AudioClockState::Disabled;
        counters.clock.observation_us = clockUs(); clock_published = counters.clock;
        if (!clockEnabled()) return;
        auto& stats = counters.clock; stats.stream_epoch = stream_epoch; stats.sample_rate = format.sample_rate;
        const auto source = detail::i2sSourceSnapshot();
        stats.ring_capacity = source.ring_capacity;
        // Match the actual I2S geometry: ceil(rate/1000), rounded to four frames.
        // A nominal millisecond threshold is wrong at44.1/88.2/176.4kHz.
        const uint32_t half = uint32_t(((uint64_t(format.sample_rate) + 999u) / 1000u + 3u) & ~uint64_t(3u));
        stats.required_ring_frames = half * 4u;
        stats.observation_us = clockUs(); clock_published = stats;
        if (!source.initialized || !source.output_epoch || source.sample_rate != format.sample_rate || !source.ring_capacity) return;
        clock_output_epoch = source.output_epoch; stats.output_epoch = stats.expected_output_epoch = source.output_epoch;
        stats.prepared_frames = source.prepared; stats.missing_frames = source.missing;
        stats.inventory_valid = source.prepared == 0;
        detail::AudioClockConfig config; config.limit_ppm = options.clock.limit_ppm; config.slew_ppm = options.clock.slew_ppm_per_second;
        if (source.ring_capacity >= stats.required_ring_frames)
            clock_eligible = clock_control.begin(stream_epoch, source.output_epoch, format.sample_rate, source.ring_capacity, active_buffers.queue_packets, config);
        stats.eligible = clock_eligible;
        stats.observation_us = clockUs(); clock_published = stats;
    }
    void clockStop() {
        if (clockEnabled() && clock_output_epoch) {
            const auto source = detail::i2sSourceSnapshot(); auto& stats = counters.clock;
            stats.output_epoch = source.output_epoch; stats.prepared_frames = source.prepared; stats.missing_frames = source.missing;
            stats.inventory_valid = source.output_epoch == clock_output_epoch && stats.discarded_frames <= stats.decoded_frames &&
                                    stats.prepared_frames <= stats.decoded_frames - stats.discarded_frames;
            // Releasing the ring/unfinished PCM explicitly discards its real inventory.
            if (stats.inventory_valid) stats.discarded_frames = stats.decoded_frames - stats.prepared_frames;
        }
        clock_control = {}; clock_output_epoch = 0; clock_have_time = clock_packet_active = false; clock_eligible = false;
        clock_whole_units = clock_remaining_units = 0; counters.clock.retained_units_q16 = counters.clock.compressed_units_q16 = 0;
        counters.clock.state = clockEnabled() ? AudioClockState::Inactive : AudioClockState::Disabled;
        counters.clock.observation_us = clockUs(); clock_published = counters.clock;
    }
    void clockStartPacket(detail::MediaRetainedState before, detail::MediaRetainedState after) {
        if (!clockEnabled()) return;
        auto& stats = counters.clock;
        if (stats.retained_units_q16 && before.assembly_generation != after.assembly_generation) {
            stats.retained_units_q16 = 0; ++stats.assembly_discards; clock_input_fault = true;
        }
        // The access-unit capacity bounds retained entries to at most65535 byte packets.
        if (stats.retained_units_q16 > UINT32_MAX - 65536u) { stats.retained_units_q16 = 0; clock_input_fault = true; }
        clock_whole_units = stats.retained_units_q16 + 65536u;
        clock_remaining_units = clock_whole_units; stats.retained_units_q16 = 0;
        clock_packet_start = stats.decoded_frames; clock_packet_active = true;
    }
    void clockDecoded(size_t consumed, size_t total, size_t frames) {
        if (!clockEnabled()) return;
        if (!total || consumed > total) { clock_input_fault = true; clock_remaining_units = 0; }
        else clock_remaining_units = uint32_t(uint64_t(clock_whole_units) * (total - consumed) / total);
        counters.clock.decoded_frames += frames; // Credit before any PCM is prepared.
    }
    void clockDiscard(size_t frames) { if (clockEnabled()) counters.clock.discarded_frames += frames; }
    void clockFinishPacket(bool valid, bool retained) {
        if (!clockEnabled() || !clock_packet_active) return;
        auto& stats = counters.clock;
        if (valid) { ++stats.completed_packets; stats.completed_frames += stats.decoded_frames - clock_packet_start; }
        else clock_input_fault = true;
        stats.retained_units_q16 = valid && retained ? clock_remaining_units : 0;
        clock_whole_units = clock_remaining_units = 0; clock_packet_active = false;
    }
    static AudioClockBackendStatus clockBackendStatus(detail::ClockTrimCode code) {
        switch (code) {
            case detail::ClockTrimCode::Applied: return AudioClockBackendStatus::Applied;
            case detail::ClockTrimCode::Unchanged: return AudioClockBackendStatus::Unchanged;
            case detail::ClockTrimCode::Inactive: return AudioClockBackendStatus::Inactive;
            case detail::ClockTrimCode::StaleEpoch: return AudioClockBackendStatus::StaleEpoch;
            case detail::ClockTrimCode::InvalidArgument: return AudioClockBackendStatus::InvalidArgument;
            case detail::ClockTrimCode::Unrepresentable: return AudioClockBackendStatus::Unrepresentable;
            case detail::ClockTrimCode::Unsupported: return AudioClockBackendStatus::Unsupported;
            case detail::ClockTrimCode::ForeignClock: return AudioClockBackendStatus::ForeignClock;
            case detail::ClockTrimCode::TimerUnavailable: return AudioClockBackendStatus::TimerUnavailable;
            case detail::ClockTrimCode::TimerStalled: return AudioClockBackendStatus::TimerStalled;
            case detail::ClockTrimCode::ReadbackFault: return AudioClockBackendStatus::ReadbackFault;
            case detail::ClockTrimCode::InvalidContext: return AudioClockBackendStatus::InvalidContext;
        }
        return AudioClockBackendStatus::Unsupported;
    }
    void clockFeedback() {
        if (!clockEnabled() || !clock_eligible || !running.load() || !configured || !playing.load() || !clock_output_epoch) return;
        const uint32_t now = clockUs();
        // Gate BEFORE backend IRQ exclusion, queue lock and occupancy arithmetic.
        if (clock_have_time && uint32_t(now - clock_last_us) < 1000u) return;
        clock_have_time = true; clock_last_us = now;
        auto& stats = counters.clock; ++stats.observations; stats.observation_us = now;
        const auto source = detail::i2sSourceSnapshot();
        const auto backend = detail::i2sClockTrimStats();
        uint32_t queued; bool rtp_changed = false;
        { Lock queue_lock(queue_mutex); queued = uint32_t(queue.count());
          const auto rtp = rtp_timing.snapshot();
          const uint64_t errors[] = {rtp.forward_gaps,rtp.duplicates,rtp.backward,rtp.source_changes,rtp.clock_discontinuities,
                                    rtp.timestamp_discontinuities,rtp.inner_sequence_discontinuities,rtp.malformed_payloads,rtp.rejected_epoch};
          for (unsigned i = 0; i < 9; ++i) { if (errors[i] != clock_rtp_errors[i]) rtp_changed = true; clock_rtp_errors[i] = errors[i]; }
        }
        const uint32_t dropped_now = dropped.load();
        const bool new_error = counters.decode_errors != clock_previous_decode_errors || dropped_now != clock_previous_dropped ||
                               stats.discarded_frames != clock_previous_discarded || rtp_changed || clock_input_fault;
        clock_previous_decode_errors = counters.decode_errors; clock_previous_dropped = dropped_now;
        clock_previous_discarded = stats.discarded_frames; clock_input_fault = false;
        stats.output_epoch = source.output_epoch;
        stats.prepared_frames = source.prepared; stats.missing_frames = source.missing;
        stats.inventory_valid = source.output_epoch == clock_output_epoch && stats.discarded_frames <= stats.decoded_frames && source.prepared <= stats.decoded_frames - stats.discarded_frames;
        const uint64_t units = uint64_t(queued) * 65536u + (clock_packet_active ? clock_remaining_units : stats.retained_units_q16);
        stats.compressed_units_q16 = units > UINT32_MAX ? UINT32_MAX : uint32_t(units);
        detail::AudioClockSample sample;
        sample.stream_epoch = stream_epoch; sample.output_epoch = source.output_epoch; sample.now_us = now;
        sample.decoded = stats.decoded_frames; sample.discarded = stats.discarded_frames; sample.prepared = source.prepared; sample.zeros = source.missing;
        sample.completed_packets = stats.completed_packets; sample.completed_frames = stats.completed_frames;
        sample.compressed_units_q16 = stats.compressed_units_q16;
        sample.running = source.initialized && source.running; sample.recovering = source.recovering;
        sample.healthy = !new_error && !backend.faulted && !clock_local_backend_fault && source.output_epoch == clock_output_epoch && source.sample_rate == format.sample_rate && units <= UINT32_MAX;
        const auto decision = clock_control.observe(sample);
        if (decision.update) {
            stats.requested_ppm_q16 = decision.requested_q16;
            const auto result = detail::i2sClockTrim(decision.output_epoch, decision.requested_q16, decision.lower_q16, decision.upper_q16);
            const bool correct_epoch = result.output_epoch == decision.output_epoch;
            bool accepted = result.accepted && correct_epoch;
            const bool acknowledged = clock_control.acknowledge(decision, accepted, result.applied_q16);
            if (!acknowledged) { clock_control.acknowledge(decision, false, 0); clock_local_backend_fault = true; accepted = false; }
            if (result.accepted && !correct_epoch) clock_local_backend_fault = true;
            if (result.partial && !accepted) clock_local_backend_fault = true;
            stats.backend_status = !acknowledged ? AudioClockBackendStatus::ReadbackFault :
                                   result.accepted && !correct_epoch ? AudioClockBackendStatus::StaleEpoch : clockBackendStatus(result.status);
            stats.backend_fault = detail::i2sClockTrimStats().faulted || clock_local_backend_fault;
            stats.duration_cycles = result.duration_cycles; stats.wait_cycles[0] = result.wait_cycles[0]; stats.wait_cycles[1] = result.wait_cycles[1];
            if (result.max_critical_cycles > stats.maximum_critical_cycles) stats.maximum_critical_cycles = result.max_critical_cycles;
            stats.duration_valid = result.duration_valid; stats.wall_overruns = result.wall_overruns;
            stats.fraction = result.fraction; stats.readback_fraction = result.readback_fraction;
            if (accepted) stats.applied_valid = true;
        } else stats.backend_fault = backend.faulted || clock_local_backend_fault;
        if (stats.backend_fault || source.output_epoch != clock_output_epoch) stats.applied_valid = false;
        const auto control = clock_control.snapshot();
        stats.applied_ppm_q16 = control.applied_q16; stats.decisions = control.decisions; stats.accepted = control.accepted; stats.refused = control.refused;
        stats.invalid_samples = control.invalid_samples; stats.estimated_inventory_frames = control.inventory_frames;
        stats.target_frames = control.target_frames; stats.frames_per_packet_q16 = control.frames_per_packet_q16;
        stats.state = stats.backend_fault ? AudioClockState::BackendFault : control.tracking ? AudioClockState::Tracking :
                      control.learning ? AudioClockState::Learning : control.suspended ? AudioClockState::Suspended : AudioClockState::Inactive;
        clock_published = stats;
    }

    explicit Impl(const ReceiverConfig& config): options(config) {
        counters.clock.enabled = clockEnabled();
        counters.clock.state = clockEnabled() ? AudioClockState::Inactive : AudioClockState::Disabled;
        counters.clock.observation_us = clockUs(); clock_published = counters.clock;
    }
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
        clockStop();
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
            status = detail::i2sBegin(c.sample_rate, selected_buffers.pcm_buffer_ms, options.i2s); output_owned = status == Status::Ok;
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
        clockBegin();
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
        clockFeedback();
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
        const uint32_t packet_epoch = stream_epoch;
        detail::PacketWorkSample work;
        const uint32_t packet_began = clockUs();
        auto record_work = [&]() { work.elapsed_us = clockUs() - packet_began; work_timing.record(packet_epoch, work); };
        detail::MediaView view;
        const auto retained_before = media.retainedState();
        Status status = media.feed(packet, n, view);
        clockStartPacket(retained_before, media.retainedState());
        if (status == Status::NeedMoreData) { clockFinishPacket(true, true); clockFeedback(); record_work(); return; }
        if (status != Status::Ok) { ++counters.decode_errors; counters.last_error = status; clockFinishPacket(false, false); clockFeedback(); record_work(); return; }
        size_t offset = 0; bool need_more = false, valid_packet = true;
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
                ++counters.decode_errors; counters.last_error = result.status == Status::Ok ? Status::CorruptFrame : result.status; valid_packet = false; break;
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
            clockDecoded(offset + result.consumed, view.bytes, result.frames);
            clockFeedback();
            if (options.apply_remote_volume) detail::scaleStereoVolume(pcm, result.frames, volume.load());
            if (options.enable_i2s) {
                const uint64_t write_budget = 100000u +
                    (uint64_t(result.frames) * 1000000u + format.sample_rate - 1u) / format.sample_rate;
                const uint32_t write_limit_us = write_budget < 0x80000000u ? uint32_t(write_budget) : 0x7fffffffu;
                size_t sent = 0; uint32_t began = clockUs();
                while (sent < result.frames) {
                    sent += detail::i2sWrite(pcm + sent * 2, result.frames - sent);
                    OutputStats output = detail::i2sStats();
                    if (!output.initialized || (!output.running && !output.priming)) {
                        counters.output_dropped_frames += uint32_t(result.frames - sent); clockDiscard(result.frames - sent);
                        counters.last_error = Status::HardwareError;
                        note("I2S stopped; output and decoder released");
                        record_work(); releaseCodec(); return;
                    }
                    if (sent == result.frames) break;
                    if (!running.load() || clockUs() - began > write_limit_us) { counters.output_dropped_frames += uint32_t(result.frames - sent); clockDiscard(result.frames - sent); break; }
                    uint32_t waiting = clockUs();
                    clockFeedback();
                    { UnlockDuringWait unlock(codec_mutex); waitOne(); }
                    work.write_wait_us += clockUs() - waiting;
                    // Stop/reconfigure can release every saved packet/PCM view
                    // during the wait. Validate before any pointer is reused.
                    if (packet_epoch != stream_epoch || !configured || !playing.load()) return;
                    clockFeedback();
                    if (!running.load()) {
                        counters.output_dropped_frames += uint32_t(result.frames - sent); clockDiscard(result.frames - sent);
                        record_work(); return;
                    }
                }
            }
            offset += result.consumed;
        }
        if (!need_more) media.consumed();
        if (offset == view.bytes) ++counters.decoded_packets;
        clockFinishPacket(valid_packet, need_more); clockFeedback();
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
    if (input.clock.limit_ppm > 500 || input.clock.slew_ppm_per_second > 10) return Status::InvalidArgument;
    detail::I2sRoutePlan route;
    if (input.enable_i2s && !detail::i2sRoutePlan(input.i2s, route)) return Status::InvalidArgument;
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
        result = impl_->counters; result.clock = impl_->clock_published; result.configuration = impl_->format;
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
    TryLock lock(impl_->radio_mutex);
    if (!lock) return Status::Busy;
    return detail::radioBleNotify(data, length);
}
Status GigaBredr::bleNotify(const BleWrite& origin) {
    if (!impl_) return Status::Unsupported;
    TryLock lock(impl_->radio_mutex);
    if (!lock) return Status::Busy;
    const BleStats peer = detail::radioBleStats();
    if (!origin.generation || !peer.connected || peer.generation != origin.generation)
        return Status::InvalidArgument;
    return detail::radioBleNotify(origin.data, origin.length);
}
bool GigaBredr::bleReadWrite(BleWrite& value) {
    if (!impl_) return false;
    TryLock lock(impl_->radio_mutex);
    if (!lock) return false;
    return detail::radioBleReadWrite(value);
}
Status GigaBredr::bleDisconnect(uint32_t expected,bool advertise_after) {
    if (!impl_) return Status::Unsupported;
    TryLock lock(impl_->radio_mutex);
    if (!lock) return Status::Busy;
    return detail::radioBleDisconnect(expected,advertise_after);
}
Status GigaBredr::bleResumeAdvertising() {
    if (!impl_) return Status::Unsupported;
    TryLock lock(impl_->radio_mutex);
    if (!lock) return Status::Busy;
    return detail::radioBleResumeAdvertising();
}
}
