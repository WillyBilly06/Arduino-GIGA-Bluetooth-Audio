#include "GigaBredr.h"
#include "platform/GigaRadio.h"
#include "platform/GigaI2s.h"
#include "detail/Memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <initializer_list>
using namespace giga_bredr;
static uint8_t observedVolume = 0, observedAttribute = 0;
static size_t observedTextBytes = 0;
static void volumeChanged(uint8_t v, void*) { observedVolume = v; }
static void metadataChanged(uint8_t attribute, const char*, size_t bytes, void*) { observedAttribute = attribute; observedTextBytes = bytes; }
namespace giga_bredr { namespace detail {
static RadioCallbacks callbacks;
static I2sPins selectedPins;
static bool failOutput = false;
static bool radioUnavailable = false;
static unsigned radioEndCalls = 0;
static bool outputStopped = false;
static bool failOutputStart = false;
static size_t written = 0;
static BleConfig selectedBle;
static unsigned notificationCalls = 0;
static uint32_t selectedRate = 0;
static uint16_t selectedPcmMs = 0;
uint32_t compiledCodecs() { return codecBit(Codec::Sbc) | codecBit(Codec::LhdcV5); }
Status radioBegin(const char*, uint32_t, const RadioCallbacks& c) { if (radioUnavailable) return Status::Busy; callbacks = c; return Status::Ok; }
Status radioBegin(const char* name, uint32_t mask, const RadioCallbacks& c, const BleConfig& ble) {
    selectedBle = ble;
    if (ble.security == BleSecurityPolicy::EncryptedPairing) return Status::Unsupported;
    return radioBegin(name, mask, c);
}
void radioPoll() {}
void radioEnd() { ++radioEndCalls; callbacks = {}; }
RadioStats radioStats() { return {}; }
BleStats radioBleStats() { BleStats s; s.enabled = selectedBle.enabled; return s; }
Status radioBleNotify(const uint8_t* p, size_t n) { ++notificationCalls; return p && n == 3 ? Status::Ok : Status::InvalidArgument; }
bool radioBleReadWrite(BleWrite& out) { out.length = 1; out.data[0] = 42; return true; }
Status i2sBegin(uint32_t) { return failOutput ? Status::HardwareError : Status::Ok; }
Status i2sBegin(uint32_t rate, uint16_t ms) { selectedRate = rate; selectedPcmMs = ms; return failOutput ? Status::HardwareError : Status::Ok; }
size_t i2sWrite(const int32_t*, size_t frames) { if (outputStopped) return 0; written += frames; if (failOutputStart) outputStopped = true; return frames; }
void i2sEnd() {}
OutputStats i2sStats() { OutputStats s; s.initialized = true; s.running = !outputStopped; return s; }
OutputTimingSnapshot i2sTiming() { return {}; }
}}
int main() {
    ReceiverConfig defaults;
    assert(defaults.maximum_packet_bytes==0 && defaults.maximum_access_unit_bytes==0 && defaults.queue_packets==0 && defaults.startup_packets==0 && defaults.startup_buffer_ms==0);
#if !defined(GIGA_BREDR_I2S_BUFFER_MS)
    assert(defaults.pcm_buffer_ms==0);
#endif
    GigaBredr receiver;
    ReceiverConfig config; config.maximum_packet_bytes = 128; config.maximum_access_unit_bytes = 128; config.queue_packets = 2; config.startup_packets = 2; // Explicit legacy packet-reserve scenarios below.
    config.i2s=I2sPins{91,47,84,15};
    {ReceiverConfig bad=config;bad.i2s.mclk=-2;assert(receiver.begin(bad)==Status::InvalidArgument && detail::memoryStats().current_bytes==0);}
    uint8_t notification[] = {1,2,3}; BleWrite bleWrite;
    assert(receiver.bleNotify(notification, sizeof(notification)) == Status::Unsupported);
    assert(!receiver.bleReadWrite(bleWrite) && !receiver.bleStats().enabled);
    config.volume_changed = volumeChanged; config.metadata_changed = metadataChanged;
    assert(receiver.begin(config) == Status::Ok);
    assert(detail::selectedBle.enabled && receiver.bleStats().enabled);
    assert(receiver.bleNotify(notification, sizeof(notification)) == Status::Ok && detail::notificationCalls == 1);
    assert(receiver.bleReadWrite(bleWrite) && bleWrite.length == 1 && bleWrite.data[0] == 42);
    { GigaBredr another; assert(another.begin(config) == Status::Busy); }
    assert(receiver.stats().decoder_memory_bytes == 0);
    assert(detail::callbacks.volume && detail::callbacks.metadata);
    detail::callbacks.volume(64, detail::callbacks.user);
    detail::callbacks.metadata(1, "example track", 13, detail::callbacks.user);
    assert(observedVolume == 0 && observedAttribute == 0);
    receiver.poll();
    assert(observedVolume == 64 && observedAttribute == 1 && observedTextBytes == 13);
    assert(receiver.stats().absolute_volume == 64);
    CodecConfig format; format.codec = Codec::Sbc; format.channels = 1; format.sample_rate = 16000;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    assert(detail::selectedPins.bclk==91 && detail::selectedPins.data==47 && detail::selectedPins.lrck==84 && detail::selectedPins.mclk==15);
    auto allocated = receiver.stats().memory.current_bytes;
    assert(receiver.stats().decoder_memory_bytes == 64);
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    assert(receiver.stats().memory.current_bytes == allocated);
    detail::callbacks.stream(true, detail::callbacks.user);
    uint8_t packet[25] = {0x80}; packet[12] = 1; packet[13] = 0x9c; packet[14] = 0x31; packet[15] = 2;
    detail::callbacks.media(packet, sizeof(packet), detail::callbacks.user);
    receiver.poll();
    // Keep a second compressed packet as the startup jitter reserve. This also
    // covers access units longer than the PCM DMA ring (e.g. AAC/SBC batches).
    assert(receiver.stats().decoded_frames == 0 && detail::written == 0);
    packet[3] = 1;
    detail::callbacks.media(packet, sizeof(packet), detail::callbacks.user);
    uint8_t oversize[129] = {};
    detail::callbacks.media(oversize, sizeof(oversize), detail::callbacks.user);
    assert(receiver.audioDiagnostics().queue.full == 0); // Oversize is a distinct rejection cause.
    receiver.poll(); receiver.poll();
    assert(receiver.stats().decoded_frames == 2 && detail::written == 2);
    auto timing = receiver.audioDiagnostics();
    assert(timing.queue.enqueued == 2 && timing.queue.dequeued == 2 && timing.queue.depth == 0);
    assert(timing.queue.invalid_token == 0 && timing.work.packets == 2 && timing.work.invalid_sample == 0);
    assert(timing.rtp.packets == 2 && timing.rtp.missing_sequences == 0);
    detail::callbacks.stream(false, detail::callbacks.user);
    assert(receiver.stats().decoder_memory_bytes == 0);
    detail::failOutput = true;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::HardwareError);
    assert(receiver.stats().decoder_memory_bytes == 0);
    detail::failOutput = false;
    format.codec = Codec::LhdcV5; format.channels = 2; format.sample_rate = 44100; format.frame_duration_us = 5000;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    detail::callbacks.stream(true, detail::callbacks.user);
    packet[12] = 0; packet[13] = 0x53; packet[14] = 0x7d;
    detail::callbacks.media(packet, 18, detail::callbacks.user); receiver.poll();
    assert(receiver.stats().decoded_frames == 2);
    packet[3] = 1;
    detail::callbacks.media(packet, 18, detail::callbacks.user); receiver.poll(); receiver.poll();
    assert(receiver.stats().decoded_frames == 482);
    detail::outputStopped = true; packet[3] = 2;
    detail::callbacks.media(packet, 18, detail::callbacks.user); receiver.poll();
    assert(receiver.stats().last_error == Status::HardwareError && !receiver.stats().playing);
    detail::outputStopped = false;
    // Priming can accept the entire block and then fail when starting DMA.
    // Observe backend status before treating a full write as success.
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    detail::callbacks.stream(true, detail::callbacks.user);
    detail::failOutputStart = true; packet[3] = 3;
    detail::callbacks.media(packet, 18, detail::callbacks.user); receiver.poll();
    packet[3] = 4; detail::callbacks.media(packet, 18, detail::callbacks.user); receiver.poll();
    assert(receiver.stats().last_error == Status::HardwareError && !receiver.stats().playing);
    assert(receiver.stats().decoder_memory_bytes == 0);
    detail::failOutputStart = false; detail::outputStopped = false;
    receiver.end(); assert(detail::memoryStats().current_bytes == 0);
    assert(receiver.bleNotify(notification, sizeof(notification)) == Status::Unsupported);
    assert(!receiver.bleReadWrite(bleWrite));
    config.ble.security = BleSecurityPolicy::EncryptedPairing;
    unsigned securityEnds = detail::radioEndCalls;
    assert(receiver.begin(config) == Status::Unsupported);
    assert(detail::radioEndCalls == securityEnds && detail::memoryStats().current_bytes == 0);
    config.ble.security = BleSecurityPolicy::PublicUnencrypted;
    unsigned ends = detail::radioEndCalls;
    detail::radioUnavailable = true;
    assert(receiver.begin(config) == Status::Busy);
    assert(detail::radioEndCalls == ends);
    detail::radioUnavailable = false;
    config.queue_packets = 1; // Startup reserve must clamp, never deadlock a one-slot queue.
    assert(receiver.begin(config) == Status::Ok);
    format.codec = Codec::Sbc; format.channels = 1; format.sample_rate = 16000;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    detail::callbacks.stream(true, detail::callbacks.user);
    memset(packet, 0, sizeof(packet)); packet[0] = 0x80;
    packet[12] = 1; packet[13] = 0x9c; packet[14] = 0x31; packet[15] = 2;
    size_t previousWritten = detail::written;
    detail::callbacks.media(packet, sizeof(packet), detail::callbacks.user); receiver.poll();
    assert(receiver.stats().decoded_frames == 1 && detail::written == previousWritten + 1);
    receiver.end(); assert(detail::memoryStats().current_bytes == 0);
    detail::failOutput = false;
    detail::setAllocationFailureCountdown(0);
    assert(receiver.begin(config) == Status::OutOfMemory);
    detail::setAllocationFailureCountdown(-1);
    assert(detail::memoryStats().current_bytes == 0);
    // Per-codec overrides inherit each zero field and allocate only on selection.
    config = ReceiverConfig(); config.maximum_packet_bytes = 128;
    config.maximum_access_unit_bytes = 256; config.queue_packets = 2; config.startup_packets = 2;
    config.pcm_buffer_ms = 12;
    config.user = &receiver;
    config.configuration_changed = [](const CodecConfig&, void* user) {
        auto snapshot = static_cast<GigaBredr*>(user)->stats(); // Callback may reenter: selection cannot hold locks here.
        assert(snapshot.configured && snapshot.buffers.queue_packets);
    };
    assert(receiver.begin(config) == Status::Ok);
    size_t idleBytes = receiver.stats().memory.current_bytes;
    assert(receiver.stats().memory.current_blocks == 1);
    receiver.end();
    auto& unused = config.codec_buffers[size_t(Codec::Opus)];
    unused.queue_packets = 64; unused.maximum_packet_bytes = 65535;
    unused.maximum_access_unit_bytes = 65535; unused.pcm_buffer_ms = 100;
    config.codec_buffers[size_t(Codec::Sbc)].queue_packets = 3;
    auto& lhdc = config.codec_buffers[size_t(Codec::LhdcV5)];
    lhdc.queue_packets = 6; lhdc.startup_packets = 7;
    lhdc.maximum_packet_bytes = 512; lhdc.maximum_access_unit_bytes = 1024; lhdc.pcm_buffer_ms = 40;
    assert(receiver.begin(config) == Status::Ok);
    assert(receiver.stats().memory.current_bytes == idleBytes && receiver.stats().memory.current_blocks == 1);
    format = CodecConfig(); format.codec = Codec::Sbc;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    auto profile = receiver.stats().buffers;
    assert(profile.queue_packets == 3 && profile.startup_packets == 2);
    assert(profile.maximum_packet_bytes == 128 && profile.maximum_access_unit_bytes == 256 && profile.pcm_buffer_ms == 12);
    assert(detail::selectedRate == 48000 && detail::selectedPcmMs == 12);
    size_t sbcBytes = receiver.stats().memory.current_bytes;
    detail::callbacks.stream(true, detail::callbacks.user);
    memset(packet, 0, sizeof(packet)); packet[0]=0x80; packet[12]=1; packet[13]=0x9c; packet[14]=0x31; packet[15]=2;
    for (unsigned i=0;i<4;++i) { packet[3]=uint8_t(i); detail::callbacks.media(packet,sizeof packet,detail::callbacks.user); }
    assert(receiver.audioDiagnostics().queue.depth == 3 && receiver.stats().dropped_packets == 1);
    detail::callbacks.stream(false, detail::callbacks.user);
    assert(receiver.stats().memory.current_bytes == idleBytes && !receiver.stats().buffers.queue_packets);
    format.codec=Codec::LhdcV5; format.sample_rate=192000; format.frame_duration_us=5000;
    assert(detail::callbacks.configure(format, detail::callbacks.user) == Status::Ok);
    profile = receiver.stats().buffers;
    assert(profile.queue_packets==6 && profile.startup_packets==6 && profile.maximum_packet_bytes==512 && profile.maximum_access_unit_bytes==1024 && profile.pcm_buffer_ms==40);
    assert(detail::selectedRate==192000 && detail::selectedPcmMs==40);
    size_t lhdcBytes=receiver.stats().memory.current_bytes;
    assert(lhdcBytes>sbcBytes);
    detail::callbacks.stream(true, detail::callbacks.user);
    packet[12]=0; packet[13]=0x53; packet[14]=0x7d;
    // This copied packet and access unit both exceed the inherited global
    // limits; the selected maximums must reach the real queue/assembler too.
    uint8_t largePacket[400]={0x80}; largePacket[13]=0x53; largePacket[14]=0x7d;
    for(unsigned i=0;i<5;++i) { largePacket[3]=uint8_t(i); detail::callbacks.media(largePacket,sizeof largePacket,detail::callbacks.user); }
    uint8_t tooLarge[513]={0x80}; detail::callbacks.media(tooLarge,sizeof tooLarge,detail::callbacks.user);
    assert(receiver.audioDiagnostics().queue.full==0 && receiver.audioDiagnostics().queue.depth==5);
    receiver.poll(); assert(receiver.stats().decoded_frames==0); // Six-packet startup reserve is real.
    largePacket[3]=5; detail::callbacks.media(largePacket,sizeof largePacket,detail::callbacks.user);
    largePacket[3]=6; detail::callbacks.media(largePacket,sizeof largePacket,detail::callbacks.user);
    assert(receiver.audioDiagnostics().queue.depth==6 && receiver.stats().dropped_packets==3);
    receiver.poll(); assert(receiver.stats().decoded_frames==240);
    // Reconfigure directly while playing with pending old-format packets.
    format.codec=Codec::Sbc; format.sample_rate=48000; format.frame_duration_us=0;
    assert(detail::callbacks.configure(format,detail::callbacks.user)==Status::Ok);
    assert(!receiver.stats().playing && receiver.stats().buffers.queue_packets==3 && receiver.audioDiagnostics().queue.depth==0);
    assert(receiver.stats().memory.current_bytes==sbcBytes);
    detail::callbacks.stream(false, detail::callbacks.user);
    assert(receiver.stats().memory.current_bytes==idleBytes);
    format.codec=Codec::LhdcV5; format.sample_rate=192000; format.frame_duration_us=5000;
    unsigned failurePositions=0;
    for(int fail=0;fail<8;++fail) {
        detail::setAllocationFailureCountdown(fail);
        Status status=detail::callbacks.configure(format,detail::callbacks.user);
        detail::setAllocationFailureCountdown(-1);
        if(status==Status::Ok) { detail::callbacks.stream(false,detail::callbacks.user); break; }
        assert(status==Status::OutOfMemory); ++failurePositions;
        assert(receiver.stats().memory.current_bytes==idleBytes && !receiver.stats().configured && !receiver.stats().buffers.queue_packets);
    }
    assert(failurePositions>=4 && receiver.stats().memory.current_bytes==idleBytes);
    receiver.end(); assert(detail::memoryStats().current_bytes==0);
    // The intentional extended capacity range is accepted before codec selection.
    for(uint16_t ms : {uint16_t(150),uint16_t(320)}) {
        lhdc.pcm_buffer_ms=ms; assert(receiver.begin(config)==Status::Ok);
        receiver.end(); assert(detail::memoryStats().current_bytes==0);
    }
    lhdc.pcm_buffer_ms=40;
    // Invalid inactive overrides fail before any receiver/codec allocation.
    lhdc.queue_packets=65; assert(receiver.begin(config)==Status::InvalidArgument); lhdc.queue_packets=6;
    lhdc.startup_packets=65; assert(receiver.begin(config)==Status::InvalidArgument); lhdc.startup_packets=7;
    lhdc.maximum_packet_bytes=63; assert(receiver.begin(config)==Status::InvalidArgument); lhdc.maximum_packet_bytes=512;
    lhdc.maximum_access_unit_bytes=65536; assert(receiver.begin(config)==Status::InvalidArgument); lhdc.maximum_access_unit_bytes=1024;
    lhdc.pcm_buffer_ms=321; assert(receiver.begin(config)==Status::InvalidArgument); lhdc.pcm_buffer_ms=40;
    config.pcm_buffer_ms=1; assert(receiver.begin(config)==Status::InvalidArgument);
    assert(detail::memoryStats().current_bytes==0);
    config.pcm_buffer_ms=20; config.enable_i2s=false;
    lhdc.queue_packets=64; lhdc.startup_packets=64;
    assert(receiver.begin(config)==Status::Ok);
    assert(detail::callbacks.configure(format,detail::callbacks.user)==Status::Ok);
    assert(receiver.stats().buffers.queue_packets==64 && receiver.stats().buffers.startup_packets==1);
    detail::callbacks.stream(true,detail::callbacks.user);
    for(unsigned i=0;i<65;++i) detail::callbacks.media(packet,18,detail::callbacks.user);
    assert(receiver.audioDiagnostics().queue.depth==64 && receiver.stats().dropped_packets==1);
    // Real producer/configure interleaving stresses publication and retirement
    // of packet storage, maximum length, tokens and capacity as one locked unit.
    std::atomic<bool> ready{false};
    std::thread producer([&]() {
        uint8_t incoming[256]={0x80};
        ready.store(true);
        for(unsigned i=0;i<50000;++i) { incoming[3]=uint8_t(i); detail::callbacks.media(incoming,sizeof incoming,detail::callbacks.user); }
    });
    while(!ready.load()) std::this_thread::yield();
    for(unsigned i=0;i<200;++i) {
        format.codec=(i&1)?Codec::Sbc:Codec::LhdcV5;
        format.sample_rate=(i&1)?48000:192000;
        format.frame_duration_us=(i&1)?0:5000;
        assert(detail::callbacks.configure(format,detail::callbacks.user)==Status::Ok);
        detail::callbacks.stream(true,detail::callbacks.user);
        receiver.poll();
    }
    producer.join();
    assert(receiver.audioDiagnostics().queue.invalid_token==0);
    receiver.end(); assert(detail::memoryStats().current_bytes==0);
    printf("BUFFER_PROFILES idle=%zu sbc=%zu lhdc192=%zu failure_positions=%u\n",idleBytes,sbcBytes,lhdcBytes,failurePositions);
    puts("PASS receiver lazy open/idempotent config/mono pipeline/stop/failed output/failed allocation cleanup");
}

namespace giga_bredr { namespace detail {
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins){selectedPins=pins;return i2sBegin(rate,ms);}
Status i2sBegin(uint32_t rate,uint16_t ms,const I2sPins& pins,uint16_t){return i2sBegin(rate,ms,pins);}
}}

// Link boundary for lifecycle APIs unused by this existing regression.
namespace giga_bredr { namespace detail {
Status radioBleDisconnect(uint32_t,bool){return Status::Unsupported;}
Status radioBleResumeAdvertising(){return Status::Unsupported;}
}}
