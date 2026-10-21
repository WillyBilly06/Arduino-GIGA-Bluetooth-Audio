#include <GigaBredr.h>
#include <platform/I2sPins.h>
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
    Status status=receiver.bleNotify(bleEcho.data,bleEcho.length);
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
    auto b=receiver.bleStats(); auto t=receiver.audioDiagnostics();
    console.print("GBR_TIMING ms=%lu epoch=%lu rtp_packets=%llu gaps=%llu missing=%llu duplicates=%llu backward=%llu arrival_max_us=%lu queue_depth=%lu queue_high=%lu queue_full=%llu residence_max_us=%lu work_packets=%llu work_max_us=%lu decode_work_max_us=%lu write_wait_max_us=%lu output_epoch=%lu refills=%llu real_frames=%llu silence_frames=%llu minimum_pcm=%lu underrun_records=%lu ble_advertising=%u ble_connected=%u ble_mtu=%u ble_writes=%lu ble_notifications=%lu\n",
      (unsigned long)millis(),(unsigned long)t.rtp.epoch,(unsigned long long)t.rtp.packets,
      (unsigned long long)t.rtp.forward_gaps,(unsigned long long)t.rtp.missing_sequences,
      (unsigned long long)t.rtp.duplicates,(unsigned long long)t.rtp.backward,
      (unsigned long)t.rtp.arrival_gaps.maximum_us,(unsigned long)t.queue.depth,(unsigned long)t.queue.high_water,
      (unsigned long long)t.queue.full,(unsigned long)t.queue.residence.maximum_us,
      (unsigned long long)t.work.packets,(unsigned long)t.work.elapsed.maximum_us,
      (unsigned long)t.work.decode_work.maximum_us,(unsigned long)t.work.write_wait.maximum_us,
      (unsigned long)t.output.epoch,(unsigned long long)t.output.refills,(unsigned long long)t.output.real_frames,
      (unsigned long long)t.output.silent_frames,(unsigned long)t.output.minimum_queued,(unsigned long)t.output.record_count,
      b.advertising,b.connected,b.att_mtu,(unsigned long)b.writes,(unsigned long)b.notifications_queued);
    console.print("GBR_RTP epoch=%lu source_packets=%llu first_ts=%lu last_ts=%lu first_us=%lu last_us=%lu clock_disc=%llu source_changes=%llu queue_residence_total_us=%llu work_total_us=%llu decode_total_us=%llu wait_total_us=%llu\n",
      (unsigned long)t.rtp.epoch,(unsigned long long)t.rtp.source_packets,
      (unsigned long)t.rtp.source_first_timestamp,(unsigned long)t.rtp.source_last_timestamp,
      (unsigned long)t.rtp.source_first_arrival_us,(unsigned long)t.rtp.source_last_arrival_us,
      (unsigned long long)t.rtp.clock_discontinuities,(unsigned long long)t.rtp.source_changes,
      (unsigned long long)t.queue.residence.total_us,(unsigned long long)t.work.elapsed.total_us,
      (unsigned long long)t.work.decode_work.total_us,(unsigned long long)t.work.write_wait.total_us);
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
