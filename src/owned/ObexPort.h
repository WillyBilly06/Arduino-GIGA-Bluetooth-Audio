#pragma once
#include "../GigaObex.h"
#include <cstring>
extern "C" {
#include "core/bth_sdp.h"
#include "core/bth_obex.h"
#include "core/bth_sdp_client.h"
}
namespace giga_bredr { namespace detail {
// OBEX endpoints on the shared RFCOMM owner: one Object Push server DLC and one
// Phonebook Access client DLC, plus the PSE SDP lookup that precedes it. Fixed
// caller-reserved storage only. Actor methods run under the radio owner's
// serialization; begin/end/request/abort/pop/stats/deliver are foreground
// methods (radio mutex held, except deliver) and never call the C core, so the
// public facade links without it. Packets are reassembled whole (at most
// PacketBytes, the advertised OBEX maximum), processed only when the event ring
// has room for every resulting record, and their response is the backpressure:
// an OBEX peer cannot send the next request before it. No full-object buffering.
class ObexPort {
public:
 static constexpr size_t PacketBytes=1024,TxBytes=512,NameBytes=96,TypeBytes=48,PathBytes=96,ChunkBytes=240,
  MinimumRing=2048,MaximumRing=16384,NameLimit=32,RecordBytes=128,SdpBytes=640,EndReserve=16,
  BeginBytes=3+4+1+NameBytes+1+TypeBytes;
 static constexpr uint32_t OppHandle=0x10007,PceHandle=0x10008;
 static constexpr uint8_t Window=7; // Initial/maximum RX credits per OBEX DLC (7*127 < PacketBytes).
 static constexpr uint32_t ConnectTimeoutMs=60000,ResponseTimeoutMs=30000,SetupTimeoutMs=20000;
 enum : uint8_t {Opp=0,Pbap=1,LinkCount=2};
 enum : uint8_t {ObjectBegin=1,ObjectData=2,ObjectEnd=3,PhonebookData=4,PhonebookEnd=5};
 ObexPort(){clearAll();}
 ObexPort(const ObexPort&)=delete;
 ObexPort& operator=(const ObexPort&)=delete;
 static size_t nameLength(const char* name){if(!name)return 0;size_t n=0;while(n<=NameLimit&&name[n])++n;return n<=NameLimit?n:0;}
 // Exact SDP attribute bytes (sorted attribute IDs) for the OPP server record.
 static size_t buildOppRecord(uint8_t* out,size_t capacity,uint8_t channel,const char* name){
  const size_t length=nameLength(name);if(!out||!length||!channel||channel>30)return 0;
  const uint8_t head[]={
   0x09,0x00,0x00,0x0a,uint8_t(OppHandle>>24),uint8_t(OppHandle>>16),uint8_t(OppHandle>>8),uint8_t(OppHandle),
   0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x05,                                                  // ServiceClassIDList: OBEXObjectPush
   0x09,0x00,0x04,0x35,0x11,0x35,0x03,0x19,0x01,0x00,0x35,0x05,0x19,0x00,0x03,0x08,channel,   // L2CAP, RFCOMM(channel),
   0x35,0x03,0x19,0x00,0x08,                                                                 //   OBEX
   0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,                                                  // BrowseGroupList: public
   0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00,                    // Language base: en, UTF-8, 0x0100
   0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x05,0x09,0x01,0x01,                         // Profile: OPP 1.1 (RFCOMM only)
   0x09,0x01,0x00,0x25,uint8_t(length)};                                                     // ServiceName
  const uint8_t tail[]={0x09,0x03,0x03,0x35,0x02,0x08,0xff};                                 // Supported formats: any
  if(capacity<sizeof head+length+sizeof tail)return 0;
  memcpy(out,head,sizeof head);memcpy(out+sizeof head,name,length);memcpy(out+sizeof head+length,tail,sizeof tail);
  return sizeof head+length+sizeof tail;
 }
 // PBAP client record: service class only (a PCE has no protocol descriptor).
 static size_t buildPceRecord(uint8_t* out,size_t capacity,const char* name){
  const size_t length=nameLength(name);if(!out||!length)return 0;
  const uint8_t head[]={
   0x09,0x00,0x00,0x0a,uint8_t(PceHandle>>24),uint8_t(PceHandle>>16),uint8_t(PceHandle>>8),uint8_t(PceHandle),
   0x09,0x00,0x01,0x35,0x03,0x19,0x11,0x2e,                                                  // ServiceClassIDList: PBAP PCE
   0x09,0x00,0x05,0x35,0x03,0x19,0x10,0x02,                                                  // BrowseGroupList: public
   0x09,0x00,0x06,0x35,0x09,0x09,0x65,0x6e,0x09,0x00,0x6a,0x09,0x01,0x00,                    // Language base
   0x09,0x00,0x09,0x35,0x08,0x35,0x06,0x19,0x11,0x30,0x09,0x01,0x01,                         // Profile: PBAP 1.1
   0x09,0x01,0x00,0x25,uint8_t(length)};
  if(capacity<sizeof head+length)return 0;
  memcpy(out,head,sizeof head);memcpy(out+sizeof head,name,length);return sizeof head+length;
 }
 // ---- Foreground (radio mutex held; no C core calls) ----
 bool begin(const ObexConfig& c,uint8_t* ring,size_t ring_size){
  end();
  if((!c.opp_enabled&&!c.pbap_enabled)||!ring||ring_size<MinimumRing||ring_size>MaximumRing)return false;
  size_t count=0;
  if(c.opp_enabled){const size_t n=buildOppRecord(record_bytes_[count],RecordBytes,c.opp_channel,c.opp_service_name);if(!n)return false;records_[count]={OppHandle,{record_bytes_[count],n}};++count;}
  if(c.pbap_enabled){const size_t n=buildPceRecord(record_bytes_[count],RecordBytes,c.pbap_service_name);if(!n){end();return false;}records_[count]={PceHandle,{record_bytes_[count],n}};++count;}
  record_count_=count;opp_=c.opp_enabled;pbap_=c.pbap_enabled;channel_=opp_?c.opp_channel:0;interval_=c.streaming_frame_interval_ms;
  ring_=ring;ring_size_=ring_size;active_=true;return true;
 }
 void end(){clearAll();}
 bool enabled()const{return active_;}
 bool oppEnabled()const{return active_&&opp_;}
 bool pbapEnabled()const{return active_&&pbap_;}
 uint8_t oppChannel()const{return channel_;}
 uint8_t streamingIntervalMs()const{return interval_;}
 const bth_sdp_record_t* records()const{return active_&&record_count_?records_:nullptr;}
 size_t recordCount()const{return active_?record_count_:0;}
 size_t uuids(uint16_t* out,size_t capacity)const{
  size_t n=0;if(!active_||!out)return 0;
  if(opp_&&n<capacity)out[n++]=0x1105;if(pbap_&&n<capacity)out[n++]=0x112e;return n;
 }
 // Admits one PBAP operation. Busy while another is active (until on_phonebook_end
 // has been queued and its DLC has closed).
 Status request(PbapOperation op,const char* path,uint16_t max_count,uint16_t offset){
  if(!active_||!pbap_)return Status::Unsupported;
  if((op!=PbapOperation::PullPhonebook&&op!=PbapOperation::PullVcardListing)||!path)return Status::InvalidArgument;
  size_t n=0;while(n<PathBytes&&path[n])++n;
  if(n==PathBytes||(op==PbapOperation::PullPhonebook&&!n))return Status::InvalidArgument;
  if(phase_!=Phase::Idle)return Status::Busy;
  memcpy(path_,path,n);path_[n]=0;op_=op;max_count_=max_count;offset_=offset;cursor_=0;
  result_=PbapResult();result_.operation=op;result_done_=false;abort_=false;have_id_=false;connection_id_=0;expect_=Expect::None;
  phase_=pse_channel_&&owner_&&pse_owner_==owner_?Phase::Transport:Phase::Sdp;deadline_=0;return Status::Ok;
 }
 Status abort(){
  if(!active_||!pbap_)return Status::Unsupported;
  if(phase_==Phase::Idle||phase_==Phase::Closing||result_done_)return Status::InvalidArgument;
  if(phase_==Phase::Obex){abort_=true;return Status::Ok;} // ABORT/DISCONNECT at the next OBEX turn.
  finish(false,0,true);if(links_[Pbap].dlci)links_[Pbap].close=true;return Status::Ok;
 }
 // Copies one queued record (payload at most ChunkBytes). False when empty.
 bool pop(uint8_t& kind,uint8_t* out,size_t capacity,size_t& length){
  length=0;kind=0;if(!active_||ring_count_<3||!out)return false;
  uint8_t head[3];peek(head,3,0);const size_t n=size_t(head[1])<<8|head[2];
  if(n>capacity||ring_count_<3+n)return false;
  peek(out,n,3);ring_read_=(ring_read_+3+n)%ring_size_;ring_count_-=3+n;kind=head[0];length=n;return true;
 }
 ObexStats stats()const{
  ObexStats s=stats_;s.opp_enabled=active_&&opp_;s.pbap_enabled=active_&&pbap_;s.opp_connected=links_[Opp].open;
  s.pbap_state=publicState();s.pse_channel=pse_channel_;s.pse_version=pse_version_;s.event_bytes_pending=uint32_t(ring_count_);return s;
 }
 // Decodes one popped record into the sketch callbacks (no lock held).
 static void deliver(const ObexConfig& c,uint8_t kind,const uint8_t* p,size_t n){
  if(!p)return;
  switch(kind){
  case ObjectBegin:{
   if(n<6)return;const uint32_t length=be32(p);const size_t name_length=p[4];if(name_length>=NameBytes||5+name_length+1>n)return;
   const size_t type_length=p[5+name_length];if(type_length>=TypeBytes||6+name_length+type_length!=n)return;
   char name[NameBytes],type[TypeBytes];memcpy(name,p+5,name_length);name[name_length]=0;memcpy(type,p+6+name_length,type_length);type[type_length]=0;
   if(c.on_object_begin)c.on_object_begin(name,type,length,c.user);return;}
  case ObjectData:if(n&&c.on_object_data)c.on_object_data(p,n,c.user);return;
  case ObjectEnd:if(n==1&&c.on_object_end)c.on_object_end(p[0]!=0,c.user);return;
  case PhonebookData:if(n&&c.on_phonebook_data)c.on_phonebook_data(p,n,c.user);return;
  case PhonebookEnd:{
   if(n!=12)return;PbapResult r;r.operation=PbapOperation(p[0]);r.ok=p[1]!=0;r.aborted=p[2]!=0;r.response=p[3];
   r.phonebook_size=uint16_t(p[4]<<8|p[5]);r.bytes=be32(p+6);if(c.on_phonebook_end)c.on_phonebook_end(r,c.user);return;}
  default:return;
  }
 }
 // ---- Actor: RFCOMM owner (VoiceProfiles) ----
 // A new ACL owner (or 0 on detach/end) retires every link and the PSE cache.
 void reset(uint32_t owner){
  if(!active_)return;
  muxClosed();if(phase_!=Phase::Idle)finish(false,0,false);
  sdp_channel_=sdp_ready_=false;phase_=Phase::Idle;expect_=Expect::None;
  if(owner!=owner_){owner_=owner;if(pse_owner_!=owner){pse_owner_=0;pse_channel_=0;pse_version_=0;}}
 }
 bool claims(uint8_t dlci,bool initiator)const{
  if(!active_||!dlci)return false;
  if(pbap_&&links_[Pbap].dlci==dlci&&!links_[Pbap].open)return true;
  return opp_&&!links_[Opp].dlci&&(dlci>>1)==channel_&&(dlci&1)==(initiator?1:0);
 }
 bool owns(uint8_t dlci)const{return active_&&linkOf(dlci)>=0;}
 void opened(uint8_t dlci,uint16_t mtu,bool credit,uint32_t generation){
  if(!active_||!dlci)return;
  const unsigned i=pbap_&&links_[Pbap].dlci==dlci&&!links_[Pbap].open?Pbap:Opp;auto& l=links_[i];
  clearLink(l);l.dlci=dlci;l.open=true;l.mtu=mtu;l.credit=credit;l.generation=generation;
  if(!mtu){l.close=true;return;}
  if(i==Opp){object_active_=connected_=false;return;}
  if(phase_!=Phase::DlcOpening){l.close=true;return;} // Late DLC after a failed/aborted operation.
  phase_=Phase::Obex;sendConnect();
 }
 void closed(uint8_t dlci){
  if(!active_||!dlci)return;
  for(unsigned i=0;i<LinkCount;++i){auto& l=links_[i];if(l.dlci!=dlci)continue;
   if(i==Opp){if(object_active_)endObject(false);connected_=false;}
   else if(!result_done_&&(phase_==Phase::DlcOpening||phase_==Phase::Obex))finish(false,0,false);
   clearLink(l);
  }
  settle();
 }
 void muxClosed(){if(!active_)return;for(unsigned i=0;i<LinkCount;++i)if(links_[i].dlci)closed(links_[i].dlci);if(phase_==Phase::DlcOpening||phase_==Phase::Obex)finish(false,0,false);settle();}
 // Copies one RFCOMM payload into the link's packet assembler. A packet larger
 // than PacketBytes, or bytes beyond the current packet, close the DLC.
 void receive(uint8_t dlci,const uint8_t* p,size_t n){
  const int i=linkOf(dlci);if(i<0)return;auto& l=links_[i];if(l.close)return;
  size_t length=0;if(bth_obex_assemble(&l.assembler,p,n,&length)<0){++stats_.protocol_errors;protocolError(unsigned(i));}
 }
 // Processes complete packets that fit the event ring; expires PBAP stages.
 void service(uint32_t now){
  if(!active_)return;now_=now;
  for(unsigned i=0;i<LinkCount;++i)processLink(i);
  if(phase_==Phase::Idle)return;
  if(!deadline_)deadline_=now+SetupTimeoutMs;
  else if(int32_t(now-deadline_)>=0){deadline_=now+ResponseTimeoutMs;timeout();}
 }
 bool wantsDlc(uint8_t& scn)const{scn=0;if(!active_||phase_!=Phase::Transport||!pse_channel_||links_[Pbap].dlci)return false;scn=pse_channel_;return true;}
 void dlcRequested(uint8_t dlci){clearLink(links_[Pbap]);links_[Pbap].dlci=dlci;phase_=Phase::DlcOpening;deadline_=now_+ResponseTimeoutMs;}
 // The owner could not open the transport (PSM 3 refused, DLC admission failed).
 void transportRefused(){if(active_&&(phase_==Phase::Transport||phase_==Phase::DlcOpening)&&!result_done_){finish(false,0,false);if(links_[Pbap].dlci)links_[Pbap].close=true;}}
 // No Classic ACL: nothing can progress; fail the operation now.
 void linkLost(){if(!active_||phase_==Phase::Idle)return;muxClosed();if(!result_done_)finish(false,0,false);sdp_channel_=sdp_ready_=false;clearLink(links_[Pbap]);phase_=Phase::Idle;}
 bool wantsTransport()const{return active_&&phase_==Phase::Transport&&!links_[Pbap].dlci;}
 // True when nothing of ours needs the RFCOMM mux (an initiated mux may close).
 bool transportIdle()const{return !links_[Opp].dlci&&!links_[Pbap].dlci&&(phase_==Phase::Idle||phase_==Phase::Sdp||phase_==Phase::SdpOpen);}
 uint8_t dlci(unsigned i)const{return i<LinkCount&&links_[i].open?links_[i].dlci:0;}
 bool wantsClose(unsigned i)const{return i<LinkCount&&links_[i].dlci&&links_[i].close&&!links_[i].close_sent;}
 uint8_t closeDlci(unsigned i)const{return i<LinkCount?links_[i].dlci:0;}
 void closeSent(unsigned i){if(i<LinkCount)links_[i].close_sent=true;}
 size_t txPending(unsigned i)const{const auto& l=links_[i];return i<LinkCount&&l.open&&!l.close?size_t(l.tx_length-l.tx_cursor):0;}
 size_t txPeek(unsigned i,uint8_t* out,size_t capacity)const{const size_t n=txPending(i)<capacity?txPending(i):capacity;if(n)memcpy(out,links_[i].tx+links_[i].tx_cursor,n);return n;}
 void txConsume(unsigned i,size_t n){auto& l=links_[i];if(n>size_t(l.tx_length-l.tx_cursor))n=l.tx_length-l.tx_cursor;l.tx_cursor=uint16_t(l.tx_cursor+n);if(l.tx_cursor==l.tx_length)l.tx_cursor=l.tx_length=0;}
 // ---- Actor: SDP client channel (OwnedHost) ----
 bool sdpWanted()const{return active_&&phase_==Phase::Sdp&&!sdp_channel_;}
 void sdpRefused(){if(active_&&phase_==Phase::Sdp&&!result_done_)finish(false,0,false);}
 void sdpConnecting(){sdp_channel_=true;sdp_ready_=false;if(phase_==Phase::Sdp)phase_=Phase::SdpOpen;deadline_=now_+SetupTimeoutMs;}
 void sdpOpened(uint16_t){
  if(!sdp_channel_)return;sdp_ready_=true;
  if(!bth_sdp_client_init(&sdp_,0x112f,sdp_bytes_,SdpBytes,uint16_t(++sdp_serial_))&&phase_==Phase::SdpOpen)finish(false,0,false);
 }
 bool sdpRequest(uint8_t* out,size_t capacity,size_t& n){
  n=0;if(!active_||phase_!=Phase::SdpOpen||!sdp_ready_||!sdp_.initialized||sdp_.awaiting||sdp_.complete)return false;
  if(bth_sdp_client_request(&sdp_,out,capacity,&n)!=BTH_OK){n=0;finish(false,0,false);return false;}return true;
 }
 void sdpReceive(const uint8_t* p,size_t n){
  if(!active_||phase_!=Phase::SdpOpen||!sdp_ready_)return;
  const auto st=bth_sdp_client_response(&sdp_,p,n);if(st==BTH_MORE)return;if(st!=BTH_OK){finish(false,0,false);return;}
  bth_sdp_service_t s{};if(bth_sdp_client_parse(sdp_bytes_,sdp_.used,&s)!=BTH_OK||!s.found){finish(false,0,false);return;}
  pse_channel_=s.rfcomm_channel;pse_version_=s.profile_version;pse_owner_=owner_;phase_=Phase::Transport;deadline_=now_+SetupTimeoutMs;
 }
 bool sdpFinished()const{return sdp_channel_&&phase_!=Phase::SdpOpen;}
 void sdpClosed(){if(!sdp_channel_)return;sdp_channel_=sdp_ready_=false;if(phase_==Phase::SdpOpen&&!result_done_)finish(false,0,false);settle();}
private:
 enum class Phase:uint8_t {Idle,Sdp,SdpOpen,Transport,DlcOpening,Obex,Closing};
 enum class Expect:uint8_t {None,Connect,SetPath,Get,Abort,Disconnect};
 struct Link {
  uint8_t rx[PacketBytes];uint8_t tx[TxBytes];bth_obex_assembler_t assembler;
  uint32_t generation;uint16_t mtu,tx_length,tx_cursor,peer_max;uint8_t dlci;bool open,credit,close,close_sent;
 };
 static uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
 static void clearLink(Link& l){
  l.assembler.data=l.rx;l.assembler.capacity=PacketBytes;l.assembler.used=0;l.generation=0;
  l.mtu=l.tx_length=l.tx_cursor=0;l.peer_max=BTH_OBEX_MIN_PACKET;l.dlci=0;l.open=l.credit=l.close=l.close_sent=false;
 }
 void clearAll(){
  for(auto& l:links_)clearLink(l);
  memset(record_bytes_,0,sizeof record_bytes_);memset(records_,0,sizeof records_);record_count_=0;
  ring_=nullptr;ring_size_=ring_read_=ring_count_=0;stats_=ObexStats();
  opp_=pbap_=active_=object_active_=connected_=false;channel_=interval_=0;
  phase_=Phase::Idle;expect_=Expect::None;op_=PbapOperation::None;memset(path_,0,sizeof path_);max_count_=offset_=0;cursor_=0;
  result_=PbapResult();result_done_=abort_=have_id_=false;connection_id_=0;deadline_=now_=0;
  memset(&sdp_,0,sizeof sdp_);sdp_channel_=sdp_ready_=false;sdp_serial_=0;owner_=pse_owner_=0;pse_channel_=0;pse_version_=0;
 }
 int linkOf(uint8_t dlci)const{if(!dlci)return -1;for(unsigned i=0;i<LinkCount;++i)if(links_[i].open&&links_[i].dlci==dlci)return int(i);return -1;}
 PbapState publicState()const{
  switch(phase_){
  case Phase::Idle:return PbapState::Idle;case Phase::Sdp:case Phase::SdpOpen:return PbapState::ServiceDiscovery;
  case Phase::Transport:case Phase::DlcOpening:return PbapState::Transport;
  case Phase::Obex:return expect_==Expect::Connect?PbapState::Connecting:PbapState::Transferring;
  default:return PbapState::Closing;
  }
 }
 void settle(){if(phase_==Phase::Closing&&!links_[Pbap].dlci&&!sdp_channel_)phase_=Phase::Idle;}
 // ---- Event ring ----
 size_t ringFree()const{return ring_size_-ring_count_;}
 static size_t need(size_t packet){return packet+3*(packet/ChunkBytes+2)+BeginBytes+2*EndReserve;}
 void write(const uint8_t* p,size_t n){size_t at=(ring_read_+ring_count_)%ring_size_;for(size_t i=0;i<n;++i){ring_[at]=p[i];at=at+1==ring_size_?0:at+1;}ring_count_+=n;}
 void peek(uint8_t* out,size_t n,size_t offset)const{size_t at=(ring_read_+offset)%ring_size_;for(size_t i=0;i<n;++i){out[i]=ring_[at];at=at+1==ring_size_?0:at+1;}}
 bool push(uint8_t kind,const uint8_t* p,size_t n){
  if(!ring_||n>ChunkBytes||3+n>ringFree())return false;const uint8_t head[3]={kind,uint8_t(n>>8),uint8_t(n)};write(head,3);if(n)write(p,n);return true;
 }
 void pushData(uint8_t kind,const uint8_t* p,size_t n){while(n){const size_t k=n<ChunkBytes?n:ChunkBytes;push(kind,p,k);p+=k;n-=k;}}
 void processLink(unsigned i){
  auto& l=links_[i];if(!l.open||l.close||l.tx_length||!bth_obex_assembler_complete(&l.assembler))return;
  const size_t n=l.assembler.used;if(ringFree()<need(n))return; // Response (and peer) wait for loop().
  if(i==Opp)handleOpp(l.rx,n);else handlePbap(l.rx,n);
  l.assembler.used=0;
 }
 void protocolError(unsigned i){
  auto& l=links_[i];l.close=true;l.assembler.used=0;
  if(i==Opp){if(object_active_)endObject(false);}else if(!result_done_)finish(false,0,false);
 }
 bool queue(unsigned i,bth_obex_writer_t& w){
  auto& l=links_[i];size_t n=0;
  if(bth_obex_finish(&w,&n)!=BTH_OK||n>l.peer_max||n>TxBytes){l.close=true;return false;}
  l.tx_length=uint16_t(n);l.tx_cursor=0;return true;
 }
 // ---- OPP server ----
 void respond(uint8_t code,bool connect=false){
  bth_obex_writer_t w;bth_obex_writer_init(&w,links_[Opp].tx,TxBytes,code);
  if(connect){bth_obex_put_u8(&w,BTH_OBEX_VERSION);bth_obex_put_u8(&w,0);bth_obex_put_u16(&w,uint16_t(PacketBytes));}
  queue(Opp,w);
 }
 void endObject(bool ok){const uint8_t v=ok;push(ObjectEnd,&v,1);object_active_=false;if(ok)++stats_.objects_completed;else ++stats_.objects_failed;}
 void handleOpp(const uint8_t* p,size_t n){
  bth_obex_request_t r;bth_obex_header_t h;
  if(bth_obex_parse_request(p,n,&r)!=BTH_OK){++stats_.protocol_errors;if(object_active_)endObject(false);respond(BTH_OBEX_BAD_REQUEST);return;}
  switch(r.opcode){
  case BTH_OBEX_CONNECT:
   if(object_active_)endObject(false);
   // OPP uses the default (inbox) service: a directed Target is another profile.
   if(bth_obex_header_find(r.headers,BTH_OBEX_H_TARGET,&h)==BTH_OK){respond(BTH_OBEX_SERVICE_UNAVAILABLE,true);return;}
   if(r.max_packet<BTH_OBEX_MIN_PACKET){respond(BTH_OBEX_BAD_REQUEST,true);return;}
   links_[Opp].peer_max=r.max_packet<TxBytes?r.max_packet:uint16_t(TxBytes);connected_=true;respond(BTH_OBEX_SUCCESS,true);return;
  case BTH_OBEX_DISCONNECT:if(object_active_)endObject(false);connected_=false;respond(BTH_OBEX_SUCCESS);return;
  case BTH_OBEX_ABORT:if(object_active_)endObject(false);respond(BTH_OBEX_SUCCESS);return;
  case BTH_OBEX_PUT:handlePut(r);return;
  default:respond(BTH_OBEX_NOT_IMPLEMENTED);return; // GET (business-card pull), SETPATH, reserved.
  }
 }
 void handlePut(const bth_obex_request_t& r){
  bth_span_t rest=r.headers;bth_obex_header_t h;bool body=false,named=false;uint32_t length=ObexUnknownLength;
  char name[NameBytes]={};char type[TypeBytes]={};size_t type_length=0;
  while(bth_obex_header_next(&rest,&h)==BTH_OK){
   if(h.id==BTH_OBEX_H_NAME&&!named){bth_obex_text_to_utf8(h.data,name,sizeof name);named=true;}
   else if(h.id==BTH_OBEX_H_TYPE&&!type_length){for(size_t i=0;i<h.data.len&&type_length+1<TypeBytes&&h.data.data[i];++i){const uint8_t c=h.data.data[i];type[type_length++]=char(c>=0x20&&c<0x7f?c:'?');}type[type_length]=0;}
   else if(h.id==BTH_OBEX_H_LENGTH)length=h.value;
   else if(h.id==BTH_OBEX_H_BODY||h.id==BTH_OBEX_H_END_OF_BODY)body=true;
  }
  if(!object_active_){
   // A final PUT without any body header is an OBEX delete: not supported here.
   if(r.final&&!body){++stats_.objects_rejected;respond(BTH_OBEX_FORBIDDEN);return;}
   object_active_=true;
   uint8_t begin[BeginBytes];size_t k=0;const size_t name_length=strlen(name);
   begin[k++]=uint8_t(length>>24);begin[k++]=uint8_t(length>>16);begin[k++]=uint8_t(length>>8);begin[k++]=uint8_t(length);
   begin[k++]=uint8_t(name_length);memcpy(begin+k,name,name_length);k+=name_length;begin[k++]=uint8_t(type_length);memcpy(begin+k,type,type_length);k+=type_length;
   push(ObjectBegin,begin,k);
  }
  rest=r.headers;
  while(bth_obex_header_next(&rest,&h)==BTH_OK)if(h.id==BTH_OBEX_H_BODY||h.id==BTH_OBEX_H_END_OF_BODY){pushData(ObjectData,h.data.data,h.data.len);stats_.object_bytes+=uint32_t(h.data.len);}
  if(r.final){endObject(true);respond(BTH_OBEX_SUCCESS);}else respond(BTH_OBEX_CONTINUE);
 }
 // ---- PBAP client ----
 void finish(bool ok,uint8_t code,bool aborted){
  if(result_done_)return;result_done_=true;result_.ok=ok;result_.response=code;result_.aborted=aborted;
  const uint8_t p[12]={uint8_t(result_.operation),uint8_t(ok),uint8_t(aborted),code,uint8_t(result_.phonebook_size>>8),uint8_t(result_.phonebook_size),
   uint8_t(result_.bytes>>24),uint8_t(result_.bytes>>16),uint8_t(result_.bytes>>8),uint8_t(result_.bytes),0,0};
  push(PhonebookEnd,p,sizeof p);if(ok)++stats_.pbap_completed;else ++stats_.pbap_failed;if(code)stats_.last_response=code;
  phase_=Phase::Closing;settle();
 }
 void sendConnect(){
  static const uint8_t target[16]={0x79,0x61,0x35,0xf0,0xf0,0xc5,0x11,0xd8,0x09,0x66,0x08,0x00,0x20,0x0c,0x9a,0x66};
  bth_obex_writer_t w;bth_obex_writer_init(&w,links_[Pbap].tx,TxBytes,BTH_OBEX_CONNECT);
  bth_obex_put_u8(&w,BTH_OBEX_VERSION);bth_obex_put_u8(&w,0);bth_obex_put_u16(&w,uint16_t(PacketBytes));
  bth_obex_add_bytes(&w,BTH_OBEX_H_TARGET,target,sizeof target);
  if(send(w,Expect::Connect))deadline_=now_+ConnectTimeoutMs; // The phone may prompt its user first.
 }
 void header(bth_obex_writer_t& w){if(have_id_)bth_obex_add_u32(&w,BTH_OBEX_H_CONNECTION_ID,connection_id_);}
 bool send(bth_obex_writer_t& w,Expect expect){
  if(!queue(Pbap,w)){finish(false,0,false);return false;}expect_=expect;deadline_=now_+ResponseTimeoutMs;return true;
 }
 void sendGet(bool first){
  bth_obex_writer_t w;bth_obex_writer_init(&w,links_[Pbap].tx,TxBytes,BTH_OBEX_GET|BTH_OBEX_FINAL);
  if(first){
   const bool listing=op_==PbapOperation::PullVcardListing;header(w);
   if(listing)bth_obex_add_text(&w,BTH_OBEX_H_NAME,"",0);else bth_obex_add_text(&w,BTH_OBEX_H_NAME,path_,strlen(path_));
   static const char phonebook[]="x-bt/phonebook",vcards[]="x-bt/vcard-listing";
   if(listing)bth_obex_add_bytes(&w,BTH_OBEX_H_TYPE,reinterpret_cast<const uint8_t*>(vcards),sizeof vcards);
   else bth_obex_add_bytes(&w,BTH_OBEX_H_TYPE,reinterpret_cast<const uint8_t*>(phonebook),sizeof phonebook);
   uint8_t params[11];size_t k=0;
   params[k++]=0x04;params[k++]=2;params[k++]=uint8_t(max_count_>>8);params[k++]=uint8_t(max_count_);        // MaxListCount
   params[k++]=0x05;params[k++]=2;params[k++]=uint8_t(offset_>>8);params[k++]=uint8_t(offset_);              // ListStartOffset
   if(!listing){params[k++]=0x07;params[k++]=1;params[k++]=0x00;}                                              // Format: vCard 2.1
   bth_obex_add_bytes(&w,BTH_OBEX_H_APP_PARAMS,params,k);
  }
  send(w,Expect::Get);
 }
 void sendSetPath(const char* name,size_t n){
  bth_obex_writer_t w;bth_obex_writer_init(&w,links_[Pbap].tx,TxBytes,BTH_OBEX_SETPATH);
  bth_obex_put_u8(&w,0x02);bth_obex_put_u8(&w,0);header(w);bth_obex_add_text(&w,BTH_OBEX_H_NAME,name,n);send(w,Expect::SetPath);
 }
 void sendSimple(uint8_t opcode,Expect expect){bth_obex_writer_t w;bth_obex_writer_init(&w,links_[Pbap].tx,TxBytes,opcode);header(w);send(w,expect);}
 void sendDisconnect(){sendSimple(BTH_OBEX_DISCONNECT,Expect::Disconnect);}
 void nextStep(){
  if(op_==PbapOperation::PullVcardListing){
   while(cursor_<PathBytes&&path_[cursor_]=='/')++cursor_;
   if(cursor_<PathBytes&&path_[cursor_]){size_t end=cursor_;while(end<PathBytes&&path_[end]&&path_[end]!='/')++end;const size_t at=cursor_;cursor_=end;sendSetPath(path_+at,end-at);return;}
  }
  sendGet(true);
 }
 void handlePbap(const uint8_t* p,size_t n){
  auto& l=links_[Pbap];
  if(expect_==Expect::None||(phase_!=Phase::Obex&&phase_!=Phase::Closing)){++stats_.protocol_errors;l.close=true;return;} // Unsolicited.
  bth_obex_response_t r;
  if(bth_obex_parse_response(p,n,expect_==Expect::Connect,&r)!=BTH_OK){++stats_.protocol_errors;finish(false,0,false);l.close=true;return;}
  const Expect was=expect_;expect_=Expect::None;deadline_=now_+ResponseTimeoutMs;
  switch(was){
  case Expect::Connect:{
   if(r.code!=BTH_OBEX_SUCCESS){finish(false,r.code,false);l.close=true;return;}
   bth_obex_header_t h;have_id_=bth_obex_header_find(r.headers,BTH_OBEX_H_CONNECTION_ID,&h)==BTH_OK;connection_id_=have_id_?h.value:0;
   l.peer_max=r.max_packet<BTH_OBEX_MIN_PACKET?uint16_t(BTH_OBEX_MIN_PACKET):(r.max_packet<TxBytes?r.max_packet:uint16_t(TxBytes));
   if(abort_){finish(false,r.code,true);sendDisconnect();return;}
   nextStep();return;}
  case Expect::SetPath:
   if(r.code!=BTH_OBEX_SUCCESS){finish(false,r.code,false);sendDisconnect();return;}
   if(abort_){finish(false,r.code,true);sendDisconnect();return;}
   nextStep();return;
  case Expect::Get:{
   bth_span_t rest=r.headers;bth_obex_header_t h;
   while(bth_obex_header_next(&rest,&h)==BTH_OK){
    if(h.id==BTH_OBEX_H_BODY||h.id==BTH_OBEX_H_END_OF_BODY){pushData(PhonebookData,h.data.data,h.data.len);result_.bytes+=uint32_t(h.data.len);stats_.pbap_bytes+=uint32_t(h.data.len);}
    else if(h.id==BTH_OBEX_H_APP_PARAMS){bth_span_t tlv=h.data;uint8_t tag;bth_span_t value;
     while(bth_obex_tlv_next(&tlv,&tag,&value)==BTH_OK)if(tag==0x08&&value.len==2)result_.phonebook_size=uint16_t(value.data[0]<<8|value.data[1]);}
   }
   if(r.code==BTH_OBEX_CONTINUE){if(abort_)sendSimple(BTH_OBEX_ABORT,Expect::Abort);else sendGet(false);return;}
   finish(r.code==BTH_OBEX_SUCCESS,r.code,false);sendDisconnect();return;}
  case Expect::Abort:finish(false,r.code,true);sendDisconnect();return;
  case Expect::Disconnect:l.close=true;return;
  default:return;
  }
 }
 void timeout(){
  auto& l=links_[Pbap];
  switch(phase_){
  case Phase::Sdp:case Phase::SdpOpen:case Phase::Transport:case Phase::DlcOpening:case Phase::Obex:
   if(!result_done_)finish(false,0,false);if(l.dlci)l.close=true;break;
  case Phase::Closing:if(l.dlci)l.close=true;settle();break;
  default:break;
  }
 }
 Link links_[LinkCount];
 uint8_t record_bytes_[2][RecordBytes];bth_sdp_record_t records_[2];size_t record_count_;
 uint8_t* ring_;size_t ring_size_,ring_read_,ring_count_;
 ObexStats stats_;
 bool opp_,pbap_,active_,object_active_,connected_;uint8_t channel_,interval_;
 Phase phase_;Expect expect_;PbapOperation op_;char path_[PathBytes];uint16_t max_count_,offset_;size_t cursor_;
 PbapResult result_;bool result_done_,abort_,have_id_;uint32_t connection_id_,deadline_,now_;
 bth_sdp_client_t sdp_;uint8_t sdp_bytes_[SdpBytes];bool sdp_channel_,sdp_ready_;uint32_t sdp_serial_;
 uint32_t owner_,pse_owner_;uint8_t pse_channel_;uint16_t pse_version_;
};
}}
