// Real OwnedHost actor + HID Device: SDP/EIR/CoD, L2CAP 0x11/0x13 admission
// and security gating, HIDP control/interrupt transactions, report queueing
// behind Classic control, disconnect/unplug lifecycle. Synthetic HCI only.
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#include "../src/owned/OwnedHost.cpp"
#include "../src/GigaHidDescriptors.h"
#define main original_host_fixture_main
#include "test_owned_host.cpp"
#undef main
static const Bytes everything={6,0,9,0,15,0x35,3,0x19,0x10,0x02,0xff,0xff,0x35,5,0x0a,0,0,0xff,0xff,0};
static Bytes search(uint16_t uuid){return {2,0,1,0,8,0x35,3,0x19,uint8_t(uuid>>8),uint8_t(uuid),0,4,0};}
static Bytes sdpExchange(Fixture& f,uint16_t cid,uint16_t remote,const Bytes& request){
 f.output.clear();f.acl(0x25,cid,request);f.settle();for(const auto& p:f.output)if(p.cid==remote)return p.bytes;CHECK(false);return {};
}
static Bytes reference(const bth_sdp_record_t* extra,size_t extras){
 size_t count=0;const auto* speaker=bth_sdp_speaker_records(&count);CHECK(count==3);
 bth_sdp_record_t records[8]={speaker[0],speaker[2]};size_t n=2;for(size_t i=0;i<extras;++i)records[n++]=extra[i]; // Fixture: no volume callback.
 bth_sdp_t server{};CHECK(bth_sdp_init(&server,records,n,77));bth_sdp_cursor_t cursor{};uint8_t out[672];size_t written=0;
 CHECK(bth_sdp_process_compact(&server,&cursor,everything.data(),everything.size(),out,sizeof out,&written)==BTH_OK);return Bytes(out,out+written);
}
// Connection response for the most recent request: {result, local cid}.
static std::pair<uint16_t,uint16_t> connectResult(const Fixture& f){
 std::pair<uint16_t,uint16_t> r{0xffff,0};
 for(const auto& p:f.output)if(p.cid==1)for(size_t at=0;at+4<=p.bytes.size();at+=size_t(bth_read_le16(p.bytes.data()+at+2))+4)
  if(p.bytes[at]==3&&p.bytes.size()>=at+12)r={bth_read_le16(p.bytes.data()+at+8),bth_read_le16(p.bytes.data()+at+4)};
 return r;
}
static HidConfig comboConfig(){HidConfig c;c.enabled=true;c.report_descriptor=hid_descriptors::KeyboardMouseCombo;c.report_descriptor_bytes=sizeof hid_descriptors::KeyboardMouseCombo;c.subclass=HidSubclassCombo;c.boot_keyboard=c.boot_mouse=true;c.description="Keyboard/Mouse";return c;}
struct HidPeer {
 HidDevice hid;Fixture f;uint16_t control=0,interrupt=0;
 explicit HidPeer(const HidConfig& c=comboConfig(),bool connect=true){
  CHECK(hid.begin(c));f.host.end();f.config.hid=&hid;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();
  if(connect){f.classicConnect();open();}
 }
 ~HidPeer(){f.host.end();}
 void open(uint16_t remote_interrupt=0x71){control=f.channel(0x11,0x70);CHECK(!hid.connected());interrupt=f.channel(0x13,remote_interrupt);CHECK(hid.connected());f.output.clear();}
 // One control transaction: the single reply on the host's control CID (or empty).
 Bytes ask(const Bytes& request){f.output.clear();f.acl(0x25,control,request);f.settle();Bytes r;unsigned n=0;for(const auto& p:f.output)if(p.cid==0x70){r=p.bytes;++n;}CHECK(n<=1);return r;}
 std::vector<Bytes> reports(uint16_t remote=0x71)const{std::vector<Bytes> r;for(const auto& p:f.output)if(p.cid==remote)r.push_back(p.bytes);return r;}
};
static void default_disabled_identical(){
 Fixture f;f.boot();
 CHECK(f.host.classic_.record_count_==2&&f.host.classic_.hid_record_count_==0&&f.host.gap_.config.uuid_count==1&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x110c);
 CHECK(f.host.gap_.config.class_of_device==0x240414&&!f.host.config_.hid);
 f.classicConnect();CHECK(f.host.signaling_.service_count==3);
 f.output.clear();f.acl(0x25,1,{2,7,4,0,0x11,0,0x70,0});f.settle();const auto refused=connectResult(f);CHECK(refused.first==2);  // PSM not supported.
 const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference(nullptr,0));
 CHECK(sdpExchange(f,sdp,0x81,search(0x1124))==Bytes({3,0,1,0,5,0,0,0,0,0}));
 CHECK(sdpExchange(f,sdp,0x81,search(0x1200))==Bytes({3,0,1,0,5,0,0,0,0,0}));
}
static void enabled_sdp_eir_cod(){
 {HidPeer p(comboConfig(),false);auto& f=p.f;
  CHECK(f.host.classic_.record_count_==4&&f.host.classic_.hid_record_count_==2&&f.host.gap_.config.uuid_count==2&&f.host.uuids_[0]==0x110b&&f.host.uuids_[1]==0x1124);
  CHECK(f.host.gap_.config.class_of_device==0x240414); // Default keeps the audio class of device.
  f.classicConnect();CHECK(f.host.signaling_.service_count==5);
  const uint16_t sdp=f.channel(1,0x81);CHECK(sdpExchange(f,sdp,0x81,everything)==reference(p.hid.records(),p.hid.recordCount()));
  CHECK(sdpExchange(f,sdp,0x81,search(0x1124))==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,7,0}));
  CHECK(sdpExchange(f,sdp,0x81,search(0x1200))==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,8,0}));
  CHECK(sdpExchange(f,sdp,0x81,search(0x110b))==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,1,0})); // A2DP sink unchanged.
 }
 // Peripheral class of device override; Device ID record optional.
 HidConfig c=comboConfig();c.class_of_device=HidClassOfDeviceCombo;c.device_id=false;
 {HidPeer p(c,false);CHECK(p.f.host.gap_.config.class_of_device==0x2405c0&&p.f.host.classic_.record_count_==3);}
 // The controller actually receives that class of device and the EIR UUID
 // list {0x110B, 0x1124}; disabled HID keeps 0x240414 and {0x110B}.
 for(const bool enabled:{false,true}){
  HidDevice hid;CHECK(hid.begin(c));Fixture g;g.host.end();g.config.hid=enabled?&hid:nullptr;CHECK(g.host.begin(g.config,g.hooks)==Status::Ok);
  Bytes cod,eir;unsigned count=0;
  while(!g.host.radioStats().controller_ready&&count++<1400){g.poll();if(!g.tx.empty()){const Bytes packet=g.pop();const uint16_t opcode=Fixture::op(packet);
   if(opcode==0x0c24)cod.assign(packet.begin()+4,packet.end());if(opcode==0x0c52)eir.assign(packet.begin()+4,packet.end());g.complete(opcode,g.response(opcode));}}
  CHECK(g.host.radioStats().controller_ready&&cod==(enabled?Bytes({0xc0,0x05,0x24}):Bytes({0x14,0x04,0x24}))&&eir.size()==241);
  const Bytes uuids=enabled?Bytes({0x05,0x03,0x0b,0x11,0x24,0x11}):Bytes({0x03,0x03,0x0b,0x11});
  CHECK(std::search(eir.begin(),eir.end(),uuids.begin(),uuids.end())!=eir.end());
  g.host.end();
 }
 // The Classic actor is required; a disabled endpoint is refused.
 HidDevice idle;Fixture f;f.host.end();f.config.hid=&idle;CHECK(f.host.begin(f.config,f.hooks)==Status::InvalidArgument);
 CHECK(idle.begin(comboConfig()));f.config.callbacks.enable_a2dp=false;CHECK(f.host.begin(f.config,f.hooks)==Status::Unsupported);
}
static void security_gating(){
 HidPeer p(comboConfig(),false);auto& f=p.f;
 f.event(4,{1,2,3,4,5,6,0,0,0,1});f.settle();f.event(3,{0,0x25,0,1,2,3,4,5,6,1,0});f.settle();
 // Unencrypted link: the secure HID service waits; nothing is admitted.
 f.output.clear();f.acl(0x25,1,{2,9,4,0,0x11,0,0x70,0});f.settle();
 const auto waiting=connectResult(f);CHECK(waiting.first!=0&&!p.hid.linked());
 f.output.clear();
 f.event(0x17,{1,2,3,4,5,6});f.settle();f.event(0x31,{1,2,3,4,5,6});f.settle();f.event(0x33,{1,2,3,4,5,6,1,0,0,0});f.settle();
 Bytes key={1,2,3,4,5,6};key.insert(key.end(),16,0xaa);key.push_back(4);f.event(0x18,key);f.event(6,{0,0x25,0});f.settle();f.event(8,{0,0x25,0,1});f.settle();
 const auto ready=connectResult(f);CHECK(ready.first==0&&ready.second>=0x40);
 uint8_t id=0;for(const auto& q:f.output)if(q.cid==1)for(size_t at=0;at+4<=q.bytes.size();at+=size_t(bth_read_le16(q.bytes.data()+at+2))+4)if(q.bytes[at]==4)id=q.bytes[at+1];
 CHECK(id);const uint16_t local=ready.second;
 f.acl(0x25,1,{5,id,6,0,uint8_t(local),uint8_t(local>>8),0,0,0,0});f.settle();f.acl(0x25,1,{4,8,8,0,uint8_t(local),uint8_t(local>>8),0,0,1,2,0xa0,2});f.settle();
 CHECK(p.hid.controlCid()==local&&!p.hid.connected());p.control=local;
 p.interrupt=f.channel(0x13,0x71);CHECK(p.hid.connected());
 // Encryption off: HID admission retires in the same turn and the ACL closes.
 const uint8_t report[8]={0,0,4};CHECK(p.hid.send(1,report,8)==Status::Ok);
 f.event(8,{0,0x25,0,0});f.settle();
 CHECK(!p.hid.connected()&&!p.hid.linked()&&p.hid.send(1,report,8)==Status::InvalidArgument&&f.host.links_.classic.disconnecting);
}
static void hidp_transactions(){
 HidPeer p;auto& f=p.f;
 CHECK(p.ask({0x60})==Bytes({0xa0,0x01}));                      // GET_PROTOCOL: report.
 CHECK(p.ask({0x41,0x01})==Bytes({0xa1,0x01,0,0,0,0,0,0,0,0}));  // GET_REPORT input kb: zeros until sent.
 CHECK(p.ask({0x41,0x02})==Bytes({0xa1,0x02,0,0,0,0}));
 CHECK(p.ask({0x42,0x01})==Bytes({0xa2,0x01,0}));                // Output (LED) report.
 CHECK(p.ask({0x41,0x09})==Bytes({0x02}));                       // Unknown report ID.
 CHECK(p.ask({0x43,0x01})==Bytes({0x02}));                       // No feature report.
 CHECK(p.ask({0x41})==Bytes({0x04})&&p.ask({0x41,0x01,0x00})==Bytes({0x04})&&p.ask({0x40,0x01})==Bytes({0x04}));
 const uint8_t key[8]={0x02,0,0x0b,0,0,0,0,0};CHECK(p.hid.send(1,key,8)==Status::Ok);f.settle();
 CHECK(p.reports()==std::vector<Bytes>({{0xa1,0x01,0x02,0,0x0b,0,0,0,0,0}}));
 CHECK(p.ask({0x41,0x01})==Bytes({0xa1,0x01,0x02,0,0x0b,0,0,0,0,0}));   // Latest value.
 CHECK(p.ask({0x49,0x01,0x04,0x00})==Bytes({0xa1,0x01,0x02,0,0x0b}));   // BufferSize 4 (ID + 3).
 // SET_REPORT output (Caps Lock) -> handshake + callback copy; invalid forms refused.
 HidReportType type;uint8_t id=0,data[64];size_t length=0;CHECK(!p.hid.takeOutput(type,id,data,length));
 CHECK(p.ask({0x52,0x01,0x02})==Bytes({0x00})&&p.hid.takeOutput(type,id,data,length)&&type==HidReportType::Output&&id==1&&length==1&&data[0]==0x02);
 CHECK(p.ask({0x42,0x01})==Bytes({0xa2,0x01,0x02}));
 CHECK(p.ask({0x52,0x01,0x01,0x02})==Bytes({0x04})&&p.ask({0x52,0x01})==Bytes({0x04})&&p.ask({0x52,0x07,0x01})==Bytes({0x02}));
 CHECK(p.ask({0x51,0x01,0,0,0,0,0,0,0,0})==Bytes({0x04})&&p.ask({0x53,0x01,0x00})==Bytes({0x02}));
 // Interrupt-channel output DATA reaches the same callback without a reply.
 f.output.clear();f.acl(0x25,p.interrupt,{0xa2,0x01,0x01});f.settle();CHECK(p.reports().empty()&&p.hid.takeOutput(type,id,data,length)&&id==1&&data[0]==0x01);
 // Overwritten before delivery is counted.
 f.acl(0x25,p.interrupt,{0xa2,0x01,0x03});f.acl(0x25,p.interrupt,{0xa2,0x01,0x05});f.settle();
 CHECK(p.hid.takeOutput(type,id,data,length)&&data[0]==0x05&&p.hid.stats().output_overwritten==1);
 // Unsupported and deprecated requests, HID_CONTROL without replies.
 CHECK(p.ask({0x20})==Bytes({0x03})&&p.ask({0xb0})==Bytes({0x03})&&p.ask({0xf0})==Bytes({0x03}));
 CHECK(p.ask({0x90,0x00})==Bytes({0x00})&&p.ask({0x80})==Bytes({0xa0,0x00}));
 CHECK(p.ask({0x00}).empty()&&p.ask({0x13}).empty()&&p.hid.stats().suspended&&p.hid.stats().suspends==1);
 CHECK(p.hid.send(1,key,8)==Status::Ok);f.settle();CHECK(p.reports().size()==1); // A suspended host may be woken by input.
 CHECK(p.ask({0x14}).empty()&&!p.hid.stats().suspended&&p.ask({0x10}).empty());
 // Boot protocol: boot-layout reports with boot IDs; other reports filtered.
 CHECK(p.ask({0x70})==Bytes({0x00})&&p.ask({0x60})==Bytes({0xa0,0x00})&&p.hid.stats().boot_protocol&&p.hid.stats().protocol_changes==1);
 const uint8_t mouse[4]={0x01,0x05,0xfb,0x01},consumer[2]={0xcd,0x00};
 f.output.clear();CHECK(p.hid.send(1,key,8)==Status::Ok&&p.hid.send(2,mouse,4)==Status::Ok&&p.hid.send(3,consumer,2)==Status::Ok);f.settle();
 CHECK(p.reports()==std::vector<Bytes>({{0xa1,0x01,0x02,0,0x0b,0,0,0,0,0},{0xa1,0x02,0x01,0x05,0xfb}})&&p.hid.stats().reports_filtered==1);
 CHECK(p.ask({0x41,0x02})==Bytes({0xa1,0x02,0x01,0x05,0xfb})&&p.ask({0x41,0x03})==Bytes({0x02}));
 CHECK(p.ask({0x52,0x01,0x04})==Bytes({0x00})&&p.hid.takeOutput(type,id,data,length)&&id==1&&data[0]==0x04);
 CHECK(p.ask({0x71})==Bytes({0x00})&&!p.hid.stats().boot_protocol);
 // Report protocol frames for every report ID.
 f.output.clear();CHECK(p.hid.send(2,mouse,4)==Status::Ok&&p.hid.send(3,consumer,2)==Status::Ok);f.settle();
 CHECK(p.reports()==std::vector<Bytes>({{0xa1,0x02,0x01,0x05,0xfb,0x01},{0xa1,0x03,0xcd,0x00}}));
 CHECK(p.ask({0x72})==Bytes({0x04})&&p.ask({0x70,0x00})==Bytes({0x04})&&p.ask({0x61})==Bytes({0x04}));
 // Hard reset returns to report protocol.
 CHECK(p.ask({0x70})==Bytes({0x00})&&p.ask({0x11}).empty()&&p.ask({0x60})==Bytes({0xa0,0x01}));
 const auto s=p.hid.stats();CHECK(s.get_reports>=10&&s.set_reports>=6&&s.error_handshakes>=10&&s.unsupported_requests==3&&s.malformed==0);
 // A descriptor without boot support refuses SET_PROTOCOL(boot).
 HidConfig plain=comboConfig();plain.boot_keyboard=plain.boot_mouse=false;HidPeer q(plain);CHECK(q.ask({0x70})==Bytes({0x04})&&q.ask({0x60})==Bytes({0xa0,0x01}));
 // Report-ID-less keyboard: no ID byte in report protocol, ID 1 in boot.
 HidConfig kb;kb.enabled=true;kb.report_descriptor=hid_descriptors::BootKeyboard;kb.report_descriptor_bytes=sizeof hid_descriptors::BootKeyboard;kb.boot_keyboard=true;
 HidPeer k(kb);CHECK(k.hid.send(0,key,8)==Status::Ok&&k.hid.send(1,key,8)==Status::InvalidArgument);k.f.settle();
 CHECK(k.reports()==std::vector<Bytes>({{0xa1,0x02,0,0x0b,0,0,0,0,0}})&&k.ask({0x41})==Bytes({0xa1,0x02,0,0x0b,0,0,0,0,0})&&k.ask({0x52,0x02})==Bytes({0x00}));
 CHECK(k.ask({0x70})==Bytes({0x00}));k.f.output.clear();CHECK(k.hid.send(0,key,8)==Status::Ok);k.f.settle();
 CHECK(k.reports()==std::vector<Bytes>({{0xa1,0x01,0x02,0,0x0b,0,0,0,0,0}})&&k.ask({0x41,0x01})==Bytes({0xa1,0x01,0x02,0,0x0b,0,0,0,0,0}));
}
static void queue_and_classic_priority(){
 HidPeer p;auto& f=p.f;uint8_t key[8]={};
 // Eight queued reports, ninth OutputFull; all leave in order, one per turn.
 for(unsigned i=0;i<8;++i){key[2]=uint8_t(4+i);CHECK(p.hid.send(1,key,8)==Status::Ok);}
 CHECK(p.hid.send(1,key,8)==Status::OutputFull&&p.hid.stats().reports_rejected==1&&p.hid.stats().tx_pending==8);
 f.poll();CHECK(p.hid.stats().tx_pending==7);f.settle();
 const auto sent=p.reports();CHECK(sent.size()==8&&p.hid.stats().reports_sent==8&&p.hid.stats().tx_pending==0);
 for(unsigned i=0;i<8;++i)CHECK(sent[i].size()==10&&sent[i][4]==4+i);
 // A report enters the shared Classic FIFO only with all three peer slots
 // free; a control reply needs two. Other Classic replies never wait behind it.
 bth_control_reservation_t hold{};CHECK(bth_control_tx_reserve(&f.host.control_,0,&hold)==BTH_OK);
 f.output.clear();CHECK(p.hid.send(1,key,8)==Status::Ok);f.settle();CHECK(p.reports().empty()&&p.hid.stats().tx_pending==1);
 CHECK(p.ask({0x60})==Bytes({0xa0,0x01})&&p.reports().empty());
 bth_control_reservation_t second{};CHECK(bth_control_tx_reserve(&f.host.control_,0,&second)==BTH_OK);
 f.output.clear();f.acl(0x25,p.control,{0x60});f.settle();CHECK(p.reports().empty());bool replied=false;for(const auto& q:f.output)replied|=q.cid==0x70;CHECK(!replied);
 CHECK(bth_control_tx_cancel(&f.host.control_,second.token)==BTH_OK);f.settle();replied=false;for(const auto& q:f.output)replied|=q.cid==0x70;CHECK(replied&&p.reports().empty());
 CHECK(bth_control_tx_cancel(&f.host.control_,hold.token)==BTH_OK);f.output.clear();f.settle();CHECK(p.reports().size()==1&&p.hid.stats().tx_pending==0);
 // SDP and HID traffic interleave on one link; A2DP streaming leaves HID admitted.
 const uint16_t sdp=f.channel(1,0x81);f.host.classic_.playing_=true;
 f.output.clear();CHECK(p.hid.send(1,key,8)==Status::Ok);f.acl(0x25,sdp,search(0x1124));f.settle();
 CHECK(p.reports().size()==1);bool answered=false;for(const auto& q:f.output)answered|=q.cid==0x81;CHECK(answered);
 f.host.classic_.playing_=false;
}
static void lifecycle(){
 HidPeer p;auto& f=p.f;const uint8_t key[8]={0,0,4};
 // Host closes the interrupt channel: disconnected, control kept, reopen.
 CHECK(p.hid.send(1,key,8)==Status::Ok);
 f.output.clear();f.acl(0x25,1,{6,0x21,4,0,uint8_t(p.interrupt),uint8_t(p.interrupt>>8),0x71,0});f.settle();
 CHECK(!p.hid.connected()&&p.hid.controlCid()==p.control&&!p.hid.interruptCid()&&p.hid.stats().reports_dropped+p.hid.stats().reports_sent==1);
 CHECK(p.hid.send(1,key,8)==Status::InvalidArgument);
 p.interrupt=f.channel(0x13,0x72);CHECK(p.hid.connected()&&p.hid.stats().generation==2&&p.hid.stats().connections==2);
 // Host closes control first: the device retires the interrupt channel.
 f.output.clear();f.acl(0x25,1,{6,0x22,4,0,uint8_t(p.control),uint8_t(p.control>>8),0x70,0});f.settle();
 uint8_t id=0;for(const auto& q:f.output)if(q.cid==1)for(size_t at=0;at+4<=q.bytes.size();at+=size_t(bth_read_le16(q.bytes.data()+at+2))+4)
  if(q.bytes[at]==6&&bth_read_le16(q.bytes.data()+at+4)==0x72&&bth_read_le16(q.bytes.data()+at+6)==p.interrupt)id=q.bytes[at+1];
 CHECK(id&&!p.hid.connected()&&!p.hid.controlCid()&&!p.hid.interruptCid());
 f.acl(0x25,1,{7,id,4,0,0x72,0,uint8_t(p.interrupt),uint8_t(p.interrupt>>8)});f.settle();CHECK(!p.hid.linked()&&!p.hid.closeRequested());
 p.open();CHECK(p.hid.stats().generation==3);
 // A second control channel is refused while one is open.
 f.output.clear();f.acl(0x25,1,{2,7,4,0,0x11,0,0x75,0});f.settle();CHECK(p.hid.controlCid()==p.control);
 // ACL loss: queued reports dropped, endpoint reset, new connection works.
 for(unsigned i=0;i<3;++i)CHECK(p.hid.send(1,key,8)==Status::Ok);
 const uint32_t dropped=p.hid.stats().reports_dropped;f.event(5,{0,0x25,0,0x13});f.settle();
 CHECK(!p.hid.connected()&&!p.hid.linked()&&p.hid.stats().reports_dropped==dropped+3&&p.hid.send(1,key,8)==Status::InvalidArgument);
 f.classicConnect();p.open();CHECK(p.hid.connected()&&p.hid.stats().connections==4);
 f.output.clear();CHECK(p.hid.send(1,key,8)==Status::Ok);f.settle();CHECK(p.reports().size()==1);
}
static void virtual_cable_unplug(){
 HidPeer p;auto& f=p.f;bth_bond_t bond{};
 CHECK(OwnedHost::loadBond(&f.host,f.host.links_.classic.peer,&bond));
 CHECK(p.ask({0x15}).empty());
 CHECK(p.hid.stats().unplugs==1&&!p.hid.connected()&&!p.hid.linked()&&f.host.links_.classic.disconnecting);
 CHECK(!OwnedHost::loadBond(&f.host,f.host.links_.classic.peer,&bond));
}
static void coexistence_channel_limit(){
 // HID beside SPP: six services, five records, three EIR UUIDs, seven channels.
 VoiceProfiles rfcomm;SppPort port;uint8_t rx[512],tx[512];CHECK(port.begin("S",3,rx,512,tx,512,0));
 HidDevice hid;CHECK(hid.begin(comboConfig()));
 Fixture f;f.host.end();f.config.voice_control=&rfcomm;f.config.voice_config=VoiceControlConfig();f.config.voice_config.serial=&port;f.config.hid=&hid;
 CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();
 CHECK(f.host.classic_.record_count_==5&&f.host.gap_.config.uuid_count==3&&f.host.uuids_[1]==0x1101&&f.host.uuids_[2]==0x1124);
 f.classicConnect();CHECK(f.host.signaling_.service_count==6);
 f.channel(1,0x80);f.channel(0x19,0x81);f.channel(3,0x82);f.channel(0x11,0x83);f.channel(0x13,0x84);f.channel(1,0x85);f.channel(1,0x86);
 unsigned open=0;for(const auto& c:f.host.signaling_.channels)open+=c.state==BTH_CHANNEL_OPEN;CHECK(open==7&&hid.connected()&&rfcomm.cid());
 f.host.end();CHECK(!hid.connected()&&!hid.linked());
 // Full table: speaker 3 + SPP + HF + HS + HID + Device ID + OPP + PCE = 10 records.
 ClassicProfiles c;bth_control_tx_t q{};bth_control_tx_init(&q);RadioCallbacks cb;cb.configure=[](const CodecConfig&,void*){return Status::Ok;};cb.volume=[](uint8_t,void*){};
 CHECK(c.begin(codecBit(Codec::Sbc),cb,q)==Status::Ok&&c.installHidRecords(hid.records(),hid.recordCount())==Status::Ok&&c.installHidRecords(hid.records(),1)==Status::InvalidArgument);
 VoiceSdp voice;VoiceControlConfig vc;vc.hands_free=vc.headset=true;CHECK(voice.begin(vc,true));
 CHECK(c.installVoiceRecords(port.record(),1)==Status::Ok&&c.installVoiceRecords(voice.records(),voice.count())==Status::Ok&&c.record_count_==8);
 // OBEX OPP + PBAP PCE occupy the last two non-HID slots (10 total). Limit check
 // only: the reused record's duplicate handle is not a real SDP configuration.
 CHECK(c.installVoiceRecords(port.record(),1)==Status::Ok&&c.installVoiceRecords(port.record(),1)==Status::Ok&&c.record_count_==10);
 CHECK(c.installVoiceRecords(port.record(),1)==Status::InvalidArgument);
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);default_disabled_identical();enabled_sdp_eir_cod();security_gating();hidp_transactions();queue_and_classic_priority();lifecycle();virtual_cable_unplug();coexistence_channel_limit();
 printf("HID_ACTOR_SDP_SECURITY_HIDP_QUEUE_LIFECYCLE_PASS checks=%u (synthetic HCI; no radio)\n",checks);}
