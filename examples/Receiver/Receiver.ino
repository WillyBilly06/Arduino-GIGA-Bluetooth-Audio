#include <GigaBredr.h>
#include <platform/I2sPins.h>
#include <platform/GigaI2s.h>
#include "Console.h"
using namespace giga_bredr;
GigaBredr receiver;
ReceiverConsole console;
void diagnostic(const char* message, void*) { console.print("GBR_EVENT ms=%lu %s\n", (unsigned long)millis(), message); }
void configured(const CodecConfig& c, void*) {
  console.print("GBR_CONFIG codec=%s rate=%lu channels=%u bits=%u bitrate=%lu duration_us=%lu\n", codecName(c.codec), (unsigned long)c.sample_rate, c.channels, c.bits_per_sample, (unsigned long)c.bitrate, (unsigned long)c.frame_duration_us);
}
void volume(uint8_t value, void*) { console.print("GBR_VOLUME value=%u\n", value); }
void metadata(uint8_t attribute, const char* text, size_t bytes, void*) { console.print("GBR_METADATA attribute=%u bytes=%u text=%.*s\n", attribute, unsigned(bytes), int(bytes), text); }
void setup() {
  Serial.begin(115200);
  uint32_t start=millis(); while (!Serial && millis()-start<3000) delay(10);
  ReceiverConfig config; config.name="GIGA Audio";
  // Codec-local buffering; other codecs inherit ReceiverConfig globals.
  auto& lhdc=config.codec_buffers[static_cast<size_t>(Codec::LhdcV5)];
  lhdc.queue_packets=32; lhdc.startup_packets=26;
  lhdc.maximum_packet_bytes=2048; lhdc.pcm_buffer_ms=20;
  auto& ldac=config.codec_buffers[static_cast<size_t>(Codec::Ldac)];
  ldac.queue_packets=40; ldac.startup_packets=16;
  auto& sbc=config.codec_buffers[static_cast<size_t>(Codec::Sbc)];
  sbc.queue_packets=12; sbc.startup_packets=4;
  config.ble.enabled=true; config.ble.maximum_mtu=517;
  config.diagnostic=diagnostic; config.configuration_changed=configured;
  config.volume_changed=volume; config.metadata_changed=metadata;
  Status status=receiver.begin(config);
  console.print("GBR_BEGIN status=%s compiled_mask=%08lx\n", statusName(status), (unsigned long)receiver.availableCodecs());
  console.print("GBR_PINS BCLK=70 DIN=72 LRCK=%u\n", unsigned(GIGA_BREDR_I2S_WS_PIN));
}
void loop() {
  receiver.poll();
  // Example data service: one copied RX write is echoed through TX notify.
  // Keep the pending value until the bounded actor queue accepts it.
  static BleWrite bleEcho;
  static bool bleEchoPending=false;
  if(!bleEchoPending) bleEchoPending=receiver.bleReadWrite(bleEcho);
  if(bleEchoPending) {
    Status status=receiver.bleNotify(bleEcho);
    if(status!=Status::Busy && status!=Status::OutputFull) {
      console.print("GBR_BLE_RX generation=%lu bytes=%u notify=%s\n",(unsigned long)bleEcho.generation,bleEcho.length,statusName(status));
      bleEchoPending=false;
    }
  }
  static uint32_t last=0;
  if (millis()-last>=2000) {
    last=millis(); auto s=receiver.stats();
    console.print("GBR_STATUS ms=%lu ready=%u bredr=%u address=%02X:%02X:%02X:%02X:%02X:%02X connected=%u streaming=%u codec=%s rate=%lu rx_packets=%lu rx_bytes=%lu hci_packets=%lu hci_bytes=%lu decoded_packets=%lu frames=%llu errors=%lu dropped=%lu decode_max_us=%lu underruns=%lu dma_errors=%lu output_frames=%llu queued=%u output_running=%u priming=%u output_dropped=%lu transport_errors=%lu volume=%u memory=%u peak=%u codec_bytes=%u diagnostic_drops=%lu console_drops=%u last=%s\n",
      (unsigned long)millis(), s.radio.controller_ready, s.radio.bredr_supported,
      s.radio.address[0], s.radio.address[1], s.radio.address[2], s.radio.address[3], s.radio.address[4], s.radio.address[5],
      s.radio.connected, s.playing, codecName(s.configuration.codec), (unsigned long)s.configuration.sample_rate,
      (unsigned long)s.radio.received_packets, (unsigned long)s.radio.received_bytes, (unsigned long)s.radio.hci_packets, (unsigned long)s.radio.hci_bytes,
      (unsigned long)s.decoded_packets, (unsigned long long)s.decoded_frames,
      (unsigned long)s.decode_errors, (unsigned long)s.dropped_packets, (unsigned long)s.maximum_decode_us,
      (unsigned long)s.output.underruns, (unsigned long)s.output.dma_errors, (unsigned long long)s.output.frames_transmitted,
      unsigned(s.output.queued_frames), s.output.running, s.output.priming, (unsigned long)s.output_dropped_frames,
      (unsigned long)s.radio.transport_errors, s.absolute_volume, unsigned(s.memory.current_bytes), unsigned(s.memory.peak_bytes), unsigned(s.decoder_memory_bytes),
      (unsigned long)s.diagnostic_drops, console.dropped, statusName(s.last_error));
    console.print("GBR_LHDC_CONCEAL ms=%lu right_blocks=%llu right_pcm_frames=%llu muted_blocks=%llu muted_pcm_frames=%llu\n",
      (unsigned long)millis(), (unsigned long long)s.lhdc_right_concealed_blocks,
      (unsigned long long)s.lhdc_right_concealed_frames, (unsigned long long)s.lhdc_frame_muted_blocks,
      (unsigned long long)s.lhdc_frame_muted_frames);
    auto b=receiver.bleStats(); auto t=receiver.audioDiagnostics();
    console.print("GBR_TIMING ms=%lu epoch=%lu rtp_packets=%llu gaps=%llu missing=%llu duplicates=%llu backward=%llu arrival_max_us=%lu queue_depth=%lu queue_high=%lu queue_full=%llu residence_max_us=%lu work_packets=%llu work_max_us=%lu decode_work_max_us=%lu write_wait_max_us=%lu output_epoch=%lu refills=%llu real_frames=%llu silence_frames=%llu minimum_pcm=%lu underrun_records=%lu ble_advertising=%u ble_connected=%u ble_mtu=%u ble_writes=%lu ble_notifications=%lu ble_command_drops=%lu\n",
      (unsigned long)millis(),(unsigned long)t.rtp.epoch,(unsigned long long)t.rtp.packets,
      (unsigned long long)t.rtp.forward_gaps,(unsigned long long)t.rtp.missing_sequences,
      (unsigned long long)t.rtp.duplicates,(unsigned long long)t.rtp.backward,
      (unsigned long)t.rtp.arrival_gaps.maximum_us,(unsigned long)t.queue.depth,(unsigned long)t.queue.high_water,
      (unsigned long long)t.queue.full,(unsigned long)t.queue.residence.maximum_us,
      (unsigned long long)t.work.packets,(unsigned long)t.work.elapsed.maximum_us,
      (unsigned long)t.work.decode_work.maximum_us,(unsigned long)t.work.write_wait.maximum_us,
      (unsigned long)t.output.epoch,(unsigned long long)t.output.refills,(unsigned long long)t.output.real_frames,
      (unsigned long long)t.output.silent_frames,(unsigned long)t.output.minimum_queued,(unsigned long)t.output.record_count,
      b.advertising,b.connected,b.att_mtu,(unsigned long)b.writes,(unsigned long)b.notifications_queued,(unsigned long)b.write_command_drops);
    console.print("GBR_RTP epoch=%lu source_packets=%llu first_ts=%lu last_ts=%lu first_us=%lu last_us=%lu clock_disc=%llu source_changes=%llu queue_residence_total_us=%llu work_total_us=%llu decode_total_us=%llu wait_total_us=%llu discontinuities=%llu timestamp_steps=%llu inner_steps=%llu payload_unknown=%llu payload_malformed=%llu disc_records=%lu disc_not_retained=%llu\n",
      (unsigned long)t.rtp.epoch,(unsigned long long)t.rtp.source_packets,
      (unsigned long)t.rtp.source_first_timestamp,(unsigned long)t.rtp.source_last_timestamp,
      (unsigned long)t.rtp.source_first_arrival_us,(unsigned long)t.rtp.source_last_arrival_us,
      (unsigned long long)t.rtp.clock_discontinuities,(unsigned long long)t.rtp.source_changes,
      (unsigned long long)t.queue.residence.total_us,(unsigned long long)t.work.elapsed.total_us,
      (unsigned long long)t.work.decode_work.total_us,(unsigned long long)t.work.write_wait.total_us,
      (unsigned long long)t.rtp.discontinuities,(unsigned long long)t.rtp.timestamp_discontinuities,
      (unsigned long long)t.rtp.inner_sequence_discontinuities,(unsigned long long)t.rtp.unknown_payloads,
      (unsigned long long)t.rtp.malformed_payloads,(unsigned long)t.rtp.record_count,(unsigned long long)t.rtp.records_not_retained);
    static uint32_t rtpEpoch=0,rtpRecordIndex=0;
    if(rtpEpoch!=t.rtp.epoch) { rtpEpoch=t.rtp.epoch;rtpRecordIndex=0; }
    if(rtpRecordIndex<t.rtp.record_count) {
      const auto& r=t.rtp.records[rtpRecordIndex++];
      console.print("GBR_RTP_DISC epoch=%lu index=%lu packet=%llu reasons=%u prev_ssrc=%lu ssrc=%lu prev_seq=%u seq=%u prev_ts=%lu ts=%lu prev_us=%lu at_us=%lu expected_valid=%u expected_delta=%lu delta_valid=%u delta=%lu arrival_valid=%u arrival_delta_us=%lu prev_header=%u header=%u prev_inner=%u inner=%u prev_declared=%u declared=%u prev_scanned=%u scanned=%u prev_flags=%u flags=%u\n",
        (unsigned long)rtpEpoch,(unsigned long)(rtpRecordIndex-1),(unsigned long long)r.packet_number,unsigned(r.reasons),
        (unsigned long)r.previous_ssrc,(unsigned long)r.ssrc,unsigned(r.previous_sequence),unsigned(r.sequence),
        (unsigned long)r.previous_timestamp,(unsigned long)r.timestamp,(unsigned long)r.previous_arrival_us,(unsigned long)r.arrival_us,
        unsigned(r.expected_delta_valid),(unsigned long)r.expected_timestamp_delta,unsigned(r.timestamp_delta_valid),(unsigned long)r.timestamp_delta,
        unsigned(r.arrival_delta_valid),(unsigned long)r.arrival_delta_us,unsigned(r.previous_packet.header),unsigned(r.packet.header),
        unsigned(r.previous_packet.inner_sequence),unsigned(r.packet.inner_sequence),unsigned(r.previous_packet.declared_frames),unsigned(r.packet.declared_frames),
        unsigned(r.previous_packet.scanned_frames),unsigned(r.packet.scanned_frames),unsigned(r.previous_packet.flags),unsigned(r.packet.flags));
    }
    auto recovery=detail::i2sRecovery();
    console.print("GBR_RECOVERY ms=%lu epoch=%lu active=%u holding=%u threshold=%lu half=%lu entries=%lu resumes=%lu\n",
      (unsigned long)millis(),(unsigned long)recovery.epoch,recovery.active,recovery.recovering,
      (unsigned long)recovery.threshold,(unsigned long)recovery.half_frames,
      (unsigned long)recovery.entries,(unsigned long)recovery.resumes);
    console.print("GBR_RECOVERY_FRAMES epoch=%lu starved=%llu held=%llu hold_refills=%llu current_hold=%llu max_hold=%llu records=%lu lost=%lu\n",
      (unsigned long)recovery.epoch,(unsigned long long)recovery.starved_frames,(unsigned long long)recovery.hold_frames,
      (unsigned long long)recovery.hold_refills,(unsigned long long)recovery.current_hold_frames,
      (unsigned long long)recovery.maximum_hold_frames,(unsigned long)recovery.record_count,(unsigned long)recovery.records_not_retained);
    static uint32_t recoveryEpoch=0,recoveryIndex=0;
    if(recoveryEpoch!=recovery.epoch) { recoveryEpoch=recovery.epoch;recoveryIndex=0; }
    if(recoveryIndex<recovery.record_count) {
      const auto& r=recovery.records[recoveryIndex++];
      console.print("GBR_RECOVERY_TRANS epoch=%lu index=%lu kind=%u at_us=%lu queued=%lu real=%lu silent=%lu\n",
        (unsigned long)recoveryEpoch,(unsigned long)(recoveryIndex-1),r.kind,(unsigned long)r.at_us,
        (unsigned long)r.queued,(unsigned long)r.real,(unsigned long)r.silent);
    }
    static uint32_t outputEpoch=0,recordIndex=0;
    if(outputEpoch!=t.output.epoch) { outputEpoch=t.output.epoch;recordIndex=0; }
    if(recordIndex<t.output.record_count) {
      const auto& u=t.output.records[recordIndex++];
      console.print("GBR_UNDERRUN epoch=%lu index=%lu at_us=%lu refill=%llu queued_before=%lu real=%lu silence=%lu\n",
        (unsigned long)outputEpoch,(unsigned long)(recordIndex-1),(unsigned long)u.at_us,
        (unsigned long long)u.refill_number,(unsigned long)u.queued_before,(unsigned long)u.real_frames,(unsigned long)u.silent_frames);
    }
  }
  console.pump();
  delay(1);
}
