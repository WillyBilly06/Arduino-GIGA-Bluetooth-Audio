#include <GigaBredr.h>
#include <platform/I2sPins.h>
#include <platform/GigaI2s.h>
#include "Console.h"
#include <platform/SchedulerWitness.h>
using namespace giga_bredr;
GigaBredr receiver;
ReceiverConsole console;
BleConnectionPreference requestedConnectionPreference;
void diagnostic(const char* message, void*) {
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
  const unsigned before=console.dropped;
#endif
  console.print("GBR_EVENT ms=%lu %s\n", (unsigned long)millis(), message);
#if GIGA_BREDR_PRIVATE_SCHED_WITNESS
  giga_bredr::detail::schedulerConsoleReceipt(console.dropped==before); // Queue admission only; no USB receipt.
#endif
}
void configured(const CodecConfig& c, void*) {
  console.print("GBR_CONFIG codec=%s rate=%lu channels=%u bits=%u bitrate=%lu duration_us=%lu\n", codecName(c.codec), (unsigned long)c.sample_rate, c.channels, c.bits_per_sample, (unsigned long)c.bitrate, (unsigned long)c.frame_duration_us);
}
void volume(uint8_t value, void*) { console.print("GBR_VOLUME value=%u\n", value); }
void metadata(uint8_t attribute, const char* text, size_t bytes, void*) { console.print("GBR_METADATA attribute=%u bytes=%u text=%.*s\n", attribute, unsigned(bytes), int(bytes), text); }
void setup() {
  Serial.begin(115200);
  uint32_t start=millis(); while (!Serial && millis()-start<3000) delay(10);
  ReceiverConfig config; config.name="GIGA Audio";
  // Zero buffer settings select the negotiated codec's real-PCM reserve.
  // Only the selected configuration allocates; DMA waits for its watermark.
  // Nonzero config.codec_buffers[Codec] fields provide explicit overrides.
  config.ble.enabled=true; config.ble.maximum_mtu=517;
  config.ble.audio_connection_preference.interval_min=96;
  config.ble.audio_connection_preference.interval_max=96; // 120ms; peer acceptance is reported.
  config.diagnostic=diagnostic; config.configuration_changed=configured;
  config.volume_changed=volume; config.metadata_changed=metadata;
  // Hardware-compatible pins are selected before begin(); -1 disables MCLK.
  config.i2s.bclk=70; config.i2s.data=72; config.i2s.lrck=GIGA_BREDR_I2S_WS_PIN; config.i2s.mclk=-1;
  requestedConnectionPreference=config.ble.audio_connection_preference;
  Status status=receiver.begin(config);
  console.print("GBR_BEGIN status=%s compiled_mask=%08lx\n", statusName(status), (unsigned long)receiver.availableCodecs());
  console.print("GBR_PINS BCLK=%d DIN=%d LRCK=%d MCLK=%d\n",int(config.i2s.bclk),int(config.i2s.data),int(config.i2s.lrck),int(config.i2s.mclk));
}
void loop() {
  receiver.poll();
  // Private qualification controls: one foreground byte/attempt per turn.
  // Actor lifecycle diagnostics above expose actual detach/new-owner edges.
  if(Serial.available()>0) {
    const int command=Serial.read();
    if(command=='d') {
      const auto current=receiver.bleStats();
      const auto result=receiver.bleDisconnect(current.generation,false);
      console.print("GBR_BLE_CONTROL ms=%lu action=disconnect expected=%lu advertise_after=0 status=%s\n",
        (unsigned long)millis(),(unsigned long)current.generation,statusName(result));
    } else if(command=='a') {
      const auto result=receiver.bleResumeAdvertising();
      console.print("GBR_BLE_CONTROL ms=%lu action=resume_advertising status=%s\n",
        (unsigned long)millis(),statusName(result));
    }
  }
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
    const auto& buffers=s.buffers;const auto& output=s.output;
    console.print("GBR_BUFFERS codec=%s rate=%lu packet_bytes=%u au_bytes=%u queue_slots=%u startup_packets=%u startup_ms=%u pcm_ms=%u ring_frames=%lu dma_half_frames=%lu startup_frames=%lu initial_queued_frames=%lu queued_frames=%lu\n",
      codecName(s.configuration.codec),(unsigned long)s.configuration.sample_rate,unsigned(buffers.maximum_packet_bytes),
      unsigned(buffers.maximum_access_unit_bytes),unsigned(buffers.queue_packets),unsigned(buffers.startup_packets),
      unsigned(buffers.startup_buffer_ms),unsigned(buffers.pcm_buffer_ms),(unsigned long)output.buffer_capacity_frames,
      (unsigned long)output.dma_half_frames,(unsigned long)output.startup_frames,
      (unsigned long)output.initial_queued_frames,(unsigned long)output.queued_frames);
    const auto& clock=s.clock;
    console.print("GBR_CLOCK ms=%lu observed_us=%lu stream=%lu output=%lu expected_output=%lu enabled=%u eligible=%u ring_capacity=%lu required_ring=%lu state=%u backend=%u fault=%u applied_valid=%u inventory_valid=%u requested_q16=%ld applied_q16=%ld fraction=%u readback=%u\n",
      (unsigned long)millis(),(unsigned long)clock.observation_us,(unsigned long)clock.stream_epoch,
      (unsigned long)clock.output_epoch,(unsigned long)clock.expected_output_epoch,clock.enabled,clock.eligible,
      (unsigned long)clock.ring_capacity,(unsigned long)clock.required_ring_frames,unsigned(clock.state),unsigned(clock.backend_status),
      clock.backend_fault,clock.applied_valid,clock.inventory_valid,(long)clock.requested_ppm_q16,(long)clock.applied_ppm_q16,
      unsigned(clock.fraction),unsigned(clock.readback_fraction));
    console.print("GBR_CLOCK_FRAMES stream=%lu A=%llu D=%llu P=%llu Z=%llu F=%llu G=%llu units_q16=%lu retained_q16=%lu estimated=%llu target=%llu fpp_q16=%llu assembly_discards=%lu\n",
      (unsigned long)clock.stream_epoch,(unsigned long long)clock.decoded_frames,(unsigned long long)clock.discarded_frames,
      (unsigned long long)clock.prepared_frames,(unsigned long long)clock.missing_frames,(unsigned long long)clock.completed_packets,
      (unsigned long long)clock.completed_frames,(unsigned long)clock.compressed_units_q16,(unsigned long)clock.retained_units_q16,
      (unsigned long long)clock.estimated_inventory_frames,(unsigned long long)clock.target_frames,
      (unsigned long long)clock.frames_per_packet_q16,(unsigned long)clock.assembly_discards);
    console.print("GBR_CLOCK_WORK stream=%lu observations=%lu decisions=%lu accepted=%lu refused=%lu invalid=%lu duration_cycles=%lu wait0_cycles=%lu wait1_cycles=%lu max_critical_cycles=%lu duration_valid=%u wall_overruns=%u\n",
      (unsigned long)clock.stream_epoch,(unsigned long)clock.observations,(unsigned long)clock.decisions,(unsigned long)clock.accepted,
      (unsigned long)clock.refused,(unsigned long)clock.invalid_samples,(unsigned long)clock.duration_cycles,
      (unsigned long)clock.wait_cycles[0],(unsigned long)clock.wait_cycles[1],(unsigned long)clock.maximum_critical_cycles,
      clock.duration_valid,unsigned(clock.wall_overruns));
    const auto& cw=clock.witness;const auto& worst=cw.worst;
    console.print("GBR_CLOCK_SUM stream=%lu output=%lu n=%08lx over_n=%08lx over_total=%08lx unknown=%08lx coverage=%08lx/%08lx max=%08lx/%08lx flags=%02x\n",
      (unsigned long)clock.stream_epoch,(unsigned long)clock.expected_output_epoch,(unsigned long)cw.results,
      (unsigned long)cw.overrun_updates,(unsigned long)cw.overrun_total,(unsigned long)cw.timing_unknown,
      (unsigned long)cw.wait1_started,(unsigned long)cw.wait1_complete,(unsigned long)cw.maximum_wait0,(unsigned long)cw.maximum_wait1,unsigned(cw.flags));
    console.print("GBR_CLOCK_WORST stream=%lu output=%lu serial=%08lx ordinal=%08lx configured_hz=%lu status=%u mask=%02x partial=%u waits=%08lx/%08lx parts=%08lx/%08lx/%08lx/%08lx over_total=%08lx\n",
      (unsigned long)worst.stream_epoch,(unsigned long)worst.output_epoch,(unsigned long)worst.serial,(unsigned long)worst.ordinal,
      (unsigned long)worst.configured_core_hz,unsigned(worst.status),unsigned(worst.mask),worst.partial,
      (unsigned long)worst.wait0_cycles,(unsigned long)worst.wait1_cycles,(unsigned long)worst.parts[0],(unsigned long)worst.parts[1],
      (unsigned long)worst.parts[2],(unsigned long)worst.parts[3],(unsigned long)worst.overrun_total);
    console.print("GBR_CLOCK_RESTORE stream=%lu output=%lu serial=%08lx ordinal=%08lx seen=%u flags=%02x split=%08lx/%08lx icsr=%08lx pending=%02x basepri=%08lx\n",
      (unsigned long)worst.stream_epoch,(unsigned long)worst.output_epoch,(unsigned long)worst.serial,(unsigned long)worst.ordinal,
      unsigned(worst.restore_seen),unsigned(worst.restore_flags),(unsigned long)worst.restore_split[0],(unsigned long)worst.restore_split[1],
      (unsigned long)worst.restore_icsr,unsigned(worst.restore_pending),(unsigned long)worst.restore_basepri);
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
    console.print("GBR_BLE_QOS ms=%lu generation=%lu data_fragments=%lu data_bytes=%lu paced_deferrals=%lu urgent_bypasses=%lu policy_rejections=%lu\n",
      (unsigned long)millis(),(unsigned long)b.generation,(unsigned long)b.qos_data_fragments,(unsigned long)b.qos_data_bytes,
      (unsigned long)b.qos_paced_deferrals,(unsigned long)b.qos_urgent_bypasses,(unsigned long)b.connection.audio_policy_rejections);
    const auto& c=b.connection;const auto& requested=requestedConnectionPreference;
    console.print("GBR_BLE_CONN ms=%lu gen=%lu requested=%u/%u/%u/%u enabled=%u started=%u stage=%u error=%u submitted=%u/%u/%u/%u feature_sub=%lu update_sub=%lu reply_sub=%lu signal_queued=%lu status_reports=%lu command_status=%u actual_known=%u actual_source=%u actual=%u/%u/%u update_seen=%u update_status=%u match=%u\n",
      (unsigned long)millis(),(unsigned long)b.generation,unsigned(requested.interval_min),unsigned(requested.interval_max),unsigned(requested.latency),unsigned(requested.timeout),
      unsigned(requested.enabled),unsigned(c.preference_started),unsigned(c.preference_stage),unsigned(c.preference_error),
      unsigned(c.submitted_min),unsigned(c.submitted_max),unsigned(c.submitted_latency),unsigned(c.submitted_timeout),
      (unsigned long)c.feature_submissions,(unsigned long)c.update_submissions,(unsigned long)c.reply_submissions,(unsigned long)c.signaling_requests_queued,
      (unsigned long)c.command_status_reports,unsigned(c.command_status),unsigned(c.known),unsigned(c.source),unsigned(c.interval),unsigned(c.latency),unsigned(c.timeout),
      unsigned(c.update_reported),unsigned(c.update_status),unsigned(c.matches_preference));
    console.print("GBR_TIMING ms=%lu epoch=%lu rtp_packets=%llu gaps=%llu missing=%llu duplicates=%llu backward=%llu arrival_max_us=%lu queue_depth=%lu queue_high=%lu queue_full=%llu residence_max_us=%lu work_packets=%llu work_max_us=%lu decode_work_max_us=%lu write_wait_max_us=%lu output_epoch=%lu refills=%llu real_frames=%llu missing_pcm_frames=%llu minimum_pcm=%lu underrun_records=%lu ble_advertising=%u ble_connected=%u ble_mtu=%u ble_writes=%lu ble_notifications=%lu ble_command_drops=%lu\n",
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
    console.print("GBR_PCM_TRANSITION epoch=%lu ramp=%lu synthesized=%llu zero_padding=%llu mixed_real=%llu\n",
      (unsigned long)recovery.epoch,(unsigned long)recovery.ramp_frames,
      (unsigned long long)recovery.synthesized_frames,(unsigned long long)recovery.zero_frames,
      (unsigned long long)recovery.mixed_frames);
#if defined(ARDUINO_GIGA) && defined(CORE_CM7)
    console.print("GBR_IRQ_PRIORITY dma1_stream7=%lu uart7=%lu grouping=%lu pendsv=%lu systick=%lu\n",
      (unsigned long)NVIC_GetPriority(DMA1_Stream7_IRQn),
      (unsigned long)NVIC_GetPriority(UART7_IRQn),(unsigned long)NVIC_GetPriorityGrouping(),
      (unsigned long)NVIC_GetPriority(PendSV_IRQn),(unsigned long)NVIC_GetPriority(SysTick_IRQn));
    const bool uartEnabled=(RCC->APB1LENR&RCC_APB1LENR_UART7EN)!=0;
    const uint32_t uartCr1=uartEnabled?UART7->CR1:0;
    console.print("GBR_UART_HW enabled=%u fifo=%u cr1=%08lx cr3=%08lx isr=%08lx\n",uartEnabled,
      unsigned((uartCr1&USART_CR1_FIFOEN)!=0),(unsigned long)uartCr1,
      (unsigned long)(uartEnabled?UART7->CR3:0),(unsigned long)(uartEnabled?UART7->ISR:0));
#endif
    static uint32_t recoveryEpoch=0,recoveryIndex=0;
    if(recoveryEpoch!=recovery.epoch) { recoveryEpoch=recovery.epoch;recoveryIndex=0; }
    if(recoveryIndex<recovery.record_count) {
      const auto& r=recovery.records[recoveryIndex++];
      console.print("GBR_RECOVERY_TRANS epoch=%lu index=%lu kind=%u at_us=%lu queued=%lu real=%lu missing_pcm=%lu\n",
        (unsigned long)recoveryEpoch,(unsigned long)(recoveryIndex-1),r.kind,(unsigned long)r.at_us,
        (unsigned long)r.queued,(unsigned long)r.real,(unsigned long)r.silent);
    }
    static uint32_t outputEpoch=0,recordIndex=0;
    if(outputEpoch!=t.output.epoch) { outputEpoch=t.output.epoch;recordIndex=0; }
    if(recordIndex<t.output.record_count) {
      const auto& u=t.output.records[recordIndex++];
      console.print("GBR_UNDERRUN epoch=%lu index=%lu at_us=%lu refill=%llu queued_before=%lu real=%lu missing_pcm=%lu\n",
        (unsigned long)outputEpoch,(unsigned long)(recordIndex-1),(unsigned long)u.at_us,
        (unsigned long long)u.refill_number,(unsigned long)u.queued_before,(unsigned long)u.real_frames,(unsigned long)u.silent_frames);
    }
  }
  console.pump();
  delay(1);
}
