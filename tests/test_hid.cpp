// HID Device endpoint: exact SDP HID/Device ID record bytes, report descriptor
// layouts, configuration validation and channel/queue state. Real SDP server
// and control FIFO; no actor, radio or host.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/owned/HidDevice.h"
#include "../src/GigaHidDescriptors.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;
using Bytes = std::vector<uint8_t>;
static unsigned checks = 0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"CHECK line%d: %s\n",__LINE__,#x);abort();}}while(0)
static Bytes cat(std::initializer_list<Bytes> parts){Bytes out;for(const auto& p:parts)out.insert(out.end(),p.begin(),p.end());return out;}
static Bytes text(const char* s){return Bytes(s,s+strlen(s));}
static HidConfig keyboard(){HidConfig c;c.enabled=true;c.report_descriptor=hid_descriptors::BootKeyboard;c.report_descriptor_bytes=sizeof hid_descriptors::BootKeyboard;c.boot_keyboard=true;return c;}
static HidConfig combo(){HidConfig c;c.enabled=true;c.report_descriptor=hid_descriptors::KeyboardMouseCombo;c.report_descriptor_bytes=sizeof hid_descriptors::KeyboardMouseCombo;c.subclass=HidSubclassCombo;c.boot_keyboard=c.boot_mouse=true;return c;}
static void layouts(){
 HidDevice::Layout l;
 CHECK(sizeof hid_descriptors::BootKeyboard==63&&HidDevice::parseDescriptor(hid_descriptors::BootKeyboard,63,l)&&!l.ids&&l.count==2);
 CHECK(l.find(1,0)&&l.find(1,0)->bits==64&&l.find(2,0)&&l.find(2,0)->bits==8&&!l.find(3,0));
 CHECK(HidDevice::parseDescriptor(hid_descriptors::Mouse,sizeof hid_descriptors::Mouse,l)&&!l.ids&&l.count==1&&l.find(1,0)->bits==32);
 CHECK(HidDevice::parseDescriptor(hid_descriptors::KeyboardMouseCombo,sizeof hid_descriptors::KeyboardMouseCombo,l)&&l.ids&&l.count==4);
 CHECK(l.find(1,1)->bits==64&&l.find(2,1)->bits==8&&l.find(1,2)->bits==32&&l.find(1,3)->bits==16&&!l.find(1,0));
 // Malformed: truncated item, unbalanced collection, reserved item type,
 // unnumbered main item beside report IDs, oversized report, no input report.
 const Bytes truncated={0x05,0x01,0x09},unbalanced={0xa1,0x01,0x75,0x08,0x95,0x01,0x81,0x02},reserved={0x0c,0x75,0x08,0x95,0x01,0x81,0x02};
 const Bytes mixed={0x75,0x08,0x95,0x01,0x81,0x02,0x85,0x01,0x81,0x02},oversized={0x75,0x08,0x95,0x41,0x81,0x02},output_only={0x75,0x08,0x95,0x01,0x91,0x02};
 const Bytes zero_id={0x85,0x00,0x75,0x08,0x95,0x01,0x81,0x02},pop_empty={0xb4,0x75,0x08,0x95,0x01,0x81,0x02};
 for(const auto* b:{&truncated,&unbalanced,&reserved,&mixed,&oversized,&output_only,&zero_id,&pop_empty})CHECK(!HidDevice::parseDescriptor(b->data(),b->size(),l));
 CHECK(!HidDevice::parseDescriptor(nullptr,4,l));
 // Push/pop restore report size/count; long items are skipped; exactly 64 bytes fits.
 const Bytes push={0x75,0x08,0x95,0x02,0xa4,0x75,0x01,0x95,0x08,0x81,0x02,0xb4,0x81,0x02,0xfe,0x02,0x00,0xaa,0xbb,0x95,0x3d,0x81,0x02};
 CHECK(HidDevice::parseDescriptor(push.data(),push.size(),l)&&l.count==1&&l.find(1,0)->bits==8+16+61*8);
}
static void validation(){
 CHECK(HidDevice::validate(keyboard())&&HidDevice::validate(combo()));
 HidConfig c=keyboard();c.report_descriptor=nullptr;CHECK(!HidDevice::validate(c));
 c=keyboard();c.report_descriptor_bytes=0;CHECK(!HidDevice::validate(c));
 c=keyboard();c.service_name=nullptr;CHECK(!HidDevice::validate(c));
 c=keyboard();c.service_name="";CHECK(!HidDevice::validate(c));
 c=keyboard();c.service_name="123456789012345678901234567890123";CHECK(!HidDevice::validate(c));
 c=keyboard();c.service_name="12345678901234567890123456789012";CHECK(HidDevice::validate(c));
 c=keyboard();c.description="";CHECK(!HidDevice::validate(c));c.description=nullptr;CHECK(HidDevice::validate(c));
 c=keyboard();c.provider="";CHECK(!HidDevice::validate(c));c.provider="Arduino";CHECK(HidDevice::validate(c));
 c=keyboard();c.subclass=0x41;CHECK(!HidDevice::validate(c));
 // Boot support needs boot-layout reports at the boot IDs.
 c=keyboard();c.boot_mouse=true;CHECK(!HidDevice::validate(c)); // Keyboard-only: no mouse report.
 c=keyboard();c.report_descriptor=hid_descriptors::Mouse;c.report_descriptor_bytes=sizeof hid_descriptors::Mouse;CHECK(!HidDevice::validate(c));
 c.boot_keyboard=false;c.boot_mouse=true;CHECK(HidDevice::validate(c));
 c=combo();CHECK(HidDevice::validate(c));c.boot_keyboard=c.boot_mouse=false;CHECK(HidDevice::validate(c));
 std::vector<uint8_t> big(513,0);c=keyboard();c.report_descriptor=big.data();c.report_descriptor_bytes=513;CHECK(!HidDevice::validate(c));
}
static Bytes keyboardRecord(){
 const Bytes descriptor(hid_descriptors::BootKeyboard,hid_descriptors::BootKeyboard+63);
 return cat({{0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x07},
  {0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x24},
  {0x09,0x00,0x04,0x35,0x0d,0x35,0x06,0x19,0x01,0x00,0x09,0x00,0x11,0x35,0x03,0x19,0x00,0x11},
  {0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02},
  {0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00},
  {0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x24,0x09,0x01,0x01},
  {0x09,0x00,0x0d,0x35,0x0f,0x35,0x0d,0x35,0x06,0x19,0x01,0x00,0x09,0x00,0x13,0x35,0x03,0x19,0x00,0x11},
  {0x09,0x01,0x00,0x25,0x08},text("GIGA HID"),{0x09,0x01,0x01,0x25,0x08},text("Keyboard"),
  {0x09,0x02,0x01,0x09,0x01,0x11},{0x09,0x02,0x02,0x08,0x40},{0x09,0x02,0x03,0x08,0x00},
  {0x09,0x02,0x04,0x28,0x01},{0x09,0x02,0x05,0x28,0x00},
  {0x09,0x02,0x06,0x35,0x45,0x35,0x43,0x08,0x22,0x25,0x3f},descriptor,
  {0x09,0x02,0x07,0x35,0x08,0x35,0x06,0x09,0x04,0x09,0x09,0x01,0x00},
  {0x09,0x02,0x0d,0x28,0x01},{0x09,0x02,0x0e,0x28,0x01}});
}
static Bytes sdp(const bth_sdp_t& server,const Bytes& request){
 bth_sdp_cursor_t cursor{};uint8_t out[672];size_t n=0;CHECK(bth_sdp_process_compact(&server,&cursor,request.data(),request.size(),out,sizeof out,&n)==BTH_OK);return Bytes(out,out+n);
}
static void records(){
 uint8_t out[HidDevice::RecordBytes];
 const size_t n=HidDevice::buildRecord(out,sizeof out,keyboard());CHECK(n&&Bytes(out,out+n)==keyboardRecord());
 CHECK(HidDevice::buildRecord(out,n-1,keyboard())==0&&HidDevice::buildRecord(nullptr,0,keyboard())==0);
 // Options: provider, country, VC off, reconnect initiate, not connectable, no boot.
 HidConfig c=keyboard();c.provider="Arduino";c.description=nullptr;c.country_code=33;c.virtual_cable=false;c.reconnect_initiate=true;c.normally_connectable=false;c.boot_keyboard=false;
 const size_t m=HidDevice::buildRecord(out,sizeof out,c);const Bytes r(out,out+m);
 Bytes expected=keyboardRecord();
 const Bytes desc={0x09,0x01,0x01,0x25,0x08,'K','e','y','b','o','a','r','d'};const Bytes prov=cat({{0x09,0x01,0x02,0x25,0x07},text("Arduino")});
 auto at=std::search(expected.begin(),expected.end(),desc.begin(),desc.end());CHECK(at!=expected.end());at=expected.erase(at,at+desc.size());expected.insert(at,prov.begin(),prov.end());
 auto patch=[&](const Bytes& from,const Bytes& to){auto p=std::search(expected.begin(),expected.end(),from.begin(),from.end());CHECK(p!=expected.end());std::copy(to.begin(),to.end(),p);};
 patch({0x09,0x02,0x03,0x08,0x00},{0x09,0x02,0x03,0x08,33});patch({0x09,0x02,0x04,0x28,0x01},{0x09,0x02,0x04,0x28,0x00});patch({0x09,0x02,0x05,0x28,0x00},{0x09,0x02,0x05,0x28,0x01});
 patch({0x09,0x02,0x0d,0x28,0x01,0x09,0x02,0x0e,0x28,0x01},{0x09,0x02,0x0d,0x28,0x00,0x09,0x02,0x0e,0x28,0x00});
 CHECK(r==expected);
 // Device ID (PnP Information) record.
 HidConfig d=keyboard();d.vendor_id=0x1234;d.product_id=0x5678;d.version=0x0203;d.vendor_id_source=1;
 uint8_t pnp[HidDevice::DeviceIdBytes];const size_t k=HidDevice::buildDeviceId(pnp,sizeof pnp,d);
 CHECK(Bytes(pnp,pnp+k)==Bytes({0x09,0x00,0x00,0x0a,0x00,0x01,0x00,0x08, 0x09,0x00,0x01,0x35,0x03,0x19,0x12,0x00, 0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,
  0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x12,0x00,0x09,0x01,0x03, 0x09,0x02,0x00,0x09,0x01,0x03, 0x09,0x02,0x01,0x09,0x12,0x34,
  0x09,0x02,0x02,0x09,0x56,0x78, 0x09,0x02,0x03,0x09,0x02,0x03, 0x09,0x02,0x04,0x28,0x01, 0x09,0x02,0x05,0x09,0x00,0x01}));
 // A 300-byte descriptor uses 16-bit sequence/string lengths and stays valid SDP.
 Bytes big;for(unsigned i=0;i<12;++i){const Bytes item={0x85,uint8_t(i+1),0x75,0x08,0x95,0x01,0x81,0x02};big.insert(big.end(),item.begin(),item.end());}
 big.push_back(0xfe);big.push_back(201);big.push_back(0x00);big.resize(300,0x00); // One skipped long item.
 CHECK(big.size()==300);
 c=keyboard();c.boot_keyboard=false;c.report_descriptor=big.data();c.report_descriptor_bytes=uint16_t(big.size());
 HidDevice::Layout l;CHECK(HidDevice::parseDescriptor(big.data(),big.size(),l)&&l.ids&&l.count==12);
 const size_t g=HidDevice::buildRecord(out,sizeof out,c);const Bytes gr(out,out+g);
 const Bytes list={0x09,0x02,0x06,0x36,0x01,0x34,0x36,0x01,0x31,0x08,0x22,0x26,0x01,0x2c};
 auto found=std::search(gr.begin(),gr.end(),list.begin(),list.end());CHECK(found!=gr.end()&&Bytes(found+list.size(),found+list.size()+300)==big);
 // The endpoint publishes both records and the real SDP server serves them.
 HidDevice hid;CHECK(hid.begin(keyboard())&&hid.enabled()&&hid.recordCount()==2&&hid.records()[0].handle==0x10007&&hid.records()[1].handle==0x10008&&hid.classOfDevice()==0);
 bth_sdp_t server{};CHECK(bth_sdp_init(&server,hid.records(),hid.recordCount(),5));
 CHECK(Bytes(hid.records()[0].attributes.data,hid.records()[0].attributes.data+hid.records()[0].attributes.len)==keyboardRecord());
 CHECK(sdp(server,{2,0,1,0,8,0x35,3,0x19,0x11,0x24,0,4,0})==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,7,0}));
 CHECK(sdp(server,{2,0,1,0,8,0x35,3,0x19,0x12,0x00,0,4,0})==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,8,0}));
 CHECK(sdp(server,{2,0,1,0,8,0x35,3,0x19,0x00,0x11,0,4,0})==Bytes({3,0,1,0,9,0,1,0,1,0,1,0,7,0})); // HIDP UUID.
 // Attribute 0x0206 alone: the descriptor bytes come back intact.
 const Bytes one=sdp(server,{6,0,1,0,13,0x35,3,0x19,0x11,0x24,0x02,0xa0,0x35,3,0x09,0x02,0x06,0});
 const Bytes want=cat({{0x09,0x02,0x06,0x35,0x45,0x35,0x43,0x08,0x22,0x25,0x3f},Bytes(hid_descriptors::BootKeyboard,hid_descriptors::BootKeyboard+63)});
 CHECK(std::search(one.begin(),one.end(),want.begin(),want.end())!=one.end());
 hid.end();CHECK(!hid.enabled()&&!hid.records()&&!hid.recordCount());
 HidConfig no_pnp=keyboard();no_pnp.device_id=false;no_pnp.class_of_device=HidClassOfDeviceKeyboard;CHECK(hid.begin(no_pnp)&&hid.recordCount()==1&&hid.classOfDevice()==0x240540);
 CHECK(!hid.begin(HidConfig())&&!hid.enabled());
}
static void state(){
 // Attached control FIFO; nothing drains it here (the actor suites do).
 bth_control_tx_t q{};bth_control_tx_init(&q);CHECK(bth_control_tx_attach(&q,0,0x25,7));
 HidDevice hid;CHECK(hid.begin(combo()));hid.bind(q);
 const uint8_t key[8]={0,0,4};CHECK(hid.send(1,key,8)==Status::InvalidArgument); // Not connected.
 CHECK(!hid.channelOpened(7,0x11,0x40,0x70,672)); // Owner not yet bound.
 hid.reset(7);CHECK(!hid.channelOpened(8,0x11,0x40,0x70,672)&&!hid.channelOpened(7,0x11,0x40,0x70,47)&&!hid.channelOpened(7,0x19,0x40,0x70,672));
 // Interrupt first is accepted; connected only with both channels.
 CHECK(hid.channelOpened(7,0x13,0x41,0x71,672)&&!hid.connected()&&!hid.channelOpened(7,0x13,0x42,0x72,672));
 CHECK(hid.channelOpened(7,0x11,0x40,0x70,672)&&hid.connected()&&hid.stats().generation==1&&hid.stats().connections==1);
 CHECK(hid.channelMtuUpdated(7,0x13,0x41,0x71,100)&&!hid.channelMtuUpdated(7,0x13,0x41,0x72,100));
 // Exact input lengths and IDs only.
 CHECK(hid.send(1,key,7)==Status::InvalidArgument&&hid.send(0,key,8)==Status::InvalidArgument&&hid.send(9,key,1)==Status::InvalidArgument&&hid.send(1,nullptr,8)==Status::InvalidArgument);
 CHECK(hid.send(2,key,4)==Status::Ok&&hid.send(3,key,2)==Status::Ok);
 for(unsigned i=2;i<HidDevice::QueueSlots;++i)CHECK(hid.send(1,key,8)==Status::Ok);
 CHECK(hid.send(1,key,8)==Status::OutputFull&&hid.stats().reports_rejected==1&&hid.stats().tx_pending==8&&hid.stats().reports_queued==8);
 // Interrupt close: disconnected, queue dropped, control kept; reopen = new generation.
 hid.channelClosed(0x41);CHECK(!hid.connected()&&hid.stats().reports_dropped==8&&hid.stats().tx_pending==0&&hid.controlCid()==0x40&&!hid.closeRequested());
 CHECK(hid.send(1,key,8)==Status::InvalidArgument);
 CHECK(hid.channelOpened(7,0x13,0x43,0x73,672)&&hid.connected()&&hid.stats().generation==2&&hid.stats().connections==2);
 // Control close retires the interrupt channel too.
 hid.channelClosed(0x40);CHECK(!hid.connected()&&hid.closeRequested()&&hid.closingCid()==0x43);
 hid.channelClosed(0x43);CHECK(!hid.linked()&&!hid.closeRequested()&&hid.closingCid()==0);
 // Reopen at the minimum MTU; an update below 48 is refused (the actor closes).
 CHECK(hid.channelOpened(7,0x11,0x40,0x70,672)&&hid.channelOpened(7,0x13,0x41,0x71,48));
 CHECK(!hid.channelMtuUpdated(7,0x13,0x41,0x71,47)&&hid.send(1,key,8)==Status::Ok&&hid.stats().generation==3);
 // Security loss: requestClose drops admission and asks for interrupt first.
 hid.receive(0x40,key,1,false,0);CHECK(!hid.connected()&&hid.closingCid()==0x41&&hid.stats().reports_dropped==9);
 hid.reset(7);CHECK(!hid.linked()&&!hid.closeRequested());
 // Disabled endpoint.
 hid.end();CHECK(hid.send(1,key,8)==Status::Unsupported&&!hid.connected()&&!hid.stats().enabled);
}
int main(){setvbuf(stdout,nullptr,_IONBF,0);layouts();validation();records();state();
 printf("HID_ENDPOINT_RECORD_LAYOUT_STATE_PASS checks=%u\n",checks);return 0;}
