#include "../src/owned/ClassicProfiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
using namespace giga_bredr;
using namespace giga_bredr::detail;
struct App {
 bth_control_tx_t* queue=nullptr;
 bool open=false, fail=false, playing=false;
 unsigned configured=0, stopped=0, media=0, volumes=0;
 uint8_t volume=127;
 CodecConfig config;
 std::vector<uint8_t> packet;
 std::vector<std::pair<unsigned,std::string>> metadata;
};
static Status configure(const CodecConfig& c,void* u) {
 auto& a=*static_cast<App*>(u); ++a.configured;
 // A reply slot is reserved, but ACCEPT is not visible to transport yet.
 assert(a.queue->peers[0].count==0 && a.queue->peers[0].owned==1);
 a.open=!a.fail; if(a.fail) return Status::OutOfMemory;
 a.config=c; return Status::Ok;
}
static void stream(bool playing,void* u) {auto& a=*static_cast<App*>(u);a.playing=playing;if(!playing){a.open=false;++a.stopped;}}
static void media(const uint8_t* p,size_t n,void* u) {auto& a=*static_cast<App*>(u);++a.media;a.packet.assign(p,p+n);}
static void volume(uint8_t v,void* u) {auto& a=*static_cast<App*>(u);++a.volumes;a.volume=v;}
static void metadata(uint8_t id,const char* p,size_t n,void* u) {static_cast<App*>(u)->metadata.push_back({id,std::string(p,n)});}
struct Fixture {
 bth_control_tx_t q; bth_hci_flow_t flow; ClassicProfiles profiles; App app;
 Fixture(uint32_t mask=AllCodecs,bool with_volume=true) {
  bth_control_tx_init(&q);assert(bth_control_tx_attach(&q,0,0x25,1));
  bth_hci_flow_init(&flow);assert(bth_hci_flow_set_buffers(&flow,1021,256,0,0));assert(bth_hci_flow_attach(&flow,0x25,BTH_LINK_CLASSIC));
  app.queue=&q;RadioCallbacks cb;cb.configure=configure;cb.media=media;cb.stream=stream;cb.metadata=metadata;cb.volume=with_volume?volume:nullptr;cb.user=&app;
  assert(profiles.begin(mask,cb,q)==Status::Ok);profiles.reset(1);
  assert(profiles.channelOpened(0x19,0x40,0x80,672));
 }
 std::vector<uint8_t> pop(uint16_t expected_cid=0x80) {
  uint8_t bytes[1030];size_t n=0;const uint32_t owners[2]={1,0};
  auto result=bth_control_tx_next(&q,owners,&flow,bytes,sizeof bytes,&n);
  if(result!=BTH_OK)fprintf(stderr,"pop result=%d queued=%u owned=%u signal=%u cfg=%u configured-calls=%u\n",int(result),q.peers[0].count,q.peers[0].owned,profiles.signalCid(),profiles.configured(),app.configured);
  assert(result==BTH_OK);
  assert(n>=9 && bytes[0]==2 && bth_read_le16(bytes+7)==expected_cid && bth_read_le16(bytes+5)==n-9);
  return {bytes+9,bytes+n};
 }
 void rx(const std::vector<uint8_t>& p,uint16_t cid=0x40,bool secure=true,uint32_t now=10) {assert(profiles.receive(cid,p.data(),p.size(),secure,now));}
 void expect(const std::vector<uint8_t>& p,std::initializer_list<uint8_t> wanted) {rx(p);assert(pop()==std::vector<uint8_t>(wanted));}
 void configured() {unsigned before=app.configured;expect({0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53},{0x12,3});assert(app.configured==before+1 && app.open && profiles.configured());}
 void opened() {configured();expect({0x20,6,4},{0x22,6});assert(profiles.wantsMediaChannel());assert(profiles.channelOpened(0x19,0x41,0x81,1008));}
 void started() {opened();expect({0x30,7,4},{0x32,7});assert(app.configured==2 && app.playing && profiles.streaming());}
};
static void configuration_transaction_and_lifecycle() {
 Fixture f;f.configured();assert(f.app.config.sample_rate==44100 && f.app.config.codec_info[0]==0x21);
 f.expect({0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53},{0x13,3,0,0x13});assert(f.app.configured==1);
 Fixture bad;bad.app.fail=true;bad.expect({0x40,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53},{0x43,3,7,0x29});assert(!bad.profiles.configured()&&!bad.app.open);
 bad.app.fail=false;bad.configured();
 Fixture s;s.started();const uint8_t media_bytes[]={0x80,0x60,0,1,1,2,3,4,5,6,7,8,0x01,0xaa};
 assert(s.profiles.receive(0x41,media_bytes,sizeof media_bytes,true,100));assert(s.app.media==1 && s.app.packet==std::vector<uint8_t>(media_bytes,media_bytes+sizeof media_bytes));
 s.expect({0x40,9,4},{0x42,9});assert(!s.app.open && !s.profiles.streaming()&&s.profiles.configured());
 s.app.fail=true;s.expect({0x50,7,4},{0x53,7,4,0x31});assert(!s.profiles.streaming()&&s.profiles.configured());
 s.app.fail=false;s.expect({0x60,7,4},{0x62,7});assert(s.profiles.streaming());
 s.expect({0x70,8,4},{0x72,8});assert(!s.profiles.configured()&&!s.app.open&&s.profiles.takeCloseMedia());
 s.profiles.channelClosed(0x41);assert(!s.profiles.mediaCid());
}
static void validation_and_reconfigure() {
 Fixture f;f.expect({0x10,3,4,8,1,0,7,5,0,0,0x21,0x15,2},{0x13,3,7,0x29});assert(f.app.configured==0);
 f.expect({0x20,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53,0xff},{0x23,3,0xff,0x11});assert(f.app.configured==0);
 f.rx({0x30,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53},0x40,false);assert(f.pop()==std::vector<uint8_t>({0x33,3,0,0x31}));assert(f.app.configured==0);
}
static void reconfigure_and_disabled_endpoint() {
 Fixture f;f.opened();f.expect({0x40,5,4,7,6,0,0,0x11,0x15,2,53},{0x42,5});assert(f.app.config.sample_rate==48000);
 f.app.fail=true;f.expect({0x50,5,4,7,6,0,0,0x21,0x15,2,53},{0x53,5,7,0x29});assert(!f.profiles.configured());
 f.app.fail=false;f.expect({0x60,5,4,7,6,0,0,0x21,0x15,2,53},{0x62,5});assert(f.profiles.configured());
 Fixture only(codecBit(Codec::Sbc));only.rx({0x10,1});assert(only.pop()==std::vector<uint8_t>({0x12,1,4,8}));
 only.expect({0x20,3,8,12,1,0,7,8,0,2,0x40,0,0x84,0x80,0,0},{0x23,3,0,0x12});assert(only.app.configured==0);
}
static void backpressure_and_generation() {
 Fixture f;bth_control_reservation_t held[3];for(auto& r:held)assert(bth_control_tx_reserve(&f.q,0,&r)==BTH_OK);
 const uint8_t cfg[]={0x10,3,4,8,1,0,7,6,0,0,0x21,0x15,2,53};assert(!f.profiles.receive(0x40,cfg,sizeof cfg,true,5));assert(f.app.configured==0);
 for(auto& r:held)assert(bth_control_tx_cancel(&f.q,r.token)==BTH_OK);f.configured();
 assert(bth_control_tx_detach(&f.q,0,1));assert(bth_control_tx_attach(&f.q,0,0x25,2));
 assert(f.profiles.receive(0x40,cfg,sizeof cfg,true,7));assert(f.app.configured==1); // stale module generation cannot mutate new queue owner
 f.profiles.reset(2);assert(!f.profiles.configured() && !f.profiles.signalCid());
}
static void sdp_and_avrcp_volume() {
 Fixture f;assert(f.profiles.channelOpened(1,0x42,0x82,672));
 f.rx({2,0,1,0,8,0x35,3,0x19,0x11,0x0b,0,4,0},0x42);assert(f.pop(0x82)==std::vector<uint8_t>({3,0,1,0,9,0,1,0,1,0,1,0,1,0}));
 assert(f.profiles.channelOpened(0x17,0x43,0x83,672));assert(!f.profiles.channelOpened(0x17,0x44,0x84,672));
 f.rx({0x10,0x11,0x0e,0,0x48,0,0,0x19,0x58,0x50,0,0,1,64},0x43);auto response=f.pop(0x83);assert(response[3]==9&&response.back()==64&&f.app.volume==64&&f.app.volumes==1);
 f.rx({0x20,0x11,0x0e,0,0x48,0x7c,0x41,0},0x43);response=f.pop(0x83);assert(response[3]==9&&f.app.volume==72&&f.app.volumes==2);
 f.rx({0x30,0x11,0x0e,0,0x48,0x7c,0xc1,0},0x43);f.pop(0x83);assert(f.app.volume==72&&f.app.volumes==2);
 assert(f.profiles.setVolume(127)==Status::Ok&&f.app.volume==127&&f.app.volumes==3);assert(f.profiles.setVolume(127)==Status::Ok&&f.app.volumes==3);assert(f.profiles.setVolume(128)==Status::InvalidArgument);
}
static std::vector<uint8_t> vendorResponse(const std::vector<uint8_t>& request,uint8_t pdu,const std::vector<uint8_t>& data,uint8_t packet=0,uint8_t ctype=12) {
 std::vector<uint8_t> p={uint8_t((request[0]&0xf0)|2),0x11,0x0e,ctype,0x48,0,0,0x19,0x58,pdu,packet,uint8_t(data.size()>>8),uint8_t(data.size())};p.insert(p.end(),data.begin(),data.end());return p;
}
static void avrcp_metadata_and_status() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));
 f.profiles.poll(true,20);auto caps=f.pop(0x83);assert(caps[9]==0x10);
 f.rx(vendorResponse(caps,0x10,{3,2,1,2}),0x43,true,21);
 f.profiles.poll(true,22);auto status=f.pop(0x83);assert(status[9]==0x30);
 f.rx(vendorResponse(status,0x30,{0,0,0x13,0x88,0,0,0,100,1}),0x43,true,23);
 assert(f.profiles.avrcpStatus().song_length_ms==5000&&f.profiles.avrcpStatus().song_position_ms==100&&f.profiles.avrcpStatus().playback_status==1);
 f.profiles.poll(true,24);auto request=f.pop(0x83);assert(request[9]==0x20&&request[21]==4);
 std::vector<uint8_t> attrs={2,0,0,0,1,0,106,0,5,'T','i','t','l','e',0,0,0,2,0,106,0,6,'A','r','t','i','s','t'};
 f.rx(vendorResponse(request,0x20,attrs),0x43,true,25);assert(f.app.metadata.size()==2&&f.app.metadata[0].first==1&&f.app.metadata[0].second=="Title"&&f.app.metadata[1].second=="Artist");
 // A received playback notification makes an older status snapshot obsolete.
 // Answer the explicit legal refresh before requiring the track registration;
 // do not index registration parameters in a13-byte GetPlayStatus request.
 f.profiles.poll(true,26);auto notification=f.pop(0x83);assert(notification[9]==0x31&&notification[13]==1);
 f.rx(vendorResponse(notification,0x31,{1,1},0,15),0x43,true,27);
 f.profiles.poll(true,28);auto refresh=f.pop(0x83);assert(refresh.size()==13&&refresh[9]==0x30);
 f.rx(vendorResponse(refresh,0x30,{0,0,0x13,0x88,0,0,0,120,1}),0x43,true,29);
 f.profiles.poll(true,30);auto track=f.pop(0x83);assert(track.size()==18&&track[9]==0x31&&track[13]==2);
 f.rx(vendorResponse(track,0x31,{2,0,0,0,0,0,0,0,1},0,15),0x43,true,31);
 f.rx(vendorResponse(track,0x31,{2,0,0,0,0,0,0,0,2},0,13),0x43,true,32);
 f.profiles.poll(true,33);status=f.pop(0x83);assert(status[9]==0x30);f.rx(vendorResponse(status,0x30,{0,0,0,10,0,0,0,1,1}),0x43,true,34);
 f.profiles.poll(true,35);request=f.pop(0x83);assert(request[9]==0x20);
 // AVRCP vendor continuation has its own transactions and bounded assembly.
 std::vector<uint8_t> first(attrs.begin(),attrs.begin()+12),last(attrs.begin()+12,attrs.end());
 f.rx(vendorResponse(request,0x20,first,1),0x43,true,36);assert(f.app.metadata.size()==2);
 f.profiles.poll(true,37);auto continuation=f.pop(0x83);assert(continuation[9]==0x40&&continuation[13]==0x20);
 f.rx(vendorResponse(continuation,0x20,last,3),0x43,true,38);assert(f.app.metadata.size()==4);
}
static void custom_labels_are_not_duplicated_by_track_change() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));
 f.profiles.poll(true,1);auto caps=f.pop(0x83);f.rx(vendorResponse(caps,0x10,{3,1,2}),0x43,true,2);
 f.profiles.poll(true,3);auto status=f.pop(0x83);f.rx(vendorResponse(status,0x30,{0,0,0,1,0,0,0,0,1}),0x43,true,4);
 f.profiles.poll(true,5);auto request=f.pop(0x83);assert(request[9]==0x20);
 f.profiles.poll(true,6);auto track=f.pop(0x83);assert(track[9]==0x31&&track[13]==2);
 f.rx(vendorResponse(track,0x31,{2,0,0,0,0,0,0,0,1},0,15),0x43,true,7);
 f.rx(vendorResponse(track,0x31,{2,0,0,0,0,0,0,0,2},0,13),0x43,true,8);
 f.profiles.poll(true,9);status=f.pop(0x83);assert(status[9]==0x30);f.rx(vendorResponse(status,0x30,{0,0,0,1,0,0,0,0,1}),0x43,true,10);
 f.profiles.poll(true,11);assert(f.q.peers[0].count==0); // Existing metadata transaction must finish first.
 f.rx(vendorResponse(request,0x20,{1,0,0,0,1,0,106,0,1,'A'}),0x43,true,12);
 f.profiles.poll(true,13);request=f.pop(0x83);assert(request[9]==0x20); // Retained track work can now send.
}
static void malformed_fragment_retires_assembly() {
 Fixture f;f.rx({0x14,2,3,4,8,1,0});assert(f.q.peers[0].count==0);
 f.rx({0x1c,7,6,0,0,0x21,0x15,2,53});assert(f.q.peers[0].count==0&&f.app.configured==0);
 f.rx({0x24,2,3,4,8,1,0,7,6,0,0});assert(f.q.peers[0].count==0);
 f.rx({0x2c,0x21,0x15,2,53});assert(f.pop()==std::vector<uint8_t>({0x22,3})&&f.app.configured==1);
}
static void custom_response_requires_matching_pdu_even_for_reject() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));f.profiles.poll(true,1);auto caps=f.pop(0x83);
 f.rx(vendorResponse(caps,0x10,{3,0}),0x43,true,2);f.profiles.poll(true,3);auto status=f.pop(0x83);
 f.rx(vendorResponse(status,0x20,{1},0,10),0x43,true,4);
 f.rx(vendorResponse(status,0x30,{0,0,0,77,0,0,0,5,1}),0x43,true,5);
 assert(f.profiles.avrcpStatus().song_length_ms==77&&f.profiles.avrcpStatus().song_position_ms==5);
}
static void all_endpoint_formats_are_bound_to_their_seid() {
 struct Selection {uint8_t seid,type;std::vector<uint8_t> cie;Codec codec;uint32_t rate;};
 const Selection selections[]={
  {1,0,{0x21,0x15,2,53},Codec::Sbc,44100},
  {2,2,{0x40,0,0x84,0x80,0,0},Codec::Aac,48000},
  {3,2,{8,0,0x84,0x80,0,0},Codec::AacHe,48000},
  {4,255,{0x4f,0,0,0,1,0,0x12},Codec::Aptx,48000},
  {5,255,{0xd7,0,0,0,0x24,0,0x12,0,0,0,0},Codec::AptxHd,48000},
  {6,255,{0x2d,1,0,0,0xaa,0,4,1},Codec::Ldac,96000},
  {7,255,{0x3a,5,0,0,0x35,0x4c,1,2,0x11,0,0},Codec::LhdcV5,192000},
  {8,255,{0xe0,0,0,0,1,0,0x92},Codec::Opus,48000},
  {9,255,{0xf1,5,0,0,5,0x10,2,1,3,0,0,0,8,0,0,0,0,0,0,0,0,0,0,0},Codec::Opus,48000}
 };
 for(const auto& s:selections){
  Fixture f;std::vector<uint8_t> packet={0x10,3,uint8_t(s.seid<<2),40,1,0,7,uint8_t(s.cie.size()+2),0,s.type};packet.insert(packet.end(),s.cie.begin(),s.cie.end());
  f.rx(packet);assert(f.pop()==std::vector<uint8_t>({0x12,3}));assert(f.app.config.codec==s.codec&&f.app.config.sample_rate==s.rate&&f.app.config.codec_info_size==s.cie.size());
  assert(!memcmp(f.app.config.codec_info,s.cie.data(),s.cie.size()));
 }
 Fixture mismatch;mismatch.expect({0x10,3,8,12,1,0,7,8,0,2,8,0,0x84,0x80,0,0},{0x13,3,7,0x29});assert(mismatch.app.configured==0);
 mismatch.expect({0x20,3,32,12,1,0,7,26,0,255,0xf1,5,0,0,5,0x10,2,1,3,0,0,0,8,0,0,0,0,0,0,0,0,0,0,0},{0x23,3,7,0x29});assert(mismatch.app.configured==0);
}
static void avctp_fragmented_status_and_utf8_metadata_bounds() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));f.profiles.poll(true,1);auto caps=f.pop(0x83);f.rx(vendorResponse(caps,0x10,{3,0}),0x43,true,2);
 f.profiles.poll(true,3);auto request=f.pop(0x83);auto reply=vendorResponse(request,0x30,{0,0,0,77,0,0,0,5,1});
 std::vector<uint8_t> first={uint8_t((reply[0]&0xf0)|6),2,0x11,0x0e};first.insert(first.end(),reply.begin()+3,reply.begin()+12);
 std::vector<uint8_t> last={uint8_t((reply[0]&0xf0)|14)};last.insert(last.end(),reply.begin()+12,reply.end());
 f.rx(first,0x43,true,4);assert(f.profiles.avrcpStatus().song_length_ms==0xffffffffu);f.rx(last,0x43,true,5);assert(f.profiles.avrcpStatus().song_length_ms==77);
 f.profiles.poll(true,6);request=f.pop(0x83);std::vector<uint8_t> attrs={1,0,0,0,1,0,106,0,130};attrs.insert(attrs.end(),126,'a');attrs.insert(attrs.end(),{0xe2,0x82,0xac,'z'});
 f.rx(vendorResponse(request,0x20,attrs),0x43,true,7);assert(f.app.metadata.size()==1&&f.app.metadata[0].second==std::string(126,'a'));
}
static void unrelated_timeout_preserves_metadata_continuation() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));f.profiles.poll(true,1);f.pop(0x83); // Leave capabilities pending.
 f.profiles.poll(true,2);auto status=f.pop(0x83);f.rx(vendorResponse(status,0x30,{0,0,0,1,0,0,0,0,1}),0x43,true,3);
 f.profiles.poll(true,4);auto request=f.pop(0x83);f.rx(vendorResponse(request,0x20,{1,0,0,0,1},1),0x43,true,2999);
 f.profiles.poll(true,3000);auto continuation=f.pop(0x83);assert(continuation[9]==0x40);
 f.profiles.poll(true,3002);assert(f.q.peers[0].count==0);
 f.rx(vendorResponse(continuation,0x20,{0,106,0,1,'A'},3),0x43,true,3003);assert(f.app.metadata.size()==1&&f.app.metadata[0].second=="A");
}
static void blocked_metadata_continuation_expires_to_abort() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));f.profiles.poll(true,1);auto caps=f.pop(0x83);f.rx(vendorResponse(caps,0x10,{3,0}),0x43,true,2);
 f.profiles.poll(true,3);auto status=f.pop(0x83);f.rx(vendorResponse(status,0x30,{0,0,0,1,0,0,0,0,1}),0x43,true,4);
 f.profiles.poll(true,5);auto request=f.pop(0x83);f.rx(vendorResponse(request,0x20,{1,0,0,0,1},1),0x43,true,6);
 bth_control_reservation_t held[3];for(auto& r:held)assert(bth_control_tx_reserve(&f.q,0,&r)==BTH_OK);
 f.profiles.poll(true,4000);for(auto& r:held)assert(bth_control_tx_cancel(&f.q,r.token)==BTH_OK);
 f.profiles.poll(true,4001);auto abort=f.pop(0x83);assert(abort[9]==0x41&&abort[13]==0x20);
}
static void malformed_avrcp_fragment_retires_assembly() {
 Fixture f;assert(f.profiles.channelOpened(0x17,0x43,0x83,672));
 f.rx({0x14,2,0x11,0x0e,0,0x48,0,0,0x19,0x58},0x43,true,100);
 assert(f.app.volumes==0);
 f.rx({0x2c,0},0x43,true,101); // A mismatched END retires the old START.
 f.rx({0x1c,0x50,0,0,1,64},0x43,true,102);
 assert(f.app.volumes==0 && f.q.peers[0].count==0);
 Fixture body;assert(body.profiles.channelOpened(0x17,0x43,0x83,672));
 body.rx({0x14,2,0x11,0x0e,0,0x48,0,0,0x19,0x58},0x43,true,100);
 body.rx({0x1c,0x50,0,0,2,64},0x43,true,101); // Complete body has a bad declared length.
 body.rx({0x1c,0x50,0,0,1,64},0x43,true,102);
 assert(body.app.volumes==0 && body.q.peers[0].count==0);
}
int main(int argc,char** argv) {
 if(argc==2){
  struct Case{const char* name;void(*run)();};
  const Case cases[]={
   {"metadata_status",avrcp_metadata_and_status},
   {"metadata_labels",custom_labels_are_not_duplicated_by_track_change},
   {"metadata_bounds",avctp_fragmented_status_and_utf8_metadata_bounds},
   {"metadata_unrelated_timeout",unrelated_timeout_preserves_metadata_continuation},
   {"metadata_blocked_abort",blocked_metadata_continuation_expires_to_abort},
   {"metadata_fragment_retirement",malformed_avrcp_fragment_retires_assembly}};
  for(const auto& c:cases)if(!strcmp(argv[1],c.name)){c.run();return 0;}
  return 2;
 }
 malformed_avrcp_fragment_retires_assembly();
 configuration_transaction_and_lifecycle();validation_and_reconfigure();reconfigure_and_disabled_endpoint();backpressure_and_generation();sdp_and_avrcp_volume();avrcp_metadata_and_status();custom_labels_are_not_duplicated_by_track_change();malformed_fragment_retires_assembly();custom_response_requires_matching_pdu_even_for_reject();all_endpoint_formats_are_bound_to_their_seid();avctp_fragmented_status_and_utf8_metadata_bounds();
 unrelated_timeout_preserves_metadata_continuation();blocked_metadata_continuation_expires_to_abort();
 puts("PASS owned Classic transactions, lifecycle, 9 endpoints, queues, SDP, volume, status and bounded metadata");
}
