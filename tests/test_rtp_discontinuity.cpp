#include "detail/AudioTiming.h"
#include <cstdio>
#include <cstring>
using namespace giga_bredr::detail;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x); return false; } } while (0)

// Actual Savitech header layout: one encoded frame is 1<<2, followed by
// the independent encoder packet sequence. Each small test frame has its
// own LE length header, 18 payload bytes plus the two-byte frame header.
static RtpPacketDetail packet(uint8_t sequence, unsigned frames=1, uint32_t hop=960) {
    uint8_t bytes[2+15*20]={}; bytes[0]=uint8_t(frames<<2);bytes[1]=sequence;
    for(unsigned i=0;i<frames&&i<15;++i)bytes[2+i*20]=9;
    return lhdcRtpDetail(bytes,2+frames*20,hop);
}
static bool framing() {
    auto a=packet(255);CHECK(a.header==4&&a.inner_sequence==255);
    CHECK(a.declared_frames==1&&a.scanned_frames==1&&a.sample_frames==960);
    CHECK(a.flags&RtpSamplesValid);CHECK(a.flags&RtpInnerSequenceValid);
    a=packet(0,3);CHECK(a.header==12&&a.declared_frames==3&&a.scanned_frames==3&&a.sample_frames==2880);
    a=packet(4,8);CHECK(a.header==32&&a.declared_frames==8&&a.sample_frames==7680);
    a=packet(1,15,240);CHECK(a.sample_frames==3600&&(a.flags&RtpSamplesValid));
    a=packet(1,1,480);CHECK(a.sample_frames==480);
    a=packet(1,1,123);CHECK(!(a.flags&RtpSamplesValid)&&(a.flags&RtpScannedFramesValid));
    uint8_t bytes[42]={4,17,9};bytes[22]=9;
    a=lhdcRtpDetail(bytes,22,960);CHECK(a.flags&RtpSamplesValid);
    for(size_t n=0;n<22;++n)CHECK(!(lhdcRtpDetail(bytes,n,960).flags&RtpSamplesValid));
    CHECK(!(lhdcRtpDetail(nullptr,22,960).flags&RtpSamplesValid));
    CHECK(!(lhdcRtpDetail(bytes,42,960).flags&RtpSamplesValid)); // trailing frame
    bytes[0]=8;CHECK(lhdcRtpDetail(bytes,42,960).sample_frames==1920);
    CHECK(!(lhdcRtpDetail(bytes,22,960).flags&RtpSamplesValid)); // declared count mismatch
    bytes[0]=5;CHECK(lhdcRtpDetail(bytes,22,960).flags&RtpPayloadMalformed);
    bytes[0]=0;CHECK(!(lhdcRtpDetail(bytes,22,960).flags&RtpDeclaredFramesValid));
    bytes[0]=0x84;a=lhdcRtpDetail(bytes,22,960);
    CHECK((a.flags&RtpPayloadFragmented)&&!(a.flags&RtpSamplesValid));
    bytes[0]=4;bytes[2]=0;CHECK(lhdcRtpDetail(bytes,22,960).flags&RtpPayloadMalformed);
    bytes[2]=0xff;bytes[3]=3;CHECK(lhdcRtpDetail(bytes,22,960).flags&RtpPayloadMalformed);
    // The declared 15-frame ceiling bounds scan work and all pointer advances.
    CHECK(!(lhdcRtpDetail(bytes,1000000,960).flags&RtpSamplesValid));
    return true;
}
static bool timestampAndInnerLoss() {
    RtpTiming t;t.beginEpoch(7);
    t.observe(7,1,10,0,1000,packet(250));
    t.observe(7,1,11,29*960,6000,packet(23)); // 28 encoded packets lost pre-RTP
    t.observe(7,1,12,30*960,11000,packet(24));
    auto s=t.snapshot();CHECK(s.discontinuities==1&&s.record_count==1);
    CHECK(s.timestamp_discontinuities==1&&s.inner_sequence_discontinuities==1);
    CHECK(s.forward_gaps==0&&s.missing_sequences==0);
    const auto&r=s.records[0];
    CHECK(r.packet_number==2&&r.previous_sequence==10&&r.sequence==11);
    CHECK((r.reasons&RtpTimestampStep)&&(r.reasons&RtpInnerSequenceStep));
    CHECK(r.expected_delta_valid&&r.timestamp_delta_valid&&r.expected_timestamp_delta==960&&r.timestamp_delta==27840);
    CHECK(r.previous_packet.inner_sequence==250&&r.packet.inner_sequence==23);
    CHECK(r.arrival_delta_valid&&r.arrival_delta_us==5000);
    // Compare to PREVIOUS packet frame count, including a multi-frame packet.
    t.beginEpoch(8);t.observe(8,1,1,0,0,packet(1,2));
    t.observe(8,1,2,1920,10000,packet(2));CHECK(t.snapshot().discontinuities==0);
    t.observe(8,1,3,2880,15000,packet(3));CHECK(t.snapshot().discontinuities==0);
    return true;
}
static bool unknownAndMalformed() {
    RtpTiming t;t.beginEpoch(1);
    t.observe(1,1,1,0,0);t.observe(1,1,2,0xffffffffu,1000);
    CHECK(!t.snapshot().timestamp_discontinuities&&!t.snapshot().record_count);
    t.beginEpoch(2);auto unknown=packet(2,1,123);
    t.observe(2,1,1,0,0,packet(1));t.observe(2,1,2,0x80000000u,5000,unknown);
    auto s=t.snapshot();CHECK(!s.timestamp_discontinuities&&s.unknown_payloads==1);
    CHECK(s.record_count==1&&!s.records[0].expected_delta_valid&&!s.records[0].timestamp_delta_valid);
    CHECK(s.records[0].expected_timestamp_delta==0&&s.records[0].timestamp_delta==0);
    t.observe(2,1,3,1,10000,packet(3));CHECK(!t.snapshot().timestamp_discontinuities);
    uint8_t malformed[3]={4,4,9};auto bad=lhdcRtpDetail(malformed,sizeof(malformed),960);
    t.observe(2,1,4,0xffffffffu,15000,bad);s=t.snapshot();
    CHECK(s.malformed_payloads==1&&s.unknown_payloads==2&&!s.timestamp_discontinuities);
    CHECK(!s.records[s.record_count-1].expected_delta_valid);
    t.beginEpoch(3);auto forged=packet(2);forged.sample_frames=0xffffffffu;
    t.observe(3,1,1,0,0,packet(1));t.observe(3,1,2,960,5000,forged);
    t.observe(3,1,3,1920,10000,packet(3));s=t.snapshot();
    CHECK(!s.timestamp_discontinuities&&s.unknown_payloads==1);
    CHECK(!s.records[0].expected_delta_valid&&s.records[0].expected_timestamp_delta==0);
    return true;
}
static bool wrapLossReorderAndSource() {
    RtpTiming t;t.beginEpoch(1);
    t.observe(1,4,65535,0xfffffc40u,0xfffffff0u,packet(255));
    t.observe(1,4,0,0,0x20,packet(0));CHECK(!t.snapshot().discontinuities);
    t.observe(1,4,2,1920,0x100,packet(2));auto s=t.snapshot();
    CHECK(s.forward_gaps==1&&s.missing_sequences==1&&s.timestamp_discontinuities==0);
    CHECK(s.records[0].reasons&RtpSequenceGap);CHECK(!s.records[0].expected_delta_valid);
    t.observe(1,4,2,1920,0x110,packet(2));
    t.observe(1,4,1,960,0x120,packet(1));
    t.observe(1,4,3,2880,0x130,packet(3));
    s=t.snapshot();CHECK(s.discontinuities==3&&s.timestamp_discontinuities==0&&s.inner_sequence_discontinuities==0);
    t.observe(1,5,20,500000,0x140,packet(40));s=t.snapshot();
    CHECK(s.records[3].reasons&RtpSourceChanged);CHECK(!s.records[3].expected_delta_valid);
    t.observe(1,5,21,500960,0x150,packet(41));CHECK(t.snapshot().discontinuities==4);
    // A timestamp reset is retained without presenting a huge delta as time.
    t.observe(1,5,22,1,0x160,packet(42));s=t.snapshot();
    CHECK(s.timestamp_discontinuities==1&&s.records[4].expected_delta_valid);
    CHECK(!s.records[4].timestamp_delta_valid&&s.records[4].timestamp_delta==0);
    return true;
}
static bool boundedLedgerAndEpochs() {
    RtpTiming t;t.beginEpoch(4);t.observe(4,1,1,0,1000,packet(1));
    for(unsigned i=2;i<=20;++i)t.observe(4,1,uint16_t(i),i*1920,i*1000,packet(uint8_t(i)));
    auto s=t.snapshot();CHECK(s.discontinuities==19&&s.record_count==16&&s.records_not_retained==3);
    CHECK(s.records[0].packet_number==2&&s.records[15].packet_number==17);
    auto first=s.records[0];t.observe(3,1,21,999,999,packet(21));
    CHECK(t.snapshot().discontinuities==19&&t.snapshot().rejected_epoch==1);
    t.endEpoch(4);t.observe(4,1,21,999,999,packet(21));CHECK(t.snapshot().discontinuities==19);
    t.beginEpoch(5);CHECK(t.snapshot().record_count==0&&t.snapshot().discontinuities==0);
    t.observe(5,1,1,0,1000,packet(1));t.observe(5,1,2,960,900,packet(2));
    auto fresh=t.snapshot();CHECK(fresh.clock_discontinuities==1&&fresh.record_count==1);
    CHECK((fresh.records[0].reasons&RtpArrivalClock)&&!fresh.records[0].arrival_delta_valid);
    CHECK(s.records[0].timestamp==first.timestamp); // Independent frozen copy.
    return true;
}
int main() {
    const bool ok[]={framing(),timestampAndInnerLoss(),unknownAndMalformed(),wrapLossReorderAndSource(),boundedLedgerAndEpochs()};
    unsigned passed=0;for(bool x:ok)passed+=x;
    std::printf("RTP discontinuity tests: %u/5 passed\n",passed);
    return passed==5?0:1;
}
